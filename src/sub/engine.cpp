#include "sub/engine.h"
#include "macro/faction.h"
#include "sub/vk_camera_math.h"
#include "sub/lighting.h"
#include "gpu/vk_device.h"
#include "sub/spawn.h"
#include "sub/targeting.h"
#include "sub/ai.h"
#include "sub/battle.h"
#include "sub/spell_effects.h"
#include "sub/base_generator.h"
#include "sub/height.h"
#include "ecs/systems.h"
#include "macro/state.h"
#include "macro/npc.h"
#include "macro/items.h"
#include "macro/attributes.h"
#include "macro/character_sheet.h"
#include "macro/map_generator.h"
#include "macro/features.h"
#include "macro/biomes.h"
#include "macro/tree_layer.h"
#include "macro/seasons.h"
#include "macro/zones.h"
#include "core/rng.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace sm::sub {

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

bool seam_trace_enabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("TIMAERT_SEAM_TRACE");
        return env && env[0] != '\0' && env[0] != '0';
    }();
    return enabled;
}

// Fine-grid resolution ceiling. If the crowd is spread wider than
// kBattleGridMaxDim cells the cell GROWS instead of the allocation — density is
// low in that case, so the neighbour bound holds either way.
constexpr int kBattleGridMaxDim = 256;
// Faction id of the player side (the player body plus its projected soldiers).
// An ordinary id like any other — the battle pass interns it exactly the same
// way — it simply has no row in the macro relation matrix, because the player's
// standing with everyone is reputation.
constexpr const char* kPlayerFactionId = "player";
// Last-resort body radius for a body that carries neither an explicit
// ecs::BodyRadius nor a table row (a bare test/console entity). Roughly a
// human's half-width: 1 world unit ≈ 1 metre.
constexpr float kFallbackBodyRadius = 0.55f;
constexpr int kMaxSubworldDeathsPerFrame = 512;
constexpr int kMaxSubworldEntityReaps = 2048;
constexpr float kSubworldFirstPersonMoveScale = 0.4f;
constexpr float kHitFlashDuration = 0.15f;
constexpr float kPlayerMeleeRange = 5.0f;
constexpr float kPlayerMeleeCooldown = 0.5f;
// Player combat body radius (BodyRadius component). Matches the TS authority's
// player entity radius and kSpellCasterRadius (1.5) so the player is struck at
// the same range through every universal path (melee, projectile, blast).
constexpr float kPlayerBodyRadius = 1.5f;
// Player carried-light (LightEmitter component). The player is the first honest
// point-light emitter: a warm lantern/torch glow gathered through the SAME
// universal path (view<Position, LightEmitter, SubworldTag>) that every future
// emitter — NPC torches, spell glows, lit windows — will use, with no
// player-special-case in the renderer. Additive over the directional sun, so it
// reads as a warm pool at night and is washed out by daylight on its own. Height
// offset seats it at roughly lantern/chest height above the feet position; the
// radius/intensity are tuned so the pool is readable in first person without
// blowing out the pixel-art palette. These are the "тюнер" knobs.
constexpr float kPlayerLightHeightM = 1.2f;
constexpr float kPlayerLightRadiusM = 16.0f;
constexpr float kPlayerLightIntensity = 1.35f;
constexpr float kPlayerLightR = 1.00f; // warm orange-white lantern
constexpr float kPlayerLightG = 0.72f;
constexpr float kPlayerLightB = 0.42f;
constexpr int kAllyRepThreshold = 50;
constexpr int kKillRepPenalty = -1;
// Flight ceiling margin is sub::kFlightMaxAboveTerrainM (height.h): the
// ceiling itself is renderer3dVk_.max_height_m() + that margin — absolute for
// the loaded window, never below any terrain the window can show.
constexpr float kCameraEyeM = 1.7f;
constexpr std::uint32_t kFnvOffset =
    std::uint32_t{2147483647} + std::uint32_t{18652614};
constexpr std::uint32_t kFnvPrime = std::uint32_t{16777619};
constexpr std::uint32_t kCellSeedX = std::uint32_t{73856093};
constexpr std::uint32_t kCellSeedY = std::uint32_t{19349663};
constexpr std::uint32_t kSquadSpawnSalt =
    std::uint32_t{2147483647} + std::uint32_t{622657538};
constexpr std::uint32_t kMacroProjectionSalt =
    std::uint32_t{2147483647} + std::uint32_t{1181783497};
constexpr std::uint32_t kEntityLootMix =
    std::uint32_t{2147483647} + std::uint32_t{506952114};
constexpr std::uint32_t kNpcMissileSpellId = 0x4E50434Du; // "NPCM"

// Combat hit radius for entity `e`. Twin of the copy in sub/spell_effects.cpp
// (spells/projectiles); keep the two in lockstep. Prefer an explicit BodyRadius,
// then the AI mover's radius, then the billboard's scale, then a coarse fallback
// for anything that declares none of those.
// ── Body size and eyesight, straight from the authoring tables ─────────────
// Both are DATA, resolved from the one row that already defines the fighter:
// NpcTypeDef::combat for a humanoid, FaunaEntry for a creature (whose `radius`
// column also scales its sprite, so a body can never be a different size than it
// looks). Reading the row per tick — one array index — keeps them in sync with
// the tables for free: making a dragon wide and far-seeing is two numbers in
// fauna.cpp and no code at all. ecs::BodyRadius still wins when present, because
// the player body is the camera and carries an explicit radius.
const FaunaEntry* creature_row(const ecs::NPCKind* kind) {
    if (!kind || kind->type < std::uint16_t{0x100}) return nullptr;
    return creature_def_from_kind(kind->type);
}

const NpcTypeDef* humanoid_row(const ecs::NPCKind* kind) {
    if (!kind || kind->type >= std::uint16_t(NPCType::Count)) return nullptr;
    return &npc_def(static_cast<NPCType>(std::uint8_t(kind->type)));
}

float target_radius(const entt::registry& reg, entt::entity e) {
    if (const auto* br = reg.try_get<ecs::BodyRadius>(e)) return br->radius;
    const auto* kind = reg.try_get<ecs::NPCKind>(e);
    if (const FaunaEntry* cr = creature_row(kind)) {
        if (cr->radius > 0.0f) return cr->radius;
    }
    if (const NpcTypeDef* nd = humanoid_row(kind)) {
        if (nd->combat.bodyRadius > 0.0f) return nd->combat.bodyRadius;
    }
    if (const auto* ai = reg.try_get<ecs::SubworldAi>(e)) return ai->radius;
    if (const auto* sp = reg.try_get<ecs::Sprite>(e)) return sp->scale;
    return kFallbackBodyRadius;
}

float body_sight(const entt::registry& reg, entt::entity e) {
    const auto* kind = reg.try_get<ecs::NPCKind>(e);
    if (const FaunaEntry* cr = creature_row(kind)) {
        if (cr->combat.sight > 0.0f) return cr->combat.sight;
    }
    if (const NpcTypeDef* nd = humanoid_row(kind)) {
        if (nd->combat.sight > 0.0f) return nd->combat.sight;
    }
    return kDetectionRadius;
}

thread_local Rng* gLootRng = nullptr;

float loot_rng_f01() {
    return gLootRng ? gLootRng->next_f01() : 0.0f;
}

float dist2(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

float dist3sq(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx;
    const float dy = ay - by;
    const float dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

// ONE index space: NPCKind.factionIdx is an index into the faction registry
// (macro/faction.h) for humanoids and monsters alike. The two per-vocabulary
// dictionaries that used to live here — with colliding indices across the
// monster-type bit — are gone; unknown / kNoFaction degrades to the empty id,
// which every relation path treats as neutral.
const char* faction_id_for_kind(const ecs::NPCKind* kind) {
    return kind ? faction_id_for_index(kind->factionIdx) : "";
}

int player_reputation(const GameState* gs, const char* factionId) {
    if (!gs || !factionId || factionId[0] == '\0') return 0;
    auto it = gs->player.reputation.find(factionId);
    return it == gs->player.reputation.end() ? 0 : it->second;
}

void add_player_reputation(GameState& gs, const char* factionId, int delta) {
    if (!factionId || factionId[0] == '\0' || delta == 0) return;
    auto it = gs.player.reputation.find(factionId);
    if (it != gs.player.reputation.end()) {
        it->second += delta;
    } else {
        gs.player.reputation.emplace(factionId, delta);
    }
}

bool is_player_side(entt::registry& reg, entt::entity e) {
    return reg.any_of<ecs::PlayerTag, ecs::PlayerSoldierTag>(e);
}

bool token_equals(const char* raw, const char* lit) {
    if (!raw || !lit) return false;
    while (*raw && *lit) {
        char a = *raw;
        char b = *lit;
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
        ++raw;
        ++lit;
    }
    return *raw == '\0' && *lit == '\0';
}

NPCType npc_type_from_token(const char* token) {
    if (token_equals(token, "peasant")) return NPCType::Peasant;
    if (token_equals(token, "woodcutter")) return NPCType::Woodcutter;
    if (token_equals(token, "merchant")) return NPCType::Merchant;
    if (token_equals(token, "caravan")) return NPCType::Caravan;
    if (token_equals(token, "guard")) return NPCType::Guard;
    if (token_equals(token, "witch")) return NPCType::Witch;
    if (token_equals(token, "sorceress")) return NPCType::Sorceress;
    return NPCType::Bandit;
}

std::uint32_t string_hash(const char* s) {
    std::uint32_t h = kFnvOffset;
    if (!s) return h;
    while (*s) {
        h ^= std::uint8_t(*s++);
        h *= kFnvPrime;
    }
    return h;
}

ecs::NpcCharacter make_visual_character(std::uint32_t seed,
                                        NPCType type,
                                        const char* displayName) {
    Rng rng(seed ^ string_hash(displayName) ^
            (std::uint32_t(type) * std::uint32_t{2654435761}));
    ecs::NpcCharacter ch{};
    ch.visualSeed = rng.next_u32();
    ch.bodyShape = std::uint8_t(rng.next_u32() & std::uint32_t{0x3});
    ch.nameIdx = std::uint8_t(rng.next_u32() & std::uint32_t{0xF});
    ch.tintR = std::uint8_t(160 + int(rng.next_u32() % std::uint32_t{96}));
    ch.tintG = std::uint8_t(160 + int(rng.next_u32() % std::uint32_t{96}));
    ch.tintB = std::uint8_t(160 + int(rng.next_u32() % std::uint32_t{96}));
    return ch;
}

bool alive_subworld_entity(entt::registry& reg, entt::entity e) {
    const auto* h = reg.try_get<ecs::Health>(e);
    return h && h->hp > 0.0f && reg.all_of<ecs::SubworldTag>(e)
        && !reg.any_of<ecs::Dead>(e);
}

bool hostile_to_player_entity(entt::registry& reg,
                              entt::entity e,
                              const GameState* gs) {
    if (!alive_subworld_entity(reg, e) || is_player_side(reg, e)) {
        return false;
    }
    if (reg.any_of<ecs::TempHostileToPlayer>(e)) return true;
    const char* factionId = faction_id_for_kind(reg.try_get<ecs::NPCKind>(e));
    return player_reputation(gs, factionId) < kHostileThreshold;
}

// Symmetric macro faction relation for two faction-id strings, degrading
// SAFELY to 0 (neutral) for empty ids, unknown ids, or an absent matrix entry.
// Ids come from the ONE faction registry (macro/faction.h) — the historical
// "magika emitted but never registered" gap is closed there, by construction.
// createFactions() builds and save.cpp persists this matrix.
int faction_relation(const GameState* gs, const char* a, const char* b) {
    if (!gs || !a || !b || a[0] == '\0' || b[0] == '\0') return 0;
    if (std::strcmp(a, b) == 0) return 100;       // same faction → allied

    const auto itA = gs->factions.find(a);
    if (itA == gs->factions.end()) return 0;
    const auto itR = itA->second.relations.find(b);
    if (itR == itA->second.relations.end()) return 0;
    return itR->second;
}

// NOTE. The old per-pair `entities_hostile(reg, a, b, gs)` is GONE. It was the
// measured hot spot: two std::map<std::string> lookups plus strcmp for every
// candidate pair, and the target scan produced ~N² pairs per frame in a real
// battle. The same rules now live in one relation callback
// (SubworldEngine::battle_relation_callback) that build_faction_masks() applies
// once per tick over the factions PRESENT, yielding a 64-bit enemy mask each; "is j my enemy" is a shift and an AND (BattleUnits::hostile).
// Player-vs-NPC still reads reputation, NPC-vs-NPC still reads the macro faction
// matrix, and the per-entity TempHostileToPlayer exception rides in the unit's
// own mask — same semantics, integer cost.

const char* subworld_attacker_label(entt::registry& reg, entt::entity e) {
    const auto* kind = reg.try_get<ecs::NPCKind>(e);
    if (kind && kind->type < std::uint16_t(NPCType::Count)) {
        const NPCType type = static_cast<NPCType>(std::uint8_t(kind->type));
        return npc_def(type).label;
    }
    return "Hostile";
}

void maybe_emplace_missile_attack(entt::registry& reg,
                                  entt::entity e,
                                  const CombatTemplate& combat) {
    if (combat.attackKind != CombatTemplate::Missile) return;
    reg.emplace<ecs::MissileAttack>(
        e,
        combat.missileSpeed > 0.0f ? combat.missileSpeed : 200.0f,
        combat.missileBlast,
        combat.missileColorRGBA);
}

// Attach the NPC type's carried light (torch / lantern / arcane glow) if it has
// one — the SAME universal ecs::LightEmitter the player lantern, spell bolts and
// the settlement-spawn path use, so the renderer lights it with zero per-emitter
// code. File-local twin of the spawn.cpp helper (mirrors maybe_emplace_missile_
// attack, which is likewise duplicated per TU): the console / encounter spawn
// path here must light a guard exactly as the settlement populator does, so the
// rule lives wherever a humanoid is born. Strictly opt-in: lightRadius <= 0 (all
// rows but Guard) attaches nothing.
void maybe_emplace_carried_light(entt::registry& reg,
                                 entt::entity e,
                                 const NpcTypeDef& def) {
    if (def.lightRadius <= 0.0f) return;
    reg.emplace<ecs::LightEmitter>(
        e, ecs::LightEmitter{0.0f, def.lightHeight, 0.0f,
                             def.lightR, def.lightG, def.lightB,
                             def.lightRadius, def.lightIntensity});
}

void maybe_flip_temp_hostile(entt::registry& reg,
                             entt::entity target,
                             const GameState* gs,
                             const char* factionId) {
    if (!gs || !reg.valid(target) || !factionId || factionId[0] == '\0') {
        return;
    }
    if (reg.any_of<ecs::TempHostileToPlayer>(target)) return;
    if (player_reputation(gs, factionId) >= kAllyRepThreshold) return;

    reg.emplace_or_replace<ecs::TempHostileToPlayer>(target);
    if (auto* ai = reg.try_get<ecs::SubworldAi>(target)) {
        if (ai->kind == ecs::SubworldAi::Wander) {
            ai->kind = ecs::SubworldAi::Combat;
        }
    }
}

void apply_player_hit_reputation(entt::registry& reg,
                                 entt::entity target,
                                 GameState* gs) {
    if (!gs || !reg.valid(target)) return;
    if (hostile_to_player_entity(reg, target, gs)) return;

    const char* factionId = faction_id_for_kind(reg.try_get<ecs::NPCKind>(target));
    if (!factionId || factionId[0] == '\0') return;
    add_player_reputation(*gs, factionId, kHitRepPenalty);
    maybe_flip_temp_hostile(reg, target, gs, factionId);
}

void apply_player_kill_reputation(GameState* gs, const ecs::NPCKind* kind) {
    if (!gs) return;
    const char* factionId = faction_id_for_kind(kind);
    if (!factionId || factionId[0] == '\0') return;
    if (std::strcmp(factionId, "wildlife") == 0
        || std::strcmp(factionId, "demons") == 0
        || std::strcmp(factionId, "bandits") == 0) {
        return;
    }
    add_player_reputation(*gs, factionId, kKillRepPenalty);
}

const char* compass_from_delta(float dx, float dy) {
    const float ax = std::abs(dx);
    const float ay = std::abs(dy);
    if (ax < 0.35f && ay < 0.35f) return "here";
    if (ax > ay * 1.7f) return dx >= 0.0f ? "E" : "W";
    if (ay > ax * 1.7f) return dy >= 0.0f ? "N" : "S";
    if (dy >= 0.0f) return dx >= 0.0f ? "NE" : "NW";
    return dx >= 0.0f ? "SE" : "SW";
}

void spawn_npc_missile(entt::registry& reg,
                       entt::entity attacker,
                       const ecs::Position& origin,
                       const ecs::Combat& combat,
                       float targetX,
                       float targetY,
                       float targetZ) {
    const auto* missile = reg.try_get<ecs::MissileAttack>(attacker);
    const float speed = missile && missile->speed > 0.0f
        ? missile->speed
        : 200.0f;
    const float dx = targetX - origin.x;
    const float dy = targetY - origin.y;
    const float dz = targetZ - origin.z;
    const float dist3 = std::sqrt(dx * dx + dy * dy + dz * dz) + 0.0001f;
    const float nx = dx / dist3;
    const float ny = dy / dist3;
    const float nz = dz / dist3;
    const float attackerRadius = target_radius(reg, attacker);
    // Muzzle geometry mirrors the player's caster_spawn_offset(): spawn the
    // bolt fully clear of the caster's own hit shell so it can never strike
    // its owner on frame 1. Since Inc 4d removed the owner self-exclusion,
    // this offset is the ONLY thing keeping a caster off its own projectile —
    // the old attackerRadius+1.0 spawned INSIDE the 1.2 shell and leaned on
    // that (now-deleted) exclusion.
    const float projectileRadius = 1.2f;
    const float muzzle = attackerRadius + projectileRadius + 2.0f;
    const float sx = origin.x + nx * muzzle;
    const float sy = origin.y + ny * muzzle;
    const float sz = origin.z + nz * muzzle;
    const float life = std::max(0.5f, (combat.attackRange + 4.0f) / speed);
    const float blast = missile ? missile->blastRadius : 0.0f;
    const std::uint32_t color = missile ? missile->colorRGBA : 0xFFFFFFFFu;
    const std::uint8_t r = std::uint8_t((color >> 16) & 0xFFu);
    const std::uint8_t g = std::uint8_t((color >> 8) & 0xFFu);
    const std::uint8_t b = std::uint8_t(color & 0xFFu);
    const std::uint8_t a = std::uint8_t((color >> 24) & 0xFFu);

    entt::entity e = reg.create();
    reg.emplace<ecs::Position>(e, sx, sy, sz);
    reg.emplace<ecs::Projectile>(
        e,
        nx * speed, ny * speed, nz * speed,
        projectileRadius, life, life,
        combat.damage,
        blast,
        sx, sy,
        0.0f,
        0.0f,
        0.0f,
        kNpcMissileSpellId,
        std::uint32_t(entt::to_integral(attacker)),
        std::int16_t(0),
        ecs::Projectile::Bolt,
        false,
        false,
        false);
    reg.emplace<ecs::Sprite>(e, std::uint16_t(0x1FD), r, g, b,
                             a == 0 ? std::uint8_t(255) : a, 1.2f);
    reg.emplace<ecs::SubworldTag>(e);
    reg.emplace<ecs::Active>(e);
}

void clear_subworld_entities(ecs::World& w) {
    auto& reg = w.reg;
    std::array<entt::entity, kMaxSubworldEntityReaps> doomed{};
    for (;;) {
        int doomedCount = 0;
        auto view = reg.view<ecs::SubworldTag>();
        for (auto e : view) {
            if (doomedCount >= kMaxSubworldEntityReaps) break;
            doomed[std::size_t(doomedCount++)] = e;
        }
        if (doomedCount == 0) break;
        for (int i = 0; i < doomedCount; ++i) {
            const entt::entity e = doomed[std::size_t(i)];
            if (reg.valid(e)) reg.destroy(e);
        }
    }
}

} // namespace

// Universal signed stance — reuses the SAME anonymous-namespace helpers and
// thresholds the combat/AI paths use above, so a marker's colour cannot drift
// from real hostility. The two saturation ends mirror hostile_to_player_entity
// exactly (TempHostileToPlayer or reputation below kHostileThreshold read as
// fully hostile); the positive end mirrors the ally threshold; 0 reputation is
// dead-centre neutral. Each side is scaled independently by its own threshold
// so an asymmetric retune stays correct. Alive/scene filtering is the caller's
// job (collect_minimap_blips already iterates only live scene NPCs).
float player_stance(entt::registry& reg, entt::entity e, const GameState* gs) {
    if (is_player_side(reg, e)) return 1.0f;                    // own side
    if (reg.any_of<ecs::TempHostileToPlayer>(e)) return -1.0f;  // provoked
    const char* factionId = faction_id_for_kind(reg.try_get<ecs::NPCKind>(e));
    const int rep = player_reputation(gs, factionId);
    if (rep >= 0) {
        return std::min(1.0f, float(rep) / float(kAllyRepThreshold));
    }
    return std::max(-1.0f, float(rep) / float(-kHostileThreshold));
}

const std::vector<MinimapBlip>& SubworldEngine::collect_minimap_blips() const {
    minimapBlips_.clear();
    if (!ecs_) return minimapBlips_;
    entt::registry& reg = ecs_->reg;
    // Same candidate set as targeting/melee: live, current-scene NPCs/monsters.
    // The hero body carries no NPCKind, but a POSSESSED foreign body does (Inc
    // 5c), so exclude PlayerTag explicitly — the player is the map centre / its
    // own heading triangle, never a blip. Projected player soldiers keep their
    // NPCKind (and no PlayerTag) and read as fully allied (+1).
    auto view = reg.view<ecs::Position, ecs::Health, ecs::NPCKind,
                         ecs::SubworldTag>(entt::exclude<ecs::Dead, ecs::PlayerTag>);
    for (auto e : view) {
        if (view.get<ecs::Health>(e).hp <= 0.0f) continue;
        const auto& pos = view.get<ecs::Position>(e);
        minimapBlips_.push_back(
            MinimapBlip{pos.x, pos.y, player_stance(reg, e, gs_)});
    }
    return minimapBlips_;
}

float SubworldEngine::crosshair_stance() const {
    if (!ecs_ || !gs_) return std::numeric_limits<float>::quiet_NaN();
    entt::registry& reg = ecs_->reg;

    // Camera aim ray in 3D (tile/metre space — kTileMeters = 1).
    const float cp = std::cos(cam_.pitch);
    const float fx = std::cos(cam_.yaw) * cp;
    const float fy = std::sin(cam_.yaw) * cp;
    const float fz = std::sin(cam_.pitch);
    constexpr float kMaxRange = 200.0f;
    // Ray segment: player position → player + dir * kMaxRange.
    const float ax = playerX_, ay = playerY_, az = playerZ_;

    entt::entity best = entt::null;
    float bestT = kMaxRange;

    auto view = reg.view<ecs::Position, ecs::Health, ecs::NPCKind,
                         ecs::SubworldTag>(entt::exclude<ecs::Dead>);
    for (auto e : view) {
        if (reg.any_of<ecs::PlayerTag>(e)) continue;
        if (view.get<ecs::Health>(e).hp <= 0.0f) continue;
        const auto& pos = view.get<ecs::Position>(e);
        const float r = target_radius(reg, e);
        // Ray-sphere: project entity onto the aim segment, check distance.
        const float dx = pos.x - ax, dy = pos.y - ay, dz = pos.z - az;
        const float dot = dx * fx + dy * fy + dz * fz;
        if (dot < 0.0f || dot > kMaxRange) continue;
        const float d2 = dx * dx + dy * dy + dz * dz;
        const float perp2 = d2 - dot * dot;
        if (perp2 > r * r) continue;
        if (dot < bestT) {
            bestT = dot;
            best = e;
        }
    }
    if (best == entt::null) return std::numeric_limits<float>::quiet_NaN();
    return player_stance(reg, best, gs_);
}

void SubworldEngine::init(const gpu::VulkanDevice& dev, VkRenderPass mainPass) {
    if (inited_) return;
    dev_ = &dev;
    const bool trace = [] {
        const char* env = std::getenv("TIMAERT_BOOT_TRACE");
        return env && env[0] != '\0' && env[0] != '0';
    }();
    if (trace) { std::fprintf(stderr, "[boot] subworld renderer3dVk init start\n"); std::fflush(stderr); }
    renderer3dVk_.init(dev, mainPass);
    if (trace) { std::fprintf(stderr, "[boot] subworld renderer3dVk init done\n"); std::fflush(stderr); }
    inited_ = true;
}

void SubworldEngine::destroy(const gpu::VulkanDevice& dev) {
    if (!inited_) return;
    renderer3dVk_.destroy(dev);
    inited_ = false;
    dev_ = nullptr;
}

void SubworldEngine::enter(GameState& gs, const TerrainData& terrain,
                           const FeatureLayer& features, ecs::World& ecs,
                           EventBus& bus,
                           const ZoneLayer* zones,
                           TreeLayer* treeLayer) {
    statusLine_.clear();
    statusTimer_ = 0.0f;
    combatLogCount_ = 0;
    // A fresh session starts with no known threat: the exit gate can be asked
    // before the first combat tick runs.
    playerThreatD2_ = kNoThreatDistance2;
    if (!terrain.has_rgba_storage()) {
        gs_ = nullptr;
        terrain_ = nullptr;
        features_ = nullptr;
        ecs_ = nullptr;
        bus_ = nullptr;
        zones_ = nullptr;
        treeLayer_ = nullptr;
        active_ = false;
        pendingUpload3d_ = {};
        set_status("Subworld unavailable: invalid terrain");
        return;
    }

    gs_ = &gs; terrain_ = &terrain; features_ = &features;
    ecs_ = &ecs; bus_ = &bus; zones_ = zones; treeLayer_ = treeLayer;
    int cx = int(gs.player.x);
    int cy = int(gs.player.y);

    auto resolver = [this](int x, int y) { return resolve_context(x, y); };

    mgr_.init(cx, cy, resolver);
    // First upload is unconditionally full inside the renderer (device buffers
    // and images not yet created). Consume the manager's dirty (load_all marked
    // it full) and hand it straight to upload(), then clear the accumulator.
    const CompositeDirty enterDirty = mgr_.consume_composite_dirty_cells();
    if (dev_) renderer3dVk_.upload(*dev_, mgr_, enterDirty);
    active_  = true;
    pendingUpload3d_ = {};
    playerX_ = playerY_ = float(kFullSize / 2);
    playerAttackHeld_ = false;
    playerAttackTimer_ = 0.0f;
    flightCamY_ = 0.0f;
    spellRng_ = Rng{gs.worldSeed
        + std::uint32_t(cx) * std::uint32_t{1000}
        + std::uint32_t(cy)};

    // Fill all nine window cells from their own macro contexts (per-cell fauna
    // + settlement citizens), so the whole visible 3×3 is populated up front
    // and neighbouring cities are alive before you ever step toward them.
    spawn_all_cells();
    spawn_player_squad(ecs, gs.player.army, mgr_, playerX_, playerY_,
        gs.worldSeed ^ kSquadSpawnSalt ^ (std::uint32_t(cx) << 8) ^ std::uint32_t(cy));
    // Project the persistent macro NPCs standing in this 3×3 window into the
    // scene as real combat bodies (Inc 5d) — the overworld lords / bandits /
    // peasants are physically MET where they roam, and each projection carries a
    // MacroOrigin backlink so leave() (5e) can land the player on the right macro
    // cell. The macro entities stay authoritative and untouched (the macro tick
    // is frozen while a subworld is active). Runs AFTER the world fill and BEFORE
    // the player entity so projections are part of the scene the player enters.
    const int projected = project_macro_npcs_into_subworld(ecs, mgr_, cx, cy,
        gs.mapW, gs.mapH,
        gs.worldSeed ^ kMacroProjectionSalt ^ (std::uint32_t(cx) << 8)
            ^ std::uint32_t(cy));
    if (projected > 0) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "%d overworld figure%s nearby",
                      projected, projected == 1 ? "" : "s");
        set_status(msg);
    }
    // Materialise the player as a real ECS entity (the movable PlayerTag flag /
    // subworld sim-centre): a full combat actor (Health + BodyRadius + Combat +
    // SubworldTag) that hostiles target through the universal paths (Inc 4b).
    spawn_player_entity();
    if (gs_) {
        set_flying(spellbook_has_sustained(gs_->player.spellBook, "flight"));
    }
}

void SubworldEngine::sync_macro_player_to_center() {
    if (!gs_ || !terrain_ || terrain_->width <= 0 || terrain_->height <= 0) {
        return;
    }
    int nx = mgr_.center_cx() % terrain_->width;
    int ny = mgr_.center_cy() % terrain_->height;
    if (nx < 0) nx += terrain_->width;
    if (ny < 0) ny += terrain_->height;
    gs_->player.x = float(nx);
    gs_->player.y = float(ny);
}

entt::entity SubworldEngine::remap_macro_player_to_origin() {
    if (!gs_ || !ecs_ || !terrain_ || terrain_->width <= 0 || terrain_->height <= 0) {
        return entt::null;
    }
    // The body currently wearing the player flag (never null mid-subworld); the
    // pure query returns has == false for a normal un-possessed exit.
    const entt::entity body = current_player_body(*ecs_);
    const MacroExitCell cell =
        macro_exit_cell_for_body(*ecs_, body, terrain_->width, terrain_->height);
    if (!cell.has) return entt::null;
    gs_->player.x = float(cell.cx);
    gs_->player.y = float(cell.cy);
    return cell.macro;   // adopted by leave() as the persistent player (5e-2)
}

// ── Player entity (Inc 4b) ──────────────────────────────────────────────
//
// The player is a movable "flag" (`ecs::PlayerTag`) on a real ECS entity — the
// owner's §8 model where any NPC can receive the flag and the flagged entity is
// the subworld sim-centre. It is a FULL combat actor: Position + PlayerTag +
// Health + BodyRadius + Combat + SubworldTag. Because its signature now matches
// the combat/projectile views, hostiles melee it and spells strike it through
// exactly the same universal paths as any NPC — no player special-case in the
// sim. Its scalars are a transient projection of the macro-authoritative
// player: sync_player_entity_position pulls Position + Health in at each tick
// top, combat mutates Health in place, and reconcile_player_hp_to_macro pushes
// the result back onto combatStats.currentHp (which drives the death screen).
// Lifecycle is explicit and symmetric — spawn_player_entity() on enter,
// clear_player_entity() on leave — so exactly one PlayerTag entity is live while
// a subworld is active and none survives into the macro world. (The player
// carries SubworldTag, so the cell-crossing reapers that skip PlayerTag in
// spawn.cpp keep it across seams, while the leave-time clear_subworld_entities
// would also catch it; clear_player_entity remains the authoritative teardown.)
// Outgoing player damage is still input-driven (tick_player_melee) and the
// entity's Combat is inert until Inc 4c routes the player's own attacks here.
void SubworldEngine::clear_player_entity() {
    if (!ecs_) return;
    auto& reg = ecs_->reg;
    // Collect then destroy — never mutate the registry while iterating a view.
    // Normally there is exactly one PlayerTag entity; the small fixed cap is a
    // defensive backstop against a hypothetical leak, never expected to fill.
    std::array<entt::entity, 8> doomed{};
    int n = 0;
    for (auto e : reg.view<ecs::PlayerTag>()) {
        if (n >= int(doomed.size())) break;
        doomed[std::size_t(n++)] = e;
    }
    for (int i = 0; i < n; ++i) {
        const entt::entity e = doomed[std::size_t(i)];
        if (!reg.valid(e)) continue;
        // A possessed MACRO NPC (Inc 5e-2) wears the flag while the player walks
        // the overworld as that lord. Entering a subworld drops possession to the
        // hero, but the lord must SURVIVE as an autonomous NPC — so strip only the
        // flag (its AI resumes automatically), never destroy it. The hero husk and
        // every subworld body carry no MacroNpcRuntime, so those are still fully
        // destroyed exactly as before.
        if (reg.all_of<ecs::MacroNpcRuntime>(e)) reg.remove<ecs::PlayerTag>(e);
        else reg.destroy(e);
    }
}

void SubworldEngine::spawn_player_entity() {
    if (!ecs_) return;
    // Defensive: never leave a stale flag behind (e.g. an enter without a prior
    // leave). Exactly one PlayerTag entity must exist while a subworld is live.
    clear_player_entity();
    // Entering a subworld drops any macro-side possession (Inc 5e-2): the player
    // becomes the hero husk built below, not the lord it may have inhabited on the
    // overworld. clear_player_entity() just stripped the flag off that lord (it
    // survives as an autonomous NPC); clear the persisted ordinal too so a
    // mid-subworld save records the hero — matching what load will restore.
    if (gs_) gs_->player.possessedMacroSpawnId = -1;
    auto& reg = ecs_->reg;
    const entt::entity e = reg.create();
    reg.emplace<ecs::Position>(e, playerX_, playerY_, 0.0f);
    reg.emplace<ecs::PlayerTag>(e);
    // Inc 4b: the player is a full combat participant, not an inert anchor.
    //  - Health mirrors the authoritative macro scalar (combatStats.currentHp);
    //    sync_player_entity_position pulls it in at each tick top and
    //    reconcile_player_hp_to_macro pushes the post-combat result back out.
    //  - SubworldTag puts the entity in the combat actor set so hostiles pick
    //    it as a melee/projectile target through the SAME paths as any NPC.
    //  - BodyRadius gives it a sane hit size (it has no SubworldAi/Sprite to
    //    stand in), so melee reach and projectile contact against the player
    //    match a humanoid instead of the coarse target_radius() fallback.
    //  - Combat carries the player's OUTGOING melee identity (Inc 4c): the
    //    sheet-derived swing damage (10 + rawPhysDamage) plus the melee range /
    //    cooldown constants. tick_player_melee reads THIS component instead of
    //    recomputing from the sheet, and sync_player_entity_position refreshes
    //    the damage each tick so a mid-subworld level-up or gear change lands on
    //    the next swing. The NPC actor loop still never drives it: is_player_side
    //    makes the player non-hostile-to-itself, so it is skipped as an attacker
    //    there — the sole trigger stays the input-driven tick_player_melee.
    const int maxHp = gs_ ? std::max(1, gs_->player.combatStats.maxHp) : 1;
    const int curHp = gs_
        ? std::clamp(gs_->player.combatStats.currentHp, 0, maxHp)
        : maxHp;
    reg.emplace<ecs::Health>(e, ecs::Health{float(curHp), float(maxHp)});
    reg.emplace<ecs::BodyRadius>(e, ecs::BodyRadius{kPlayerBodyRadius});
    const float meleeDamage = gs_
        ? 10.0f + calculate_derived(gs_->player.sheet.attributes,
                                    gs_->player.sheet.skills).rawPhysDamage
        : 10.0f;
    reg.emplace<ecs::Combat>(
        e, ecs::Combat{meleeDamage, 0.0f, kPlayerMeleeRange,
                       kPlayerMeleeCooldown, 0.0f, ecs::Combat::Melee});
    reg.emplace<ecs::SubworldTag>(e);
    // First honest point-light emitter (Inc 4): a warm carried lantern. Gathered
    // by the renderer through the universal view<Position, LightEmitter,
    // SubworldTag>, so possessing another body (which moves PlayerTag but leaves
    // this hero husk's components) simply stops lighting from here and starts
    // from whatever the possessed body carries — no special-case anywhere.
    reg.emplace<ecs::LightEmitter>(
        e, ecs::LightEmitter{0.0f, kPlayerLightHeightM, 0.0f,
                             kPlayerLightR, kPlayerLightG, kPlayerLightB,
                             kPlayerLightRadiusM, kPlayerLightIntensity});
}

void SubworldEngine::pull_player_entity_to_scalars() {
    if (!ecs_) return;
    auto& reg = ecs_->reg;
    // Entity Position is authoritative (Inc 5a); copy it onto the scalar mirror.
    auto pv = reg.view<ecs::PlayerTag, ecs::Position>();
    for (auto e : pv) {
        const auto& p = pv.get<ecs::Position>(e);
        playerX_ = p.x;
        playerY_ = p.y;
        break; // exactly one PlayerTag flag is live at a time
    }
}

void SubworldEngine::push_scalars_to_player_entity() {
    if (!ecs_) return;
    auto& reg = ecs_->reg;
    // Fold a scalar change (a seam rewrap, or a move_player/set_player_pos edit)
    // back onto the authoritative entity Position. Assignment, so it is a no-op
    // when already equal and idempotent w.r.t. the seam rebase that likewise
    // shifts the SubworldTag-tagged player entity by the same ∓cell amount.
    auto pv = reg.view<ecs::PlayerTag, ecs::Position>();
    for (auto e : pv) {
        auto& p = pv.get<ecs::Position>(e);
        p.x = playerX_;
        p.y = playerY_;
        p.z = playerZ_;
        break;
    }
}

void SubworldEngine::sync_player_entity_position() {
    if (!ecs_) return;
    auto& reg = ecs_->reg;
    // Inc 5a: the player entity's Position is AUTHORITATIVE; propagate it onto the
    // scalar mirror that every legacy reader (camera / melee origin / proximity /
    // seam / HUD) still uses, so a possession that hopped the flag to a body at a
    // different Position is followed by all of them from the next tick.
    //
    // Inc 5c (D3 body-native): only the HERO body is macro-driven. The hero body
    // carries no NPCKind — that is the discriminator possess_entity maintains. For
    // it, HP stays MACRO-authoritative (pull combatStats -> Health here; combat
    // mutates it in place; reconcile pushes it back onto currentHp at tick end)
    // and outgoing melee damage tracks the sheet so a mid-subworld level-up / gear
    // change lands on the next swing. A POSSESSED foreign body (has NPCKind) is
    // left entirely alone here: it fights with its OWN Health + Combat, and
    // gs.player is frozen as the preserved revert target.
    auto pv = reg.view<ecs::PlayerTag, ecs::Position>();
    for (auto e : pv) {
        const auto& p = pv.get<ecs::Position>(e);
        playerX_ = p.x;
        playerY_ = p.y;
        playerZ_ = p.z;
        if (gs_ && !reg.all_of<ecs::NPCKind>(e)) {
            if (auto* h = reg.try_get<ecs::Health>(e)) {
                const int maxHp = std::max(1, gs_->player.combatStats.maxHp);
                h->maxHp = float(maxHp);
                h->hp = float(std::clamp(
                    gs_->player.combatStats.currentHp, 0, maxHp));
            }
            if (auto* c = reg.try_get<ecs::Combat>(e)) {
                c->damage = 10.0f + calculate_derived(
                    gs_->player.sheet.attributes,
                    gs_->player.sheet.skills).rawPhysDamage;
            }
        }
        break;
    }
}

void SubworldEngine::reconcile_player_hp_to_macro() {
    if (!ecs_ || !gs_) return;
    auto& reg = ecs_->reg;
    // Tick-end push: whatever damage the universal combat/projectile paths dealt
    // to the player entity's Health this tick is written back onto the macro
    // scalar (currentHp), which is what drives the death screen. Incoming-hit
    // feedback and godMode invulnerability are unified here — one place for both
    // melee and projectile damage, since both now mutate the same Health.
    // View includes Dead: a lethal hit must still reconcile currentHp to 0.
    auto pv = reg.view<ecs::PlayerTag, ecs::Health>();
    for (auto e : pv) {
        auto& h = pv.get<ecs::Health>(e);
        // Inc 5c (D3 body-native): a POSSESSED foreign body (has NPCKind) owns
        // its Health — do NOT reconcile it onto gs.player, which stays frozen as
        // the preserved revert target. The one thing that must still cross back
        // is death: if the body you inhabit dies, your consciousness dies with
        // it (game-over routed through the macro scalar, exactly like the hero).
        // godMode keeps the inhabited body on its feet.
        if (reg.all_of<ecs::NPCKind>(e)) {
            if (godMode_) {
                if (h.hp < 1.0f) h.hp = 1.0f;
                reg.remove<ecs::Dead>(e);
            } else if (h.hp <= 0.0f) {
                gs_->player.combatStats.currentHp = 0;
            }
            continue;
        }
        const int maxHp = std::max(1, gs_->player.combatStats.maxHp);
        if (godMode_) {
            // Invulnerable: undo any incoming damage applied this tick and keep
            // the entity out of the death path entirely.
            h.hp = float(std::clamp(gs_->player.combatStats.currentHp, 0, maxHp));
            reg.remove<ecs::Dead>(e);
            continue;
        }
        const int before = std::clamp(gs_->player.combatStats.currentHp, 0, maxHp);
        const int after = std::clamp(int(std::round(h.hp)), 0, maxHp);
        // Keep the Dead tag consistent with the reconciled scalar. A lethal hit
        // (after == 0) leaves it on: the entity drops out of every combat view,
        // matching the death screen. Any non-lethal outcome must clear a Dead
        // that a hypothetical over-damage-then-refresh ordering could otherwise
        // strand — a live-but-Dead player would be a zombie, silently excluded
        // from all incoming combat for the rest of the session.
        if (after > 0) reg.remove<ecs::Dead>(e);
        if (after < before) {
            const int dmg = before - after;
            // Label + compass from the LastHit any incoming path (melee strike
            // or spell/projectile) stamped on the player entity this tick.
            const char* label = "Hostile";
            float ax = playerX_;
            float ay = playerY_;
            if (const auto* lh = reg.try_get<ecs::LastHit>(e)) {
                const entt::entity atk = entt::entity(lh->attackerId);
                if (reg.valid(atk)) {
                    label = subworld_attacker_label(reg, atk);
                    if (const auto* ap = reg.try_get<ecs::Position>(atk)) {
                        ax = ap->x;
                        ay = ap->y;
                    }
                }
            }
            const bool lethal = after <= 0;
            char status[160]{};
            std::snprintf(status, sizeof(status), "Hit by %s for %d (%s %.0fm)",
                          label, dmg,
                          compass_from_delta(ax - playerX_, ay - playerY_),
                          std::sqrt(dist2(ax, ay, playerX_, playerY_)));
            set_status(status);
            char logMsg[96]{};
            std::snprintf(logMsg, sizeof(logMsg), "%s %s you for %d",
                          label, lethal ? "killed" : "hit", dmg);
            push_combat_log(logMsg);
        }
        gs_->player.combatStats.currentHp = after;
    }
}

CellContext SubworldEngine::resolve_context(int x, int y) const {
    CellContext c{};
    const int W = terrain_->width, H = terrain_->height;
    const int xi = ((x % W) + W) % W;
    const int yi = ((y % H) + H) % H;
    const std::size_t idx = std::size_t(yi) * W + xi;
    const float h = float(terrain_->rgba[idx * 4 + 0]) / 255.0f;
    const float m = float(terrain_->rgba[idx * 4 + 1]) / 255.0f;
    const float t = float(terrain_->rgba[idx * 4 + 2]) / 255.0f;
    const std::uint8_t mask = terrain_->rgba[idx * 4 + 3];
    c.cx = x; c.cy = y;
    c.macroHeight = h;
    // Season nudges ONLY the temperature that drives tree-species selection
    // (foliage shifts toward evergreen/autumn in the cold half of the year);
    // it is applied at this single source so both the CPU tree dispatch
    // (main.cpp) and the renderer's atlas bake (vk_renderer_3d.cpp) inherit it
    // for free. Biome CLASSIFICATION below keeps the raw climate `t` so a forest
    // never reclassifies to tundra in winter — only its trees change.
    const float seasonOffset = gs_ ? season_temp_offset(gs_->worldTime.day) : 0.0f;
    c.macroTemperature = std::clamp(t + seasonOffset, 0.0f, 1.0f);
    // Mask (land/water) drives Water; elevation drives Mountain; the climate
    // matrix fills in the land biomes. Mirrors biome_at, but keyed off the
    // subworld's authoritative land MASK rather than the sea-level threshold.
    c.biome   = !mask ? Biome::Water
              : (h >= kMountainBiomeLevel ? Biome::Mountain
                                          : biome_from_climate(t, m));
    c.feature = features_->at(xi, yi);
    // Macro tree count — the density target for the subworld scatter. -1
    // (no layer wired) lets dispatch re-derive from biome/features instead.
    c.treeCount = (treeLayer_ && treeLayer_->has_complete_storage())
        ? int(treeLayer_->at(xi, yi)) : -1;
    c.landmarkSettlementId = -1;
    c.landmarkSize = 0;
    c.landmarkKind = CellLandmarkKind::None;
    // Linear scan is fine — settlements are a small set (< 100) and resolve is
    // called O(9) times per enter / re-centre.
    for (const auto& s : gs_->settlements) {
        if (s.x == xi && s.y == yi) {
            c.landmarkSettlementId = s.id;
            c.landmarkSize = s.population;
            c.landmarkKind = CellLandmarkKind::City;
            break;
        }
    }
    if (c.landmarkSettlementId < 0) {
        for (const auto& v : gs_->villages) {
            if (v.x == xi && v.y == yi) {
                c.landmarkSettlementId = v.id;
                c.landmarkSize = v.population;
                c.landmarkKind = CellLandmarkKind::Village;
                break;
            }
        }
    }
    if (c.landmarkSettlementId < 0) {
        for (const auto& sp : gs_->spires) {
            if (sp.x == xi && sp.y == yi) {
                c.landmarkSettlementId = sp.id;
                c.landmarkSize = 0;
                c.landmarkKind = CellLandmarkKind::Spire;
                break;
            }
        }
    }
    c.seed = gs_->worldSeed
           ^ (std::uint32_t(xi) * kCellSeedX)
           ^ (std::uint32_t(yi) * kCellSeedY);
    return c;
}

namespace {
// Map the macro cell's landmark tag to the spawn table's LandmarkKind. A bare
// settlement id with no explicit kind is treated as a city (matches the old
// centre-only path). Kept local — the only consumer is spawn_cell below.
LandmarkKind to_landmark_kind(const CellContext& c) {
    switch (c.landmarkKind) {
        case CellLandmarkKind::City:    return LandmarkKind::City;
        case CellLandmarkKind::Village: return LandmarkKind::Village;
        case CellLandmarkKind::Ruin:    return LandmarkKind::Ruin;
        case CellLandmarkKind::Spire:   return LandmarkKind::Spire;
        case CellLandmarkKind::None:    break;
    }
    return c.landmarkSettlementId >= 0 ? LandmarkKind::City : LandmarkKind::None;
}
} // namespace

// Populate ONE window cell (offset ox,oy ∈ {-1,0,1} from centre) from that
// cell's ABSOLUTE macro context — biome/feature/landmark/pop/zone/seed all
// resolved at its true macro coordinate. This is why an off-centre city fills
// with citizens (it no longer depends on being the centre cell), and why the
// procedural fauna is deterministic per cell (seeded from ctx.seed).
void SubworldEngine::spawn_cell(int ox, int oy) {
    if (!ecs_ || !gs_ || !terrain_ || terrain_->width <= 0
        || terrain_->height <= 0) {
        return;
    }
    const int ccx = mgr_.center_cx() + ox;
    const int ccy = mgr_.center_cy() + oy;
    const CellContext ctx = resolve_context(ccx, ccy);
    const int W = terrain_->width, H = terrain_->height;
    const int wcx = ((ccx % W) + W) % W;
    const int wcy = ((ccy % H) + H) % H;
    const int zoneLevel = (zones_ && !zones_->data.empty())
        ? int(zones_->at(wcx, wcy)) : 0;
    spawn_cell_npcs(*ecs_, ctx.biome, ctx.treeCount, to_landmark_kind(ctx), mgr_,
                    ox, oy, ctx.seed, ctx.landmarkSize, zoneLevel);
}

// Clean fill of all nine window cells — enter() / fresh scene. The player's
// projected squad + player entity are preserved by the clear step's PlayerTag /
// PlayerSoldierTag skip, so they are not wiped here.
void SubworldEngine::spawn_all_cells() {
    if (!ecs_) return;
    clear_subworld_world_entities(*ecs_);
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox)
            spawn_cell(ox, oy);
}

// Seam re-centre by (dx,dy) macro cells: slide every subworld entity (and the
// player squad) by -(dx,dy)·kCellSize so fixed physical content tracks the new
// window, evict whatever now lies outside the 3×3, then spawn ONLY the cells
// newly brought into view. The overlap (6/9 on an axis step, 4/9 diagonal) is
// carried across untouched — no clear-and-respawn, so nobody in the current 3×3
// vanishes; only the far cells that actually left do.
void SubworldEngine::repopulate_after_recenter(int dx, int dy) {
    if (!ecs_) return;
    rebase_subworld_entities(*ecs_,
        float(-dx * kCellSize), float(-dy * kCellSize));
    despawn_subworld_entities_outside_window(*ecs_);
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int oldOx = ox + dx;
            const int oldOy = oy + dy;
            const bool wasInWindow =
                oldOx >= -1 && oldOx <= 1 && oldOy >= -1 && oldOy <= 1;
            if (!wasInWindow) spawn_cell(ox, oy);
        }
    }
}

void SubworldEngine::set_status(const char* msg) {
    statusLine_ = msg ? msg : "";
    statusTimer_ = statusLine_.empty() ? 0.0f : 2.5f;
}

const CombatLogEntry* SubworldEngine::combat_log_entry(int index) const {
    if (index < 0 || index >= combatLogCount_) return nullptr;
    return &combatLog_[std::size_t(index)];
}

void SubworldEngine::push_combat_log(const char* msg) {
    if (!msg || msg[0] == '\0') return;
    int dst = combatLogCount_;
    if (combatLogCount_ < kCombatLogLimit) {
        ++combatLogCount_;
    } else {
        for (int i = 1; i < kCombatLogLimit; ++i) {
            combatLog_[std::size_t(i - 1)] = combatLog_[std::size_t(i)];
        }
        dst = kCombatLogLimit - 1;
    }
    std::snprintf(combatLog_[std::size_t(dst)].text,
                  sizeof(combatLog_[std::size_t(dst)].text),
                  "%s", msg);
    combatLog_[std::size_t(dst)].age = 0.0f;
}

void SubworldEngine::push_player_hit_log(std::uint32_t targetEntityId,
                                         float damage,
                                         bool lethal) {
    if (!ecs_ || damage <= 0.0f) return;
    entt::entity target = entt::entity(targetEntityId);
    if (!ecs_->reg.valid(target)) return;
    apply_player_hit_reputation(ecs_->reg, target, gs_);

    const int dmg = std::max(0, int(std::round(damage)));
    char msg[96]{};
    std::snprintf(msg, sizeof(msg), "You %s %s for %d",
                  lethal ? "killed" : "hit",
                  subworld_attacker_label(ecs_->reg, target),
                  dmg);
    push_combat_log(msg);
}

void SubworldEngine::spell_damage_log_callback(void* user,
                                               std::uint32_t targetEntityId,
                                               float damage,
                                               bool lethal) {
    auto* engine = static_cast<SubworldEngine*>(user);
    if (!engine) return;
    engine->push_player_hit_log(targetEntityId, damage, lethal);
}

bool SubworldEngine::spell_can_hit_callback(void* user,
                                            const ecs::Projectile& projectile,
                                            std::uint32_t targetEntityId) {
    // Projectiles are physics — they hit whatever body they intersect.
    // Faction/hostility only determines consequences (reputation, aggro),
    // handled by the damage log callback, not here.
    (void)projectile;
    auto* engine = static_cast<SubworldEngine*>(user);
    if (!engine || !engine->ecs_) return true;
    auto& reg = engine->ecs_->reg;
    const entt::entity target = entt::entity(targetEntityId);
    return reg.valid(target);
}

float SubworldEngine::spell_height_callback(void* user, float x, float y) {
    auto* self = static_cast<SubworldEngine*>(user);
    return self->renderer3dVk_.sample_height_m(x, y);
}

void SubworldEngine::spell_fx_emit_callback(void* user,
                                            SpellFxEvent event,
                                            std::uint32_t entity,
                                            float ax, float ay, float az,
                                            float bx, float by, float bz,
                                            float blastRadius) {
    auto* engine = static_cast<SubworldEngine*>(user);
    if (!engine || !engine->ecs_) return;
    auto& reg = engine->ecs_->reg;

    // Colour every VFX from the bolt's OWN Sprite tint — the universal rule: a
    // bolt's colour drives both its trail and its impact (fireball warm, magic
    // violet, lightning gold, ice pale) with zero per-spell branching. u8→linear
    // normalised so the brightest channel is 1.0, exactly like the travelling
    // point light bolt_light() (registry.cpp) so wake, glow and burst share one
    // hue. No Sprite (e.g. a bare meteor) ⇒ fall back to the preset colour.
    vec3 tint{};
    bool haveTint = false;
    const entt::entity e = entt::entity(entity);
    if (reg.valid(e)) {
        if (const auto* spr = reg.try_get<ecs::Sprite>(e)) {
            const float rf = float(spr->r);
            const float gf = float(spr->g);
            const float bf = float(spr->b);
            const float peak = std::max(rf, std::max(gf, bf));
            const float inv = peak > 1.0f ? 1.0f / peak : 1.0f;
            tint = {rf * inv, gf * inv, bf * inv};
            haveTint = true;
        }
    }
    const vec3* tintPtr = haveTint ? &tint : nullptr;

    if (event == SpellFxEvent::Trail) {
        float wax = 0.0f, waz = 0.0f, wbx = 0.0f, wbz = 0.0f;
        Renderer3DVk::tile_to_world(ax, ay, wax, waz);
        Renderer3DVk::tile_to_world(bx, by, wbx, wbz);
        // az/bz are the projectile's actual world-space altitude (metres) —
        // use directly instead of sampling terrain height.
        const float ya = az;
        const float yb = bz;
        // One mote per 1.5 m travelled ⇒ a continuous glowing wake at any bolt
        // speed (distance-based, so 280 u/s and 400 u/s read the same density).
        constexpr float kTrailSpacingM = 1.5f;
        engine->particles_.emit_streak(FxKind::SpellTrail,
                                       vec3{wax, ya, waz}, vec3{wbx, yb, wbz},
                                       kTrailSpacingM, tintPtr);
        return;
    }

    // Impact: pick the burst archetype from the ONE physical field the bolt
    // carries — a positive blast radius means an explosive bloom (FireBurst),
    // otherwise a crisp arcane point-pop (MagicBurst). Scale grows (gently,
    // capped) with the blast so a fat fireball erupts bigger than a small charge.
    float wx = 0.0f, wz = 0.0f;
    Renderer3DVk::tile_to_world(bx, by, wx, wz);
    const float y = bz; // actual projectile altitude
    if (blastRadius > 0.0f) {
        const float scale = std::clamp(1.0f + blastRadius * 0.04f, 1.0f, 3.0f);
        engine->particles_.emit(FxKind::FireBurst, vec3{wx, y, wz},
                                tintPtr, scale);
    } else {
        engine->particles_.emit(FxKind::MagicBurst, vec3{wx, y, wz},
                                tintPtr, 1.0f);
    }
}

std::uint32_t SubworldEngine::player_entity_id() const {
    if (ecs_) {
        for (auto e : ecs_->reg.view<ecs::PlayerTag>()) {
            return std::uint32_t(entt::to_integral(e));
        }
    }
    return std::uint32_t(
        entt::to_integral(static_cast<entt::entity>(entt::null)));
}

bool SubworldEngine::possess_aim(float cosHalfAngle, float maxRange) {
    if (!active_ || !ecs_) return false;
    auto& reg = ecs_->reg;
    // The current body is excluded as an aim candidate (a shooter never targets
    // itself); aim_target already skips every player-side entity too.
    const entt::entity self = current_player_body(*ecs_);
    const entt::entity target = aim_target(reg, playerX_, playerY_, cam_.yaw,
                                           maxRange, cosHalfAngle, self);
    if (target == entt::null) {
        set_status("Nothing in reach to possess");
        return false;
    }
    if (!possess_entity(*ecs_, target)) return false;
    // The flag now rides the new body; mirror its Position onto the scalars so
    // the camera, seam, melee origin, and HUD snap to it this very frame.
    pull_player_entity_to_scalars();
    char msg[128]{};
    std::snprintf(msg, sizeof(msg), "Possessed %s",
                  subworld_attacker_label(reg, target));
    set_status(msg);
    return true;
}

bool SubworldEngine::possess_by_id(std::uint32_t entityId) {
    if (!active_ || !ecs_) return false;
    if (!possess_entity(*ecs_, entt::entity(entityId))) return false;
    pull_player_entity_to_scalars();
    set_status("Possessed (by id)");
    return true;
}

int SubworldEngine::player_display_hp() const {
    if (ecs_) {
        // The flagged body's Health IS the display truth: for the hero it mirrors
        // combatStats (kept in sync each tick), for a possessed foreign body it is
        // the body's own pool — so the HUD/flash follows possession with no
        // gs.player mutation (D3 keeps gs.player frozen as the revert target).
        for (auto e : ecs_->reg.view<ecs::PlayerTag, ecs::Health>()) {
            return int(std::round(ecs_->reg.get<ecs::Health>(e).hp));
        }
    }
    return gs_ ? gs_->player.combatStats.currentHp : 0;
}

bool SubworldEngine::player_threat_callback(void* user,
                                            std::uint32_t entityId) {
    auto* engine = static_cast<SubworldEngine*>(user);
    if (!engine || !engine->ecs_ || !engine->gs_) return true;
    auto& reg = engine->ecs_->reg;
    const entt::entity e = entt::entity(entityId);
    if (!reg.valid(e)) return false;
    return hostile_to_player_entity(reg, e, engine->gs_);
}

void SubworldEngine::tick_player_melee(float dt) {
    if (!ecs_ || !gs_ || dt <= 0.0f) return;
    if (gs_->player.combatStats.currentHp <= 0) return;

    playerAttackTimer_ -= dt;
    if (!playerAttackHeld_ || playerAttackTimer_ > 0.0f) return;

    auto& reg = ecs_->reg;
    // Inc 4c: the player's outgoing melee identity lives on its ECS Combat
    // (damage/range/cooldown), refreshed from the sheet by
    // sync_player_entity_position — read it here instead of recomputing. Capture
    // the scalars up front so later component emplaces can't dangle the pointer.
    const ecs::Combat* pc = nullptr;
    for (auto pe : reg.view<ecs::PlayerTag, ecs::Combat>()) {
        pc = &reg.get<ecs::Combat>(pe);
        break;
    }
    if (!pc) return;
    const float meleeDamage = std::floor(pc->damage);
    const float meleeCooldown = pc->cooldown;
    const float range2 = pc->attackRange * pc->attackRange;
    entt::entity target = entt::null;
    float bestD2 = range2;
    auto view = reg.view<ecs::Position, ecs::Health, ecs::NPCKind,
                         ecs::SubworldTag>(entt::exclude<ecs::Dead>);
    for (auto e : view) {
        // Skip the whole player side. Inc 5c: a possessed foreign body carries
        // NPCKind + PlayerTag, so it enters this view — is_player_side keeps the
        // player from meleeing the very body they inhabit (PlayerSoldierTag alone
        // would have missed it).
        if (is_player_side(reg, e)) continue;
        const auto& hp = view.get<ecs::Health>(e);
        if (hp.hp <= 0.0f) continue;
        const auto& pos = view.get<ecs::Position>(e);
        const float d2 = dist3sq(pos.x, pos.y, pos.z, playerX_, playerY_, playerZ_);
        if (d2 <= bestD2) {
            bestD2 = d2;
            target = e;
        }
    }
    if (target == entt::null) {
        // No creature in reach — the same swing can fell a tree instead
        // (minimal wood-cutting; the +1.5 covers the trunk radius the melee
        // point-range does not model).
        if (fell_tree_near_player(pc->attackRange + 1.5f))
            playerAttackTimer_ = meleeCooldown;
        return;
    }

    auto* hp = reg.try_get<ecs::Health>(target);
    if (!hp || hp->hp <= 0.0f) return;
    const float damage = meleeDamage;
    const bool lethal = hp->hp > 0.0f && hp->hp - damage <= 0.0f;
    hp->hp -= damage;
    reg.emplace_or_replace<ecs::LastHit>(
        target, std::uint32_t{0}, true);
    reg.emplace_or_replace<ecs::HitFlash>(
        target, ecs::HitFlash{kHitFlashDuration});
    // Universal impact-VFX marker (Inc C): drained by tick_damage_fx into a
    // blood/dust burst this same tick. One line per damage site; no particle
    // types here.
    reg.emplace_or_replace<ecs::DamageFx>(target, ecs::DamageFx{lethal});
    push_player_hit_log(std::uint32_t(entt::to_integral(target)),
                        damage, lethal);
    const char* label = subworld_attacker_label(reg, target);
    char status[96]{};
    std::snprintf(status, sizeof(status), "You %s %s for %d",
                  lethal ? "killed" : "hit",
                  label,
                  std::max(0, int(std::round(damage))));
    set_status(status);
    playerAttackTimer_ = meleeCooldown;

    if (hp->hp <= 0.0f && !reg.any_of<ecs::Dead>(target)) {
        reg.emplace<ecs::Dead>(target);
        if (bus_) {
            GameEvent ev{EventTag::NpcDeath};
            ev.a = std::uint32_t(entt::to_integral(target));
            ev.b = 0u;
            if (const auto* kind = reg.try_get<ecs::NPCKind>(target)) {
                ev.ix = int(kind->type);
            }
            bus_->emit(ev);
        }
    }
}

bool SubworldEngine::fell_tree_near_player(float maxDist,
                                           int* outCellX, int* outCellY,
                                           int* outPrevCount) {
    if (!active_ || !gs_) return false;
    int mcx = 0, mcy = 0;
    if (!mgr_.fell_tree_near(playerX_, playerY_, maxDist, mcx, mcy))
        return false;
    // The macro writeback: one tree gone in the subworld = one count off the
    // owning macro cell. Recorded in gs.treeOverrides so it survives
    // save/load and thins the map sprite (TreeLayer.revision drives the
    // u_treeMap refresh).
    int prev = 0;
    if (treeLayer_) {
        prev = int(treeLayer_->at(mcx, mcy));
        set_tree_count(*treeLayer_, gs_->treeOverrides, mcx, mcy, prev - 1);
    }
    if (outCellX) *outCellX = mcx;
    if (outCellY) *outCellY = mcy;
    if (outPrevCount) *outPrevCount = prev;
    set_status("You fell a tree");
    return true;
}

void SubworldEngine::tick_damage_fx() {
    if (!ecs_) return;
    auto& reg = ecs_->reg;
    // Height above ground to seat the spray at roughly mid-body, so droplets arc
    // out from the torso rather than the feet. One constant, not per-creature —
    // the archetype only chooses blood vs dust, never geometry.
    constexpr float kSprayHeightM = 1.1f;
    std::array<entt::entity, kMaxSubworldEntityReaps> consumed{};
    int consumedCount = 0;
    auto view = reg.view<ecs::DamageFx, ecs::Position>();
    for (auto e : view) {
        if (consumedCount < kMaxSubworldEntityReaps) {
            consumed[std::size_t(consumedCount++)] = e;
        }
        // The player body's damage feedback is the HUD hit-flash; a world burst
        // would spawn on the camera and clip the near plane. Skip it (still
        // consumed below so the tag never lingers).
        if (reg.any_of<ecs::PlayerTag>(e)) continue;
        const auto& fx = view.get<ecs::DamageFx>(e);
        const auto& pos = view.get<ecs::Position>(e);

        // Blood by default (flesh); dust for the bloodless body plans — bony /
        // ghostly Undead and stone Hulk — read purely from the victim's own
        // sprite archetype, so a skeleton puffs grey and a wolf sprays red with
        // zero creature-specific branching. No Sprite (a plain NPC paper-doll,
        // archetype 0xFF) is flesh ⇒ blood.
        FxKind kind = FxKind::Blood;
        if (const auto* spr = reg.try_get<ecs::Sprite>(e)) {
            const auto arch = static_cast<CreatureArchetype>(spr->archetype);
            if (arch == CreatureArchetype::Undead
                || arch == CreatureArchetype::Hulk) {
                kind = FxKind::Dust;
            }
        }
        // A killing blow throws a bigger, more emphatic burst than a glancing
        // hit — the one gameplay fact (lethal) scales the spray, no new data.
        const float scale = fx.lethal ? 1.8f : 1.0f;

        float wx = 0.0f, wz = 0.0f;
        Renderer3DVk::tile_to_world(pos.x, pos.y, wx, wz);
        const float y = pos.z + kSprayHeightM;
        particles_.emit(kind, vec3{wx, y, wz}, nullptr, scale);
    }
    for (int i = 0; i < consumedCount; ++i) {
        const entt::entity e = consumed[std::size_t(i)];
        if (reg.valid(e) && reg.all_of<ecs::DamageFx>(e)) {
            reg.remove<ecs::DamageFx>(e);
        }
    }
}

void SubworldEngine::tick_hit_flashes(float dt) {
    if (!ecs_ || dt <= 0.0f) return;
    auto& reg = ecs_->reg;
    std::array<entt::entity, kMaxSubworldEntityReaps> expired{};
    int expiredCount = 0;
    auto view = reg.view<ecs::HitFlash>();
    for (auto e : view) {
        auto& flash = view.get<ecs::HitFlash>(e);
        flash.timer -= dt;
        if (flash.timer <= 0.0f && expiredCount < kMaxSubworldEntityReaps) {
            expired[std::size_t(expiredCount++)] = e;
        }
    }
    for (int i = 0; i < expiredCount; ++i) {
        const entt::entity e = expired[std::size_t(i)];
        if (reg.valid(e) && reg.all_of<ecs::HitFlash>(e)) {
            reg.remove<ecs::HitFlash>(e);
        }
    }
}

// Both of these answer "how close is the nearest body hostile to the player",
// and BOTH are read every frame (the HUD danger gem, the exit gate). They used
// to walk every subworld entity calling hostile_to_player_entity — two
// std::map<std::string> lookups apiece, i.e. ~32k map lookups per frame at the
// 16k-body ceiling, to colour one gem. The battle pass already knows the answer:
// it holds a packed SoA whose enemy masks encode exactly the same rule
// (reputation, plus TempHostileToPlayer per body), so the distance is cached
// there once per tick and both queries are now O(1) reads.
bool SubworldEngine::has_hostile_near_player(float radius) const {
    return playerThreatD2_ <= radius * radius;
}

DangerLevel SubworldEngine::danger_level() const {
    if (!active_ || !ecs_) return DangerLevel::Green;
    constexpr float kMeleeRange = 40.0f;
    constexpr float kMeleeRange2 = kMeleeRange * kMeleeRange;
    const float detection2 = kDetectionRadius * kDetectionRadius;
    if (playerThreatD2_ > detection2) return DangerLevel::Green;
    return playerThreatD2_ <= kMeleeRange2 ? DangerLevel::Red
                                           : DangerLevel::Yellow;
}

bool SubworldEngine::exit_blocked_by_danger() const {
    if (!active_ || !zones_ || zones_->data.empty()) return false;
    const int zoneLevel = int(zones_->at(mgr_.center_cx(), mgr_.center_cy()));
    if (zoneLevel <= 2) return false;
    return has_hostile_near_player(kDetectionRadius);
}

bool SubworldEngine::interact() {
    if (!active_ || !ecs_ || !gs_) return false;
    auto& reg = ecs_->reg;
    auto view = reg.view<ecs::Position, ecs::Structure, ecs::CorpseLoot,
                         ecs::SubworldTag>();
    entt::entity best = entt::null;
    float bestD2 = 12.0f * 12.0f;
    for (auto e : view) {
        const auto& st = view.get<ecs::Structure>(e);
        if (st.kind != ecs::Structure::Corpse) continue;
        const auto& p = view.get<ecs::Position>(e);
        const float d2 = dist3sq(p.x, p.y, p.z, playerX_, playerY_, playerZ_);
        if (d2 <= bestD2) {
            bestD2 = d2;
            best = e;
        }
    }
    if (best == entt::null) {
        set_status("No corpse loot nearby.");
        return false;
    }

    auto& loot = reg.get<ecs::CorpseLoot>(best);
    gs_->player.gold += loot.gold;
    for (const ItemStack& s : loot.inv.stacks) {
        gs_->player.inventory.add(s.id, s.count);
    }
    reg.destroy(best);
    set_status("Loot recovered.");
    return true;
}

bool SubworldEngine::spawn_hostile_npc(const char* npcTypeId,
                                       const char* displayName,
                                       int level,
                                       std::uint32_t seed,
                                       const char* factionId,
                                       const ecs::NpcInventory* inventoryOverride,
                                       const ecs::NpcTraits* traitsOverride,
                                       const ecs::NpcCharacter* characterOverride,
                                       const float* positionOverride) {
    if (!active_ || !ecs_) return false;

    auto& reg = ecs_->reg;
    const NPCType type = npc_type_from_token(npcTypeId);
    const NpcTypeDef& def = npc_def(type);
    const int lvl = normalize_soldier_level(std::max(level, def.baseLevel));

    Rng rng(seed ^ string_hash(npcTypeId) ^ string_hash(displayName));
    const auto& tiles = mgr_.tiles();
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);
    float fx = playerX_;
    float fy = playerY_;
    bool placed = positionOverride != nullptr;
    if (placed) {
        fx = std::clamp(positionOverride[0], 1.0f, float(kFullSize - 2));
        fy = std::clamp(positionOverride[1], 1.0f, float(kFullSize - 2));
    }
    for (int attempt = 0; attempt < 24 && !placed; ++attempt) {
        const float angle = rng.next_f01() * 6.2831853f;
        const float radius = 18.0f + rng.next_f01() * 16.0f;
        fx = std::clamp(playerX_ + std::cos(angle) * radius,
                        1.0f, float(kFullSize - 2));
        fy = std::clamp(playerY_ + std::sin(angle) * radius,
                        1.0f, float(kFullSize - 2));
        const int ix = int(fx);
        const int iy = int(fy);
        if (tilesUsable && tiles[std::size_t(iy) * kFullSize + ix] == TILE_WATER) {
            continue;
        }
        placed = true;
        break;
    }
    if (!placed) {
        fx = std::clamp(playerX_ + 12.0f, 1.0f, float(kFullSize - 2));
        fy = playerY_;
    }

    // ── Monster branch ──────────────────────────────────────────
    // The token names a creature in the global monster table (fauna.h). A
    // monster is a SEPARATE branch from an NPC: it gets no character sheet /
    // traits, and its Sprite carries the procedural archetype. This is the same
    // component layout the ambient fauna populator emplaces (spawn.cpp), so a
    // console `spawn wolf` and a biome-spawned wolf are the same entity. The
    // death path rolls loot from the creature's lootId, so no bag is pre-rolled
    // here (unless the caller supplied an explicit inventoryOverride).
    if (const FaunaEntry* cd = creature_def(npcTypeId)) {
        const int clvl = normalize_soldier_level(std::max(level, int(cd->baseLevel)));
        const float scale = 1.0f + float(std::max(0, clvl - 1)) * 0.15f;
        const int catIdx = creature_index(cd);
        const std::uint16_t typeId =
            std::uint16_t(0x100 | (catIdx < 0 ? 0 : catIdx));

        auto e = reg.create();
        reg.emplace<ecs::Position>(e, fx, fy, 0.0f);
        reg.emplace<ecs::VisualPos>(e, fx, fy, 32.0f);
        // uint16(-1) == kNoFaction: an unregistered/absent creature faction is
        // honestly factionless, not accidentally faction 0.
        reg.emplace<ecs::NPCKind>(
            e, typeId, std::uint16_t(faction_index(cd->factionId)));
        const float hp = std::floor(float(cd->combat.hp) * scale);
        reg.emplace<ecs::Health>(e, hp, hp);
        reg.emplace<ecs::Combat>(e,
            std::floor(float(cd->combat.damage) * scale),
            cd->combat.speed, cd->combat.attackRange, cd->combat.cooldown, 0.0f,
            cd->combat.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                             : ecs::Combat::Melee);
        maybe_emplace_missile_attack(reg, e, cd->combat);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(clvl));
        reg.emplace<ecs::Active>(e);
        reg.emplace<ecs::SubworldTag>(e);
        const ecs::SubworldAi::Kind aiKind =
            cd->ai == FaunaAi::Combat ? ecs::SubworldAi::Combat
          : cd->ai == FaunaAi::Flee   ? ecs::SubworldAi::Flee
                                      : ecs::SubworldAi::Wander;
        reg.emplace<ecs::SubworldAi>(e, aiKind, /*aiTimer*/0.0f,
            /*vx*/0.0f, /*vy*/0.0f,
            /*wanderSpeed*/cd->combat.speed * 0.40f, cd->radius);
        if (inventoryOverride) {
            ecs::NpcInventory bag = *inventoryOverride;
            reg.emplace<ecs::NpcInventory>(e, std::move(bag));
        }
        const std::uint8_t cr = std::uint8_t((cd->color >> 16) & 0xFFu);
        const std::uint8_t cg = std::uint8_t((cd->color >>  8) & 0xFFu);
        const std::uint8_t cb = std::uint8_t( cd->color        & 0xFFu);
        reg.emplace<ecs::Sprite>(e, typeId, cr, cg, cb, std::uint8_t(255),
                                 cd->radius, std::uint8_t(cd->archetype));

        char msg[160]{};
        std::snprintf(msg, sizeof(msg), "Encounter spawned: %s",
                      displayName && displayName[0] ? displayName : cd->label);
        set_status(msg);
        return true;
    }

    auto e = reg.create();
    reg.emplace<ecs::Position>(e, fx, fy, 0.0f);
    reg.emplace<ecs::VisualPos>(e, fx, fy, 48.0f);
    reg.emplace<ecs::NPCKind>(
        e, ecs::NPCKind{std::uint16_t(type),
                        std::uint16_t(faction_index(factionId))});
    // Universal character sheet — combat is DERIVED from it (project_combat), so
    // level scaling lives in the sheet's spent points, not a multiplier. The
    // position-mixed seed keeps co-spawned hostiles distinct at one call site.
    const CharacterSheet sheet = make_character_sheet(
        type, lvl, seed ^ (std::uint32_t(int(fx)) * 73856093u)
                        ^ (std::uint32_t(int(fy)) * 19349663u));
    const CombatTemplate pc = project_combat(sheet, def.combat);
    const float hp = pc.hp;
    reg.emplace<ecs::Health>(e, hp, hp);
    reg.emplace<ecs::Combat>(e,
        pc.damage,
        pc.speed,
        pc.attackRange,
        pc.cooldown,
        0.0f,
        pc.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                 : ecs::Combat::Melee);
    maybe_emplace_missile_attack(reg, e, pc);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(lvl));
    reg.emplace<ecs::Active>(e);
    reg.emplace<ecs::SubworldTag>(e);
    reg.emplace<ecs::SubworldAi>(e, ecs::SubworldAi::Combat,
        0.0f, 0.0f, 0.0f, pc.speed * 0.40f, 0.8f);
    reg.emplace<CharacterSheet>(e, sheet);

    if (inventoryOverride) {
        ecs::NpcInventory bag = *inventoryOverride;
        reg.emplace<ecs::NpcInventory>(e, std::move(bag));
    } else {
        ecs::NpcInventory bag{};
        gLootRng = &rng;
        auto stacks = generate_npc_inventory(int(type), lvl, &loot_rng_f01);
        gLootRng = nullptr;
        for (const ItemStack& s : stacks) bag.inv.add(s.id, s.count);
        reg.emplace<ecs::NpcInventory>(e, std::move(bag));
    }
    if (traitsOverride && traitsOverride->count > 0) {
        const ecs::NpcTraits traits = *traitsOverride;
        reg.emplace<ecs::NpcTraits>(e, traits);
    }
    if (characterOverride) {
        const ecs::NpcCharacter ch = *characterOverride;
        reg.emplace<ecs::NpcCharacter>(e, ch);
    } else {
        reg.emplace<ecs::NpcCharacter>(
            e, make_visual_character(seed, type, displayName));
    }
    reg.emplace<ecs::Sprite>(e, std::uint16_t(type),
        std::uint8_t(220), std::uint8_t(80), std::uint8_t(70),
        std::uint8_t(255), 0.8f);
    maybe_emplace_carried_light(reg, e, def);

    char msg[160]{};
    std::snprintf(msg, sizeof(msg), "Encounter spawned: %s",
                  displayName && displayName[0] ? displayName : def.label);
    set_status(msg);
    return true;
}

// Terrain hook for the battle pass: forwards to the renderer's CPU heightfield.
// A free function + void* user, not a virtual — sub/battle.h stays Vulkan-free.
float SubworldEngine::battle_height_callback(void* user, float x, float y) {
    auto* self = static_cast<SubworldEngine*>(user);
    return self->renderer3dVk_.sample_height_m(x, y);
}

// THE relation rule, and the only place it lives: one function that answers
// "how do these two faction ids regard each other" for any pair the battle pass
// interns. Faction-vs-faction reads the macro relation matrix; anything paired
// with the player side reads the player's reputation with the other id, which is
// how this game has always modelled the player's standing. The player is not a
// special case in the algorithm — only a row with a different lookup.
//
// Passed as a plain function pointer so the pair loop itself can live in the
// Vulkan-free, ECS-free module (and therefore be tested).
int SubworldEngine::battle_relation_callback(void* user, const char* a,
                                             const char* b) {
    auto* self = static_cast<SubworldEngine*>(user);
    if (a == kPlayerFactionId) return player_reputation(self->gs_, b);
    if (b == kPlayerFactionId) return player_reputation(self->gs_, a);
    return faction_relation(self->gs_, a, b);
}

void SubworldEngine::tick_subworld_combat(float dt) {
    if (!ecs_ || dt <= 0.0f) return;
    auto& reg = ecs_->reg;

    // ── Gather: ECS → SoA, O(N) ────────────────────────────────────────────
    // Every living combat body enters the snapshot, including ones this pass
    // must not move: the player body and fleeing wildlife are pinned (BU_Pinned)
    // so they still block and can still be targeted. No actor is special-cased
    // out of existence — only its execution unit differs.
    //
    // Faction identity is the id STRING, interned here into a dense index. The
    // set is rebuilt every tick, so a faction that walks into the window (or a
    // brand-new faction added to the data) simply appears — there is no roster to
    // extend and no vocabulary in the battle code. The relation matrix over the
    // interned set is recomputed only when that set CHANGES or the refresh timer
    // fires, which is what keeps the string lookups off the per-frame path.
    battle_.clear();
    battleEnts_.clear();
    battle_.reserve(kMaxBattleUnits);
    battleEnts_.reserve(std::size_t(kMaxBattleUnits));
    battleFactions_.clear();
    // The player side is interned FIRST so it owns a stable index for the tick;
    // it is an ordinary faction with an ordinary id, not a special case.
    battlePlayerFaction_ = battleFactions_.intern(kPlayerFactionId);
    std::uint64_t playerExtraMask = 0ull;
    float threat2 = kNoThreatDistance2;

    auto actorView = reg.view<ecs::Position, ecs::Health, ecs::Combat,
                              ecs::SubworldTag>(entt::exclude<ecs::Dead>);
    for (auto e : actorView) {
        const auto& p = actorView.get<ecs::Position>(e);
        const auto& hp = actorView.get<ecs::Health>(e);
        const auto& c = actorView.get<ecs::Combat>(e);
        if (hp.hp <= 0.0f) continue;

        BattleUnitDesc d{};
        d.x = p.x; d.y = p.y; d.z = p.z;
        d.radius = target_radius(reg, e);
        d.speed = c.speed;
        d.reach = c.attackRange;
        d.sight = body_sight(reg, e);
        if (c.kind == ecs::Combat::Missile) d.flags |= BU_Missile;
        if (reg.any_of<ecs::Flying>(e)) d.flags |= BU_Flying;

        const bool owned = reg.any_of<ecs::PlayerSoldierTag>(e);
        const bool isPlayer = reg.any_of<ecs::PlayerTag>(e);
        d.faction = std::int16_t(
            (owned || isPlayer)
                ? battlePlayerFaction_
                : battleFactions_.intern(
                      faction_id_for_kind(reg.try_get<ecs::NPCKind>(e))));

        // The player body is input-driven; a fleeing body is driven by sub/ai.cpp.
        // Both are real obstacles and real targets, so they are pinned, not cut.
        if (isPlayer) d.flags |= BU_Pinned;
        if (auto* ai = reg.try_get<ecs::SubworldAi>(e)) {
            d.vx = ai->vx;
            d.vy = ai->vy;
            if (!owned && ai->kind == ecs::SubworldAi::Flee) d.flags |= BU_Pinned;
        }
        // A body flagged individually hostile to the player records that as a
        // private grudge, and hands the player side the reciprocal bit — the one
        // per-entity exception survives as data, never as a branch in the loop.
        if (!isPlayer && !owned && d.faction >= 0
            && reg.any_of<ecs::TempHostileToPlayer>(e)
            && battlePlayerFaction_ >= 0) {
            playerExtraMask |= (1ull << d.faction);
        }

        const int idx = battle_.add(d);
        if (idx < 0) break;                 // 16k ceiling: shared with the renderer
        battleEnts_.push_back(e);
    }
    if (battle_.count <= 0) {
        playerThreatD2_ = kNoThreatDistance2;
        return;
    }

    // ── Hostility as integers ──────────────────────────────────────────────
    // EVERY tick, unconditionally: the interned set was just rebuilt from
    // scratch, so its masks are empty until filled. K² over the factions present
    // (a couple of dozen lookups) — see the warning on build_faction_masks for
    // the bug that "amortising" this caused.
    build_faction_masks(battleFactions_,
                        &SubworldEngine::battle_relation_callback, this,
                        kHostileThreshold);

    // Stamp each body's enemy mask from its faction, plus any private grudge, and
    // fold in the nearest-threat-to-the-player distance while the data is hot —
    // the HUD gem and the exit gate read that every frame and must never scan.
    for (int i = 0; i < battle_.count; ++i) {
        const std::size_t si = std::size_t(i);
        const std::int16_t f = battle_.faction[si];
        std::uint64_t mask = f >= 0 ? battleFactions_.enemyMask[f] : 0ull;
        if (f >= 0 && f == std::int16_t(battlePlayerFaction_)) {
            mask |= playerExtraMask;
        } else if (playerExtraMask != 0ull && f >= 0 && battlePlayerFaction_ >= 0
                   && ((playerExtraMask >> f) & 1ull) != 0ull) {
            mask |= (1ull << battlePlayerFaction_);
        }
        battle_.enemyMask[si] = mask;
        if (battlePlayerFaction_ >= 0
            && ((mask >> battlePlayerFaction_) & 1ull) != 0ull) {
            threat2 = std::min(threat2, dist3sq(battle_.x[si], battle_.y[si],
                                                battle_.z[si],
                                                playerX_, playerY_, playerZ_));
        }
    }
    playerThreatD2_ = threat2;

    // ── Three O(N) structures, then one O(N) steering pass ─────────────────
    // Two bucket grids at two scales — bodies are ~1 unit wide while weapons
    // reach up to 25, and one cell size cannot serve both without becoming
    // either quadratic or blind. Both cell sizes come from the crowd's own data.
    build_unit_grid(battleFine_, battle_, fine_cell_for(battle_, battleParams_),
                    kBattleGridMaxDim);
    build_unit_grid(battlePick_, battle_, pick_cell_for(battle_, battleParams_),
                    kBattleGridMaxDim);
    build_influence_field(battleField_, battle_, battleParams_.influenceCell,
                          float(kFullSize));
    BattleTerrain terrain{};
    terrain.heightAt = &SubworldEngine::battle_height_callback;
    terrain.user = this;
    // Ground cost is DATA: the tile grid the generator already produced, plus the
    // one movement table in sub/map_data.h. The battle pass does not know what
    // water is, and a new ground type needs no code here.
    const std::vector<std::uint8_t>& tiles = mgr_.tiles();
    if (tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize)) {
        terrain.tiles = tiles.data();
        terrain.tileDim = kFullSize;
        terrain.tileSpeed = kTileMovementSpeed;
        terrain.tileSpeedCount = int(TILE_COUNT);
    }
    terrain.worldMax = float(kFullSize);
    BattleStats stats{};
    steer_battle(battle_, battleFine_, battlePick_, battleField_, terrain,
                 battleParams_, dt, &stats);

    // ── Scatter: SoA → ECS ─────────────────────────────────────────────────
    // Z is deliberately untouched: the ground-follow pass in tick() owns it.
    for (int i = 0; i < battle_.count; ++i) {
        const std::size_t si = std::size_t(i);
        if ((battle_.flags[si] & BU_Pinned) != 0u) continue;
        const entt::entity e = battleEnts_[si];
        if (!reg.valid(e)) continue;
        auto& p = reg.get<ecs::Position>(e);
        p.x = battle_.x[si];
        p.y = battle_.y[si];
        if (auto* ai = reg.try_get<ecs::SubworldAi>(e)) {
            ai->vx = battle_.vx[si];
            ai->vy = battle_.vy[si];
        }
    }

    auto strike = [&](entt::entity attacker, entt::entity target,
                      ecs::Combat& c, bool playerOwned) {
        if (!reg.valid(target)) return;
        auto* hp = reg.try_get<ecs::Health>(target);
        if (!hp || hp->hp <= 0.0f) return;
        hp->hp -= c.damage;
        reg.emplace_or_replace<ecs::LastHit>(
            target, std::uint32_t(entt::to_integral(attacker)), playerOwned);
        reg.emplace_or_replace<ecs::HitFlash>(
            target, ecs::HitFlash{kHitFlashDuration});
        // Universal impact-VFX marker (Inc C): blood/dust for every combatant,
        // not just player-dealt hits — an NPC-vs-NPC skirmish sprays too.
        reg.emplace_or_replace<ecs::DamageFx>(
            target, ecs::DamageFx{hp->hp <= 0.0f});
        c.cooldownTimer = c.cooldown;
        if (hp->hp <= 0.0f && !reg.any_of<ecs::Dead>(target)) {
            reg.emplace<ecs::Dead>(target);
            // A dead player is a game-over, not an NPC kill: skip the NpcDeath
            // event so it never counts toward quest kill-tallies or XP.
            if (bus_ && !reg.any_of<ecs::PlayerTag>(target)) {
                GameEvent ev{EventTag::NpcDeath};
                ev.a = std::uint32_t(entt::to_integral(target));
                ev.b = std::uint32_t(entt::to_integral(attacker));
                if (const auto* kind = reg.try_get<ecs::NPCKind>(target)) {
                    ev.ix = int(kind->type);
                }
                bus_->emit(ev);
            }
        }
    };

    // ── Resolve blows ──────────────────────────────────────────────────────
    // Steering already decided who each body faces and whether that body is
    // strikeable this tick (3D reach), so this pass is pure gameplay authority:
    // one bounded loop, no distance search, no hostility re-test. Damage,
    // death, loot, XP and events stay exactly where they were — in the ECS.
    for (int i = 0; i < battle_.count; ++i) {
        const std::size_t si = std::size_t(i);
        if (!battle_.inReach[si]) continue;
        // The player body — including a possessed NPC wearing PlayerTag — is
        // driven by input + tick_player_melee, never by auto-combat. Fleeing
        // bodies do not fight either. Both are pinned, so this one test covers
        // them without naming either case.
        if ((battle_.flags[si] & BU_Pinned) != 0u) continue;
        const entt::entity e = battleEnts_[si];
        if (!reg.valid(e) || !alive_subworld_entity(reg, e)) continue;
        const std::int32_t t = battle_.target[si];
        if (t < 0) continue;
        const entt::entity targetEnt = battleEnts_[std::size_t(t)];
        if (!reg.valid(targetEnt) || !alive_subworld_entity(reg, targetEnt))
            continue;

        auto& c = reg.get<ecs::Combat>(e);
        if (c.cooldownTimer > 0.0f) continue;
        const bool owned = reg.any_of<ecs::PlayerSoldierTag>(e);
        if (c.kind == ecs::Combat::Missile) {
            const auto& p = reg.get<ecs::Position>(e);
            const auto& tp = reg.get<ecs::Position>(targetEnt);
            spawn_npc_missile(reg, e, p, c, tp.x, tp.y, tp.z);
            c.cooldownTimer = c.cooldown;
            continue;
        }
        strike(e, targetEnt, c, owned);
    }
}

void SubworldEngine::resolve_subworld_deaths(bool drainAll) {
    if (!ecs_ || !gs_) return;
    auto& reg = ecs_->reg;
    int deadCount = 0;
    do {
        std::array<entt::entity, kMaxSubworldDeathsPerFrame> dead{};
        deadCount = 0;
        auto view = reg.view<ecs::Dead, ecs::SubworldTag>();
        for (auto e : view) {
            if (deadCount >= kMaxSubworldDeathsPerFrame) break;
            dead[std::size_t(deadCount++)] = e;
        }

        for (int i = 0; i < deadCount; ++i) {
            const entt::entity e = dead[std::size_t(i)];
            if (!reg.valid(e)) continue;
            // The player is a combat entity but never a corpse: its death is a
            // game-over routed through the macro scalar (reconcile_player_hp_to_macro
            // drives currentHp to 0 -> AppState::Dead), not a loot/XP/removal
            // event, and the entity is reset on the next subworld enter().
            if (reg.any_of<ecs::PlayerTag>(e)) continue;
            const auto* pos = reg.try_get<ecs::Position>(e);
            const auto* kind = reg.try_get<ecs::NPCKind>(e);
            const auto* level = reg.try_get<ecs::NpcLevel>(e);
            const auto* lastHit = reg.try_get<ecs::LastHit>(e);
            const int lvl = normalize_soldier_level(level ? level->value : 1);

            if (reg.any_of<ecs::PlayerSoldierTag>(e)) {
                if (const auto* link = reg.try_get<ecs::SoldierLink>(e)) {
                    remove_one_soldier_by_entity_id(gs_->player.army, link->entityId);
                }
                reg.destroy(e);
                continue;
            }

            if (lastHit && lastHit->playerOwned) {
                int xp = exp_from_fight(lvl);
                if (kind && kind->type < std::uint16_t(NPCType::Count)) {
                    xp = npc_xp_reward(static_cast<NPCType>(std::uint8_t(kind->type)), lvl);
                } else if (kind) {
                    // Monster: per-creature XP base (fauna.h xpReward) scaled by
                    // level like npc_xp_reward. xpReward 0 keeps the generic
                    // exp_from_fight(lvl) fallback (behavior-preserving).
                    if (const FaunaEntry* cd = creature_def_from_kind(kind->type);
                        cd && cd->xpReward > 0) {
                        xp = int(cd->xpReward) + (lvl - 1) * 5;
                    }
                }
                gs_->player.sheet.levelData.exp += xp;
                while (try_level_up(gs_->player.sheet.levelData)) {}
                apply_player_kill_reputation(gs_, kind);
            }

            Inventory inv{};
            if (const auto* bag = reg.try_get<ecs::NpcInventory>(e)) {
                inv = bag->inv;
            }

            const std::uint32_t seed = gs_->worldSeed
                ^ (std::uint32_t(entt::to_integral(e)) * kEntityLootMix)
                ^ std::uint32_t(lvl * 7919);
            Rng rng(seed);
            gLootRng = &rng;
            if (inv.stacks.empty() && kind) {
                // Single keyed loot path (macro/items.h roll_loot_profile): a
                // humanoid NPC resolves by role, a monster by its creature-table
                // lootId (null => faction default). Both share one resolver, so a
                // Bandits-faction creature now drops real items via the "bandits"
                // profile instead of nothing (the old faction-string gap).
                const char* lootId;
                if (kind->type < std::uint16_t(NPCType::Count)) {
                    lootId = npc_loot_id(int(kind->type));
                } else if (const FaunaEntry* cd = creature_def_from_kind(kind->type);
                           cd && cd->lootId && cd->lootId[0]) {
                    lootId = cd->lootId;
                } else {
                    lootId = faction_id_for_kind(kind);
                }
                auto stacks = roll_loot_profile(lootId, lvl, &loot_rng_f01);
                for (const ItemStack& s : stacks) inv.add(s.id, s.count);
            }
            const char* factionId = faction_id_for_kind(kind);
            const int gold = generate_loot_gold(lvl, factionId, &loot_rng_f01);
            gLootRng = nullptr;

            if (pos && (gold > 0 || !inv.stacks.empty())) {
                auto corpse = reg.create();
                reg.emplace<ecs::Position>(corpse, pos->x, pos->y, pos->z);
                reg.emplace<ecs::SubworldTag>(corpse);
                reg.emplace<ecs::Structure>(
                    corpse, ecs::Structure::Corpse, pos->x, pos->y, 4.0f, 0.3f);
                reg.emplace<ecs::CorpseLoot>(corpse, std::move(inv), gold);
            }
            reg.destroy(e);
        }
    } while (drainAll && deadCount == kMaxSubworldDeathsPerFrame);
}

void SubworldEngine::leave(bool force) {
    if (!force && exit_blocked_by_danger()) {
        set_status("Exit blocked: hostiles are too close in this danger zone.");
        return;
    }
    entt::entity possessedMacro = entt::null;   // set iff exit was AS a lord (5e-2)
    if (active_) {
        resolve_subworld_deaths(true);
        mgr_.snapshot_all_to_cache();
        // Sync the player's MACRO position from where they actually ended
        // up in the subworld. The seamless manager re-centres the 3×3 grid
        // when the player crosses a cell boundary, so `mgr_.center_cx()/cy()`
        // is the macro cell currently under the player. Macro convention
        // (see ui/macro_overlay.cpp player render) is `player.x` = INTEGER
        // cell index; the sprite is drawn at `player.x + 0.5` to centre it.
        // Writing a fractional sub-cell offset here would render the sprite
        // at `cellIdx + 0.5 + 0.5` = vertex of 4 cells. Snap to the centre
        // cell of the seamless 3×3 grid.
        //
        // Inc 5e-1 (D5 exit remap): if the player possessed a macro-projected
        // body (it carries a `MacroOrigin` backlink), land the macro player on
        // THAT lord's macro cell instead — you "exit AS" the body you possessed.
        // Must read the backlink here, BEFORE clear_subworld_entities below reaps
        // every SubworldTag body. Falls back to the window centre for a normal
        // un-possessed exit.
        possessedMacro = remap_macro_player_to_origin();
        if (possessedMacro == entt::null) {
            sync_macro_player_to_center();
        }
    }
    if (ecs_) {
        clear_subworld_entities(*ecs_);
        // Authoritative player teardown (symmetric with spawn_player_entity on
        // enter). The player carries SubworldTag, so the reaper above already
        // destroyed it; this explicit clear owns the player lifecycle regardless
        // of that incidental overlap and guarantees no PlayerTag entity leaks
        // into the macro world.
        clear_player_entity();
        // Inc 5e-2 (identity remap): if the exit was AS a possessed lord, ADOPT
        // that macro NPC as the persistent player — move the single flag onto it
        // and record its save-stable spawn ordinal. clear_player_entity() just
        // tore down the subworld body that wore the flag, and the macro entity
        // (no SubworldTag) survived the reaper, so this re-homes the one flag
        // cleanly. Un-possessed exits pass entt::null ⇒ -1, and the next macro
        // tick's ensure_macro_player_entity() re-creates the hero husk.
        if (gs_) {
            gs_->player.possessedMacroSpawnId =
                adopt_possessed_macro_as_player(*ecs_, possessedMacro);
        }
    }
    active_ = false;
    pendingUpload3d_ = {};
    gs_ = nullptr;
    terrain_ = nullptr;
    features_ = nullptr;
    ecs_ = nullptr;
    bus_ = nullptr;
    zones_ = nullptr;
    treeLayer_ = nullptr;
    playerAttackHeld_ = false;
    flightCamY_ = 0.0f;
    playerAttackTimer_ = 0.0f;
    statusLine_.clear();
    statusTimer_ = 0.0f;
    combatLogCount_ = 0;
}

entt::entity SubworldEngine::player_entity() const {
    if (!ecs_) return entt::null;
    auto v = ecs_->reg.view<ecs::PlayerTag>();
    return v.empty() ? entt::null : v.front();
}

bool SubworldEngine::flying() const {
    auto e = player_entity();
    return e != entt::null && ecs_->reg.any_of<ecs::Flying>(e);
}

void SubworldEngine::set_flying(bool enabled) {
    auto e = player_entity();
    if (e == entt::null) return;

    const bool currently_flying = ecs_->reg.any_of<ecs::Flying>(e);
    if (enabled && !currently_flying) {
        flightCamY_ = renderer3dVk_.sample_height_m(playerX_, playerY_) + kCameraEyeM;
        ecs_->reg.emplace<ecs::Flying>(e);
    } else if (!enabled && currently_flying) {
        flightCamY_ = 0.0f;
        ecs_->reg.remove<ecs::Flying>(e);
    }
}

void SubworldEngine::move_player(float dx, float dy) {
    if (!active_) return;
    // First-person walk: dy = forward (UP arrow / W), dx = strafe right.
    // Camera-forward in tile-XY plane is (cos yaw, sin yaw); right is its
    // 90° rotation (-sin yaw, cos yaw). Compose world delta from those
    // basis vectors so UP always means "into the screen".
    const float cy = std::cos(cam_.yaw), sy = std::sin(cam_.yaw);
    if (flying()) {
        const float cp = std::cos(cam_.pitch);
        const float sp = std::sin(cam_.pitch);
        const float wx = dy * cy * cp - dx * sy;
        const float wy = dy * sy * cp + dx * cy;
        playerX_ += wx * kSubworldFirstPersonMoveScale;
        playerY_ += wy * kSubworldFirstPersonMoveScale;
        flightCamY_ += dy * sp * kSubworldFirstPersonMoveScale;
    } else {
        const float wx = dy * cy - dx * sy;
        const float wy = dy * sy + dx * cy;
        playerX_ += wx * kSubworldFirstPersonMoveScale;
        playerY_ += wy * kSubworldFirstPersonMoveScale;
    }
    if (playerX_ < 0) playerX_ = 0;
    if (playerY_ < 0) playerY_ = 0;
    if (playerX_ > float(kFullSize)) playerX_ = float(kFullSize);
    if (playerY_ > float(kFullSize)) playerY_ = float(kFullSize);
    // Inc 5a: the entity Position is authoritative — commit the moved scalars
    // onto it (the tick's top pull would otherwise revert this input next frame).
    push_scalars_to_player_entity();
}

void SubworldEngine::set_player_pos(float x, float y) {
    if (!active_) return;
    playerX_ = std::clamp(x, 1.0f, float(kFullSize - 2));
    playerY_ = std::clamp(y, 1.0f, float(kFullSize - 2));
    // Inc 5a: the entity Position is authoritative — commit the teleport onto it.
    push_scalars_to_player_entity();
    // The next tick()'s check_boundary() re-centres the seamless window and
    // repopulates the cell if this crossed into a new one.
}

void SubworldEngine::respawn_fauna() {
    if (!active_) return;
    // Rebuild the whole 3×3 scene from scratch via the enter() path. Under the
    // seamless per-cell model each cell's fauna is deterministic from its
    // absolute macro seed, so this reproduces the current scene exactly rather
    // than rolling a different set — a true re-roll is the future per-cell
    // visitation-age epoch mixed into the seed, not a dev-console dice throw.
    spawn_all_cells();
}

int SubworldEngine::dev_kill_all_hostiles() {
    if (!active_ || !ecs_ || !gs_) return 0;
    auto& reg = ecs_->reg;
    int killed = 0;
    int batch = 0;
    // Batched drain mirroring resolve_subworld_deaths: collect a bounded set of
    // still-alive hostiles, tag them Dead (which drops them from the excl view),
    // repeat until a pass finds none. The normal death path then awards XP+loot.
    do {
        std::array<entt::entity, kMaxSubworldDeathsPerFrame> victims{};
        batch = 0;
        auto view = reg.view<ecs::Health, ecs::SubworldTag>(
            entt::exclude<ecs::Dead>);
        for (auto e : view) {
            if (batch >= kMaxSubworldDeathsPerFrame) break;
            if (!hostile_to_player_entity(reg, e, gs_)) continue;
            victims[std::size_t(batch++)] = e;
        }
        for (int i = 0; i < batch; ++i) {
            const entt::entity e = victims[std::size_t(i)];
            if (!reg.valid(e)) continue;
            if (auto* hp = reg.try_get<ecs::Health>(e)) hp->hp = 0.0f;
            reg.emplace_or_replace<ecs::LastHit>(e, std::uint32_t{0}, true);
            // Impact-VFX marker (Inc C): the dev cheat sprays too, so this is a
            // free in-game test of the whole blood/dust path.
            reg.emplace_or_replace<ecs::DamageFx>(e, ecs::DamageFx{true});
            reg.emplace<ecs::Dead>(e);
            if (bus_) {
                GameEvent ev{EventTag::NpcDeath};
                ev.a = std::uint32_t(entt::to_integral(e));
                ev.b = 0u;
                if (const auto* kind = reg.try_get<ecs::NPCKind>(e))
                    ev.ix = int(kind->type);
                bus_->emit(ev);
            }
            ++killed;
        }
    } while (batch > 0);
    return killed;
}

void SubworldEngine::rotate_camera(float dyaw, float dpitch) {
    cam_.yaw   += dyaw;
    cam_.pitch += dpitch;
    cam_.pitch = std::clamp(cam_.pitch, -kMaxPitchRad, kMaxPitchRad);
}

void SubworldEngine::tick(float dt) {
    if (!active_) return;
    elapsed_ += dt;
    if (statusTimer_ > 0.0f) {
        statusTimer_ -= dt;
        if (statusTimer_ <= 0.0f) statusLine_.clear();
    }
    for (int i = 0; i < combatLogCount_; ++i) {
        combatLog_[std::size_t(i)].age += dt;
    }
    // Inc 5a: the entity Position is authoritative — mirror it onto the scalars
    // so the seam (check_boundary) re-centres against the player's true position
    // (e.g. right after a possession snapped the flag onto a body elsewhere).
    pull_player_entity_to_scalars();
    int prevCx = mgr_.center_cx(), prevCy = mgr_.center_cy();
    const auto seamStart = Clock::now();
    mgr_.check_boundary(playerX_, playerY_);
    const SeamTiming timing = mgr_.last_seam_timing();
    const bool centerChanged = prevCx != mgr_.center_cx() || prevCy != mgr_.center_cy();
    const CompositeDirty dirtyNow = mgr_.consume_composite_dirty_cells();
    if (centerChanged) {
        sync_macro_player_to_center();
        // Seamless persistence: carry the current 3×3's creatures across the
        // re-centre (shift + evict-departed + spawn-newly-entered) instead of
        // wiping and rebuilding. Entities in cells still inside the window —
        // including a city you just stepped out of — survive untouched; only
        // the cells that genuinely left are despawned.
        const int dx = mgr_.center_cx() - prevCx;
        const int dy = mgr_.center_cy() - prevCy;
        repopulate_after_recenter(dx, dy);
        // Inc 5a: check_boundary rewrapped the scalars ∓cell and the rebase inside
        // repopulate shifted the SubworldTag-tagged player entity by the SAME
        // amount. Commit the scalars back onto the entity (assignment ⇒ no double
        // shift) so the entity remains the single source of truth after the seam.
        push_scalars_to_player_entity();
    }

    double upload3dMs = 0.0;
    if (dirtyNow.any) {
        pendingUpload3d_.merge(dirtyNow);
    }
    if (pendingUpload3d_.any) {
        auto t0 = Clock::now();
        if (dev_) renderer3dVk_.upload(*dev_, mgr_, pendingUpload3d_);
        auto t1 = Clock::now();
        upload3dMs = elapsed_ms(t0, t1);
        pendingUpload3d_ = {};
    }

    if (timing.crossed && seam_trace_enabled()) {
        const double totalMs = elapsed_ms(seamStart, Clock::now());
        std::fprintf(stderr,
            "[seam-cross] gen=%.3fms smooth=%.3fms upload3d=%.3fms total=%.3fms\n",
            timing.genMs, timing.smoothMs, upload3dMs, totalMs);
        std::fflush(stderr);
    }

    if (ecs_) {
        // Pull the authoritative macro scalars onto the player entity (Position
        // + Health) before combat runs; the entity then participates like any
        // other actor. It survives seamless re-centres because the respawn clear
        // now skips PlayerTag as well as PlayerSoldierTag.
        sync_player_entity_position();
        // Ground-follow: pin non-flying, non-projectile entities to the terrain
        // surface. Position.z is set to sample_height_m(x,y) — the absolute
        // world-space altitude of the terrain at that tile coordinate. Flying
        // entities and projectiles own their z through movement/velocity.
        // PlayerTag entities are excluded — the player's z is set by the camera
        // sync in record_shadow() based on flight state.
        {
            auto gv = ecs_->reg.view<ecs::Position, ecs::SubworldTag>(
                entt::exclude<ecs::Flying, ecs::Projectile, ecs::PlayerTag>);
            for (auto e : gv) {
                auto& p = gv.get<ecs::Position>(e);
                p.z = renderer3dVk_.sample_height_m(p.x, p.y);
            }
        }
        // Flying entities own their z but stay inside the flight envelope
        // (height.h): floor = terrain surface (never underground — the old
        // sea-level-based ceiling sat BELOW the ground in high mountains and
        // std::min shoved flyers under the relief), ceiling = the window's
        // highest terrain + kFlightMaxAboveTerrainM. Projectiles are NOT
        // clamped at all — they arc freely and die on honest terrain
        // collision in tick_spell_projectiles.
        {
            const float ceilZ = renderer3dVk_.max_height_m()
                              + kFlightMaxAboveTerrainM;
            auto fv = ecs_->reg.view<ecs::Position, ecs::SubworldTag,
                                     ecs::Flying>(entt::exclude<ecs::PlayerTag>);
            for (auto e : fv) {
                auto& p = fv.get<ecs::Position>(e);
                const float groundZ =
                    renderer3dVk_.sample_height_m(p.x, p.y);
                p.z = std::clamp(p.z, groundZ, std::max(groundZ, ceilZ));
            }
        }
        tick_player_melee(dt);
        tick_npc_ai(*ecs_, playerX_, playerY_, std::uint32_t{0}, dt,
                    &SubworldEngine::player_threat_callback, this);
        tick_subworld_combat(dt);
        ecs::sys::tick_visual_interp(*ecs_, dt);
        ecs::sys::tick_combat_cooldowns(*ecs_, dt);
        tick_spell_projectiles(*ecs_, bus_, dt,
                               &SubworldEngine::spell_damage_log_callback,
                               this,
                               &SubworldEngine::spell_can_hit_callback,
                               this,
                               &SubworldEngine::spell_fx_emit_callback,
                               this,
                               &SubworldEngine::spell_height_callback,
                               this);
        tick_hit_flashes(dt);
        // Turn this tick's damage markers into blood/dust BEFORE deaths are
        // resolved, so a killing blow still sprays from the body's live position.
        tick_damage_fx();
        resolve_subworld_deaths();
        // Push the player entity's post-combat Health back onto the macro
        // scalar. Incoming melee and projectile damage now both land on the
        // entity's Health via the universal paths above; this is the single
        // place that reconciles it to currentHp (and drives the death screen).
        reconcile_player_hp_to_macro();
    }

    // Advance the transient-VFX pool. Emitters (spell trails, impact bursts,
    // blood) feed it from the combat/spell ticks above; here we just integrate
    // and reap. Pure CPU, no ECS churn — see sub/particles.h.
    particles_.tick(dt);
}

void SubworldEngine::prepare_frame(VkCommandBuffer cmd) {
    if (!active_ || !dev_) return;
    if (pendingUpload3d_.any) {
        renderer3dVk_.upload(*dev_, mgr_, pendingUpload3d_);
        pendingUpload3d_ = {};
    }
    renderer3dVk_.prepare_frame(cmd, ecs_, elapsed_);

    // Pack the live particle pool into GPU instances and stage them for this
    // frame's additive pass. Reused scratch (sized once to the pool ceiling) ⇒
    // no per-frame allocation. Empty pool ⇒ stage_particles(...,0) draws nothing.
    if (particleScratch_.size()
        != static_cast<std::size_t>(ParticleSystem::kMaxParticles)) {
        particleScratch_.resize(ParticleSystem::kMaxParticles);
    }
    const std::uint32_t n =
        particles_.pack(particleScratch_.data(),
                        static_cast<std::uint32_t>(particleScratch_.size()));
    renderer3dVk_.stage_particles(cmd, particleScratch_.data(), n);
}

void SubworldEngine::record_shadow(VkCommandBuffer cmd) {
    if (!active_ || !dev_) return;
    if (pendingUpload3d_.any) {
        renderer3dVk_.upload(*dev_, mgr_, pendingUpload3d_);
        pendingUpload3d_ = {};
    }
    // Sync camera to player tile position with eye height above terrain.
    if (gs_) {
        float wx = 0, wz = 0;
        Renderer3DVk::tile_to_world(playerX_, playerY_, wx, wz);
        float groundM = renderer3dVk_.sample_height_m(playerX_, playerY_);
        const float groundEyeM = groundM + kCameraEyeM;
        if (flying()) {
            // Same envelope as Flying entities (height.h): the eye never goes
            // below the local ground eye and never above the window's highest
            // terrain + margin. The ceiling is absolute for the window, so
            // crossing a cliff edge keeps the altitude instead of yanking the
            // camera down with the terrain.
            const float maxCamY = renderer3dVk_.max_height_m()
                                + kFlightMaxAboveTerrainM + kCameraEyeM;
            flightCamY_ = std::clamp(flightCamY_, groundEyeM,
                                     std::max(groundEyeM, maxCamY));
            cam_.pos = {wx, flightCamY_, wz};
            playerZ_ = flightCamY_ - kCameraEyeM;
        } else {
            flightCamY_ = groundEyeM;
            cam_.pos = {wx, groundEyeM, wz};
            playerZ_ = groundM;
        }
        push_scalars_to_player_entity();
    }
    renderer3dVk_.record_shadow(cmd, cam_, gs_->worldTime);
}

void SubworldEngine::record_main(VkCommandBuffer cmd, VkExtent2D ext,
                                 std::uint32_t frameIndex) {
    if (!active_ || !gs_) return;
    const bool hasteAura =
        spellbook_has_sustained(gs_->player.spellBook, "haste");
    const bool flightAura = flying();
    renderer3dVk_.record_main(cmd, ext, cam_, gs_->worldTime, WATER_LEVEL,
                              &mgr_, ecs_, hasteAura, flightAura,
                              playerX_, playerY_, elapsed_, frameIndex);
}

} // namespace sm::sub
