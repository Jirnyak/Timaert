#include "sub/engine.h"
#include "sub/spawn.h"
#include "sub/ai.h"
#include "sub/spatial_hash.h"
#include "sub/spell_effects.h"
#include "sub/base_generator.h"
#include "ecs/systems.h"
#include "macro/state.h"
#include "macro/npc.h"
#include "macro/items.h"
#include "macro/attributes.h"
#include "macro/map_generator.h"
#include "macro/features.h"
#include "macro/biomes.h"
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

constexpr int kMaxSubworldCombatActors = 2048;
constexpr int kMaxSubworldDeathsPerFrame = 512;
constexpr int kMaxSubworldEntityReaps = 2048;
constexpr float kSubworldFirstPersonMoveScale = 0.4f;
constexpr float kHitFlashDuration = 0.15f;
constexpr float kPlayerMeleeRange = 5.0f;
constexpr float kPlayerMeleeCooldown = 0.5f;
constexpr float kPlayerCollisionRadius = 1.5f;
constexpr int kAllyRepThreshold = 50;
constexpr int kKillRepPenalty = -1;
constexpr float kFlightMaxAboveGroundM = 120.0f;
constexpr float kCameraEyeM = 1.7f;
constexpr std::uint32_t kFnvOffset =
    std::uint32_t{2147483647} + std::uint32_t{18652614};
constexpr std::uint32_t kFnvPrime = std::uint32_t{16777619};
constexpr std::uint32_t kCellSeedX = std::uint32_t{73856093};
constexpr std::uint32_t kCellSeedY = std::uint32_t{19349663};
constexpr std::uint32_t kSquadSpawnSalt =
    std::uint32_t{2147483647} + std::uint32_t{622657538};
constexpr std::uint32_t kEntityLootMix =
    std::uint32_t{2147483647} + std::uint32_t{506952114};
constexpr std::uint32_t kNpcMissileSpellId = 0x4E50434Du; // "NPCM"

float target_radius(const entt::registry& reg, entt::entity e) {
    if (const auto* ai = reg.try_get<ecs::SubworldAi>(e)) return ai->radius;
    if (const auto* sp = reg.try_get<ecs::Sprite>(e)) return sp->scale;
    return 6.0f;
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

const char* fauna_faction_id_for(std::uint16_t factionIdx) {
    switch (factionIdx) {
    case 1: return "wildlife";
    case 2: return "bandits";
    case 3: return "demons";
    default: return "";
    }
}

const char* npc_faction_id_for(std::uint16_t factionIdx) {
    switch (factionIdx) {
    case 0: return "empire";
    case 1: return "magika";
    case 2: return "timaert";
    case 3: return "bandits";
    case 4: return "cults";
    default: return "";
    }
}

const char* faction_id_for_kind(const ecs::NPCKind* kind) {
    if (!kind) return "";
    if (kind->type >= std::uint16_t{0x100}) {
        return fauna_faction_id_for(kind->factionIdx);
    }
    return npc_faction_id_for(kind->factionIdx);
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
                       float dist) {
    const auto* missile = reg.try_get<ecs::MissileAttack>(attacker);
    const float speed = missile && missile->speed > 0.0f
        ? missile->speed
        : 200.0f;
    const float nx = dist > 0.001f ? (targetX - origin.x) / dist : 1.0f;
    const float ny = dist > 0.001f ? (targetY - origin.y) / dist : 0.0f;
    const float attackerRadius = target_radius(reg, attacker);
    const float sx = origin.x + nx * (attackerRadius + 1.0f);
    const float sy = origin.y + ny * (attackerRadius + 1.0f);
    const float life = std::max(0.5f, (combat.attackRange + 4.0f) / speed);
    const float blast = missile ? missile->blastRadius : 0.0f;
    const std::uint32_t color = missile ? missile->colorRGBA : 0xFFFFFFFFu;
    const std::uint8_t r = std::uint8_t((color >> 16) & 0xFFu);
    const std::uint8_t g = std::uint8_t((color >> 8) & 0xFFu);
    const std::uint8_t b = std::uint8_t(color & 0xFFu);
    const std::uint8_t a = std::uint8_t((color >> 24) & 0xFFu);

    entt::entity e = reg.create();
    reg.emplace<ecs::Position>(e, sx, sy);
    reg.emplace<ecs::Projectile>(
        e,
        nx * speed, ny * speed,
        1.2f, life, life,
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

void SubworldEngine::init() {
    if (inited_) return;
    const bool trace = [] {
        const char* env = std::getenv("TIMAERT_BOOT_TRACE");
        return env && env[0] != '\0' && env[0] != '0';
    }();
    if (trace) { std::fprintf(stderr, "[boot] subworld renderer3d init start\n"); std::fflush(stderr); }
    renderer3d_.init();
    if (trace) { std::fprintf(stderr, "[boot] subworld renderer3d init done\n"); std::fflush(stderr); }
    if (trace) { std::fprintf(stderr, "[boot] subworld sky init start\n"); std::fflush(stderr); }
    sky_.init();
    if (trace) { std::fprintf(stderr, "[boot] subworld sky init done\n"); std::fflush(stderr); }
    inited_ = true;
}

void SubworldEngine::destroy() {
    if (!inited_) return;
    renderer3d_.destroy();
    sky_.destroy();
    inited_ = false;
}

void SubworldEngine::enter(GameState& gs, const TerrainData& terrain,
                           const FeatureLayer& features, ecs::World& ecs,
                           EventBus& bus,
                           const ZoneLayer* zones) {
    statusLine_.clear();
    statusTimer_ = 0.0f;
    combatLogCount_ = 0;
    if (!terrain.has_rgba_storage()) {
        gs_ = nullptr;
        terrain_ = nullptr;
        features_ = nullptr;
        ecs_ = nullptr;
        bus_ = nullptr;
        zones_ = nullptr;
        active_ = false;
        upload3dDirty_ = false;
        set_status("Subworld unavailable: invalid terrain");
        return;
    }

    gs_ = &gs; terrain_ = &terrain; features_ = &features;
    ecs_ = &ecs; bus_ = &bus; zones_ = zones;
    int cx = int(gs.player.x);
    int cy = int(gs.player.y);

    auto resolver = [this](int x, int y) { return resolve_context(x, y); };

    mgr_.init(cx, cy, resolver);
    renderer3d_.upload(mgr_);
    mgr_.consume_composite_dirty();
    active_  = true;
    upload3dDirty_ = false;
    playerX_ = playerY_ = float(kFullSize / 2);
    playerFlying_ = false;
    playerAttackHeld_ = false;
    playerAttackTimer_ = 0.0f;
    flightCamY_ = 0.0f;
    spellRng_ = Rng{gs.worldSeed
        + std::uint32_t(cx) * std::uint32_t{1000}
        + std::uint32_t(cy)};

    // Resolve centre cell again to pull biome + feature + landmark for the
    // initial fauna roll. We re-run the resolver (cheap) instead of
    // duplicating the inline math here.
    respawn_npcs_for_center();
    spawn_player_squad(ecs, gs.player.army, mgr_, playerX_, playerY_,
        gs.worldSeed ^ kSquadSpawnSalt ^ (std::uint32_t(cx) << 8) ^ std::uint32_t(cy));
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
    c.macroTemperature = t;
    c.biome   = mask ? biome_from_climate(t, m) : Biome::Water;
    c.feature = features_->at(xi, yi);
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

// Populate the current centre cell's world NPCs: fauna + settlement citizens.
// Called on enter() AND on every seamless re-centre (tick), so walking into a
// city from a neighbouring subworld cell fills it just like a fresh entry. The
// player's projected squad is preserved by respawn's clear step, so it is not
// wiped here.
void SubworldEngine::respawn_npcs_for_center() {
    if (!ecs_ || !gs_ || !terrain_ || terrain_->width <= 0
        || terrain_->height <= 0) {
        return;
    }
    const int ccx = mgr_.center_cx();
    const int ccy = mgr_.center_cy();
    const int W = terrain_->width, H = terrain_->height;
    const int wcx = ((ccx % W) + W) % W;
    const int wcy = ((ccy % H) + H) % H;
    const CellContext center = resolve_context(ccx, ccy);
    LandmarkKind lk = LandmarkKind::None;
    switch (center.landmarkKind) {
        case CellLandmarkKind::City:    lk = LandmarkKind::City; break;
        case CellLandmarkKind::Village: lk = LandmarkKind::Village; break;
        case CellLandmarkKind::Ruin:    lk = LandmarkKind::Ruin; break;
        case CellLandmarkKind::Spire:   lk = LandmarkKind::Spire; break;
        case CellLandmarkKind::None:
            if (center.landmarkSettlementId >= 0) lk = LandmarkKind::City;
            break;
    }
    const int zoneLevel = (zones_ && !zones_->data.empty())
        ? int(zones_->at(wcx, wcy)) : 0;
    respawn_subworld_npcs(*ecs_, center.biome, center.feature, lk, mgr_,
        gs_->worldSeed ^ (std::uint32_t(wcx) << 16) ^ std::uint32_t(wcy),
        center.landmarkSize, zoneLevel);
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
    auto* engine = static_cast<SubworldEngine*>(user);
    if (!engine || !engine->ecs_ || !engine->gs_) return true;
    if (projectile.friendlyFire) return true;

    auto& reg = engine->ecs_->reg;
    const entt::entity target = entt::entity(targetEntityId);
    if (!reg.valid(target)) return false;

    const bool ownerIsPlayerSide =
        projectile.ownerId == 0u
        || (reg.valid(entt::entity(projectile.ownerId))
            && is_player_side(reg, entt::entity(projectile.ownerId)));
    if (ownerIsPlayerSide) {
        return hostile_to_player_entity(reg, target, engine->gs_);
    }

    const entt::entity owner = entt::entity(projectile.ownerId);
    if (reg.valid(owner) && is_player_side(reg, target)) {
        return hostile_to_player_entity(reg, owner, engine->gs_);
    }
    return true;
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
    const float range2 = kPlayerMeleeRange * kPlayerMeleeRange;
    entt::entity target = entt::null;
    float bestD2 = range2;
    auto view = reg.view<ecs::Position, ecs::Health, ecs::NPCKind,
                         ecs::SubworldTag>(entt::exclude<ecs::Dead>);
    for (auto e : view) {
        if (reg.any_of<ecs::PlayerSoldierTag>(e)) continue;
        const auto& hp = view.get<ecs::Health>(e);
        if (hp.hp <= 0.0f) continue;
        const auto& pos = view.get<ecs::Position>(e);
        const float d2 = dist2(pos.x, pos.y, playerX_, playerY_);
        if (d2 <= bestD2) {
            bestD2 = d2;
            target = e;
        }
    }
    if (target == entt::null) return;

    auto* hp = reg.try_get<ecs::Health>(target);
    if (!hp || hp->hp <= 0.0f) return;
    const DerivedBonuses derived =
        calculate_derived(gs_->player.attributes, gs_->player.skills);
    const float damage = std::floor(10.0f + derived.rawPhysDamage);
    const bool lethal = hp->hp > 0.0f && hp->hp - damage <= 0.0f;
    hp->hp -= damage;
    reg.emplace_or_replace<ecs::LastHit>(
        target, std::uint32_t{0}, true);
    reg.emplace_or_replace<ecs::HitFlash>(
        target, ecs::HitFlash{kHitFlashDuration});
    push_player_hit_log(std::uint32_t(entt::to_integral(target)),
                        damage, lethal);
    const char* label = subworld_attacker_label(reg, target);
    char status[96]{};
    std::snprintf(status, sizeof(status), "You %s %s for %d",
                  lethal ? "killed" : "hit",
                  label,
                  std::max(0, int(std::round(damage))));
    set_status(status);
    playerAttackTimer_ = kPlayerMeleeCooldown;

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

bool SubworldEngine::has_hostile_near_player(float radius) const {
    if (!ecs_) return false;
    auto& reg = ecs_->reg;
    const float r2 = radius * radius;
    auto view = reg.view<ecs::Position, ecs::Health, ecs::SubworldTag>(
        entt::exclude<ecs::Dead>);
    for (auto e : view) {
        if (!hostile_to_player_entity(reg, e, gs_)) continue;
        const auto& p = view.get<ecs::Position>(e);
        if (dist2(p.x, p.y, playerX_, playerY_) <= r2) return true;
    }
    return false;
}

DangerLevel SubworldEngine::danger_level() const {
    if (!active_ || !ecs_) return DangerLevel::Green;
    auto& reg = ecs_->reg;
    constexpr float kMeleeRange = 40.0f;
    constexpr float kMeleeRange2 = kMeleeRange * kMeleeRange;
    const float detection2 = kDetectionRadius * kDetectionRadius;
    bool found = false;
    float minD2 = detection2;
    auto view = reg.view<ecs::Position, ecs::Health, ecs::SubworldTag>(
        entt::exclude<ecs::Dead>);
    for (auto e : view) {
        if (!hostile_to_player_entity(reg, e, gs_)) continue;
        const auto& p = view.get<ecs::Position>(e);
        const float d2 = dist2(p.x, p.y, playerX_, playerY_);
        if (d2 <= detection2 && (!found || d2 < minD2)) {
            found = true;
            minD2 = d2;
        }
    }
    if (!found) return DangerLevel::Green;
    return minD2 <= kMeleeRange2 ? DangerLevel::Red : DangerLevel::Yellow;
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
        const float d2 = dist2(p.x, p.y, playerX_, playerY_);
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
                                       const ecs::NpcInventory* inventoryOverride,
                                       const ecs::NpcTraits* traitsOverride,
                                       const ecs::NpcCharacter* characterOverride) {
    if (!active_ || !ecs_) return false;

    auto& reg = ecs_->reg;
    const NPCType type = npc_type_from_token(npcTypeId);
    const NpcTypeDef& def = npc_def(type);
    const int lvl = normalize_soldier_level(std::max(level, def.baseLevel));
    const float levelMul = 1.0f + float(std::max(0, lvl - 1)) * 0.08f;

    Rng rng(seed ^ string_hash(npcTypeId) ^ string_hash(displayName));
    const auto& tiles = mgr_.tiles();
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);
    float fx = playerX_;
    float fy = playerY_;
    bool placed = false;
    for (int attempt = 0; attempt < 24; ++attempt) {
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

    auto e = reg.create();
    reg.emplace<ecs::Position>(e, fx, fy);
    reg.emplace<ecs::VisualPos>(e, fx, fy, 48.0f);
    reg.emplace<ecs::NPCKind>(
        e, ecs::NPCKind{std::uint16_t(type), std::uint16_t(3)});
    const float hp = float(def.combat.hp) * levelMul;
    reg.emplace<ecs::Health>(e, hp, hp);
    reg.emplace<ecs::Combat>(e,
        float(def.combat.damage) * levelMul,
        def.combat.speed,
        def.combat.attackRange,
        def.combat.cooldown,
        0.0f,
        def.combat.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                         : ecs::Combat::Melee);
    maybe_emplace_missile_attack(reg, e, def.combat);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(lvl));
    reg.emplace<ecs::Active>(e);
    reg.emplace<ecs::SubworldTag>(e);
    reg.emplace<ecs::SubworldAi>(e, ecs::SubworldAi::Combat,
        0.0f, 0.0f, 0.0f, def.combat.speed * 0.40f, 0.8f);

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

    char msg[160]{};
    std::snprintf(msg, sizeof(msg), "Encounter spawned: %s",
                  displayName && displayName[0] ? displayName : def.label);
    set_status(msg);
    return true;
}

void SubworldEngine::tick_subworld_combat(float dt) {
    if (!ecs_ || dt <= 0.0f) return;
    auto& reg = ecs_->reg;
    std::array<entt::entity, kMaxSubworldCombatActors> actors{};
    int actorCount = 0;
    auto actorView = reg.view<ecs::Position, ecs::Health, ecs::Combat,
                              ecs::SubworldTag>(entt::exclude<ecs::Dead>);
    for (auto e : actorView) {
        if (actorCount >= kMaxSubworldCombatActors) break;
        actors[std::size_t(actorCount++)] = e;
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
        c.cooldownTimer = c.cooldown;
        if (hp->hp <= 0.0f && !reg.any_of<ecs::Dead>(target)) {
            reg.emplace<ecs::Dead>(target);
            if (bus_) {
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

    for (int i = 0; i < actorCount; ++i) {
        const entt::entity e = actors[std::size_t(i)];
        if (!reg.valid(e) || !alive_subworld_entity(reg, e)) continue;
        auto& p = reg.get<ecs::Position>(e);
        auto& c = reg.get<ecs::Combat>(e);
        const bool owned = reg.any_of<ecs::PlayerSoldierTag>(e);
        if (!owned) {
            if (const auto* ai = reg.try_get<ecs::SubworldAi>(e)) {
                if (ai->kind == ecs::SubworldAi::Flee) continue;
            }
        }
        entt::entity target = entt::null;
        float bestD2 = kDetectionRadius * kDetectionRadius;

        if (owned) {
            for (int j = 0; j < actorCount; ++j) {
                const entt::entity other = actors[std::size_t(j)];
                if (other == e || !reg.valid(other)
                    || !hostile_to_player_entity(reg, other, gs_)) {
                    continue;
                }
                const auto& op = reg.get<ecs::Position>(other);
                const float d2 = dist2(p.x, p.y, op.x, op.y);
                if (d2 < bestD2) {
                    bestD2 = d2;
                    target = other;
                }
            }
            if (target == entt::null) continue;
        } else {
            if (!hostile_to_player_entity(reg, e, gs_)) continue;
            for (int j = 0; j < actorCount; ++j) {
                const entt::entity other = actors[std::size_t(j)];
                if (other == e || !reg.valid(other)) continue;
                if (!reg.any_of<ecs::PlayerSoldierTag>(other)) continue;
                const auto& op = reg.get<ecs::Position>(other);
                const float d2 = dist2(p.x, p.y, op.x, op.y);
                if (d2 < bestD2) {
                    bestD2 = d2;
                    target = other;
                }
            }
        }

        float tx = playerX_;
        float ty = playerY_;
        float targetRadius = 4.0f;
        if (target != entt::null) {
            const auto& tp = reg.get<ecs::Position>(target);
            tx = tp.x;
            ty = tp.y;
            targetRadius = target_radius(reg, target);
        } else if (owned) {
            continue;
        }

        const float d = std::sqrt(dist2(p.x, p.y, tx, ty)) + 0.0001f;
        const float attackRange = c.attackRange + targetRadius;
        if (d > attackRange) {
            const float step = c.speed * dt;
            p.x = std::clamp(p.x + (tx - p.x) / d * step, 1.0f, float(kFullSize - 2));
            p.y = std::clamp(p.y + (ty - p.y) / d * step, 1.0f, float(kFullSize - 2));
            if (auto* ai = reg.try_get<ecs::SubworldAi>(e)) {
                ai->vx = (tx - p.x) / d * c.speed;
                ai->vy = (ty - p.y) / d * c.speed;
            }
            continue;
        }

        if (c.cooldownTimer > 0.0f) continue;
        if (c.kind == ecs::Combat::Missile) {
            spawn_npc_missile(reg, e, p, c, tx, ty, d);
            c.cooldownTimer = c.cooldown;
            continue;
        }
        if (target != entt::null) {
            strike(e, target, c, owned);
        } else if (!owned && gs_) {
            const int damage = std::max(1, int(std::round(c.damage)));
            const int hpBefore = gs_->player.combatStats.currentHp;
            const bool lethal = hpBefore > 0 && hpBefore - damage <= 0;
            gs_->player.combatStats.currentHp = std::max(
                0, hpBefore - damage);
            char msg[160]{};
            std::snprintf(msg, sizeof(msg), "Hit by %s for %d (%s %.0fm)",
                          subworld_attacker_label(reg, e),
                          damage,
                          compass_from_delta(p.x - playerX_, p.y - playerY_),
                          std::max(0.0f, d));
            set_status(msg);
            char logMsg[96]{};
            std::snprintf(logMsg, sizeof(logMsg), "%s %s you for %d",
                          subworld_attacker_label(reg, e),
                          lethal ? "killed" : "hit",
                          damage);
            push_combat_log(logMsg);
            c.cooldownTimer = c.cooldown;
        }
    }
}

void SubworldEngine::resolve_projectile_hits_player() {
    if (!ecs_ || !gs_) return;
    auto& reg = ecs_->reg;
    std::array<entt::entity, kMaxSubworldEntityReaps> reaps{};
    int reapCount = 0;
    auto view = reg.view<ecs::Position, ecs::Projectile>();
    for (auto e : view) {
        const auto& pos = view.get<ecs::Position>(e);
        const auto& p = view.get<ecs::Projectile>(e);
        if (p.ownerId == 0u || p.visualOnly || p.damage <= 0.0f) continue;
        const entt::entity owner = entt::entity(p.ownerId);
        if (!reg.valid(owner) || !hostile_to_player_entity(reg, owner, gs_)) {
            continue;
        }
        const float r = p.radius + kPlayerCollisionRadius;
        if (dist2(pos.x, pos.y, playerX_, playerY_) > r * r) continue;

        const int damage = std::max(1, int(std::round(p.damage)));
        const int hpBefore = gs_->player.combatStats.currentHp;
        const bool lethal = hpBefore > 0 && hpBefore - damage <= 0;
        gs_->player.combatStats.currentHp = std::max(0, hpBefore - damage);

        const char* label = reg.valid(owner)
            ? subworld_attacker_label(reg, owner)
            : "Hostile";
        char status[160]{};
        std::snprintf(status, sizeof(status), "Hit by %s for %d (%s %.0fm)",
                      label,
                      damage,
                      compass_from_delta(pos.x - playerX_, pos.y - playerY_),
                      std::sqrt(dist2(pos.x, pos.y, playerX_, playerY_)));
        set_status(status);
        char logMsg[96]{};
        std::snprintf(logMsg, sizeof(logMsg), "%s %s you for %d",
                      label, lethal ? "killed" : "hit", damage);
        push_combat_log(logMsg);

        if (reapCount < kMaxSubworldEntityReaps) {
            reaps[std::size_t(reapCount++)] = e;
        }
    }
    for (int i = 0; i < reapCount; ++i) {
        const entt::entity e = reaps[std::size_t(i)];
        if (reg.valid(e)) reg.destroy(e);
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
                }
                gs_->player.levelData.exp += xp;
                while (try_level_up(gs_->player.levelData)) {}
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
            if (inv.stacks.empty()) {
                if (kind && kind->type < std::uint16_t(NPCType::Count)) {
                    auto stacks = generate_npc_inventory(int(kind->type), lvl, &loot_rng_f01);
                    for (const ItemStack& s : stacks) inv.add(s.id, s.count);
                } else if (kind) {
                    auto stacks = generate_fauna_loot(faction_id_for_kind(kind),
                                                      lvl, &loot_rng_f01);
                    for (const ItemStack& s : stacks) inv.add(s.id, s.count);
                }
            }
            const char* factionId = faction_id_for_kind(kind);
            const int gold = generate_loot_gold(lvl, factionId, &loot_rng_f01);
            gLootRng = nullptr;

            if (pos && (gold > 0 || !inv.stacks.empty())) {
                auto corpse = reg.create();
                reg.emplace<ecs::Position>(corpse, pos->x, pos->y);
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
        sync_macro_player_to_center();
    }
    if (ecs_) {
        clear_subworld_entities(*ecs_);
    }
    active_ = false;
    upload3dDirty_ = false;
    gs_ = nullptr;
    terrain_ = nullptr;
    features_ = nullptr;
    ecs_ = nullptr;
    bus_ = nullptr;
    zones_ = nullptr;
    playerFlying_ = false;
    playerAttackHeld_ = false;
    flightCamY_ = 0.0f;
    playerAttackTimer_ = 0.0f;
    statusLine_.clear();
    statusTimer_ = 0.0f;
    combatLogCount_ = 0;
}

void SubworldEngine::set_flying(bool enabled) {
    if (!active_) {
        playerFlying_ = false;
        flightCamY_ = 0.0f;
        return;
    }

    if (enabled && !playerFlying_) {
        flightCamY_ = renderer3d_.sample_height_m(playerX_, playerY_) + kCameraEyeM;
    }
    if (!enabled) {
        flightCamY_ = 0.0f;
    }
    playerFlying_ = enabled;
}

void SubworldEngine::move_player(float dx, float dy) {
    if (!active_) return;
    // First-person walk: dy = forward (UP arrow / W), dx = strafe right.
    // Camera-forward in tile-XY plane is (cos yaw, sin yaw); right is its
    // 90° rotation (-sin yaw, cos yaw). Compose world delta from those
    // basis vectors so UP always means "into the screen".
    const float cy = std::cos(cam_.yaw), sy = std::sin(cam_.yaw);
    if (playerFlying_) {
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
    int prevCx = mgr_.center_cx(), prevCy = mgr_.center_cy();
    const auto seamStart = Clock::now();
    mgr_.check_boundary(playerX_, playerY_);
    const SeamTiming timing = mgr_.last_seam_timing();
    const bool centerChanged = prevCx != mgr_.center_cx() || prevCy != mgr_.center_cy();
    const bool compositeDirty = mgr_.consume_composite_dirty();
    if (centerChanged) {
        sync_macro_player_to_center();
        // Re-populate the newly centred cell: fauna + settlement citizens.
        // Without this, crossing a seamless boundary into a city spawned
        // nobody. The player squad is preserved by respawn's clear step.
        respawn_npcs_for_center();
    }

    double upload3dMs = 0.0;
    if (compositeDirty) {
        upload3dDirty_ = true;
    }
    if (upload3dDirty_) {
        auto t0 = Clock::now();
        renderer3d_.upload(mgr_);
        auto t1 = Clock::now();
        upload3dMs = elapsed_ms(t0, t1);
        upload3dDirty_ = false;
    }

    if (timing.crossed && seam_trace_enabled()) {
        const double totalMs = elapsed_ms(seamStart, Clock::now());
        std::fprintf(stderr,
            "[seam-cross] gen=%.3fms smooth=%.3fms upload3d=%.3fms total=%.3fms\n",
            timing.genMs, timing.smoothMs, upload3dMs, totalMs);
        std::fflush(stderr);
    }

    if (ecs_) {
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
                               this);
        resolve_projectile_hits_player();
        tick_hit_flashes(dt);
        resolve_subworld_deaths();
    }
}

void SubworldEngine::render(int w, int h) {
    if (!active_) return;
    if (upload3dDirty_) {
        renderer3d_.upload(mgr_);
        upload3dDirty_ = false;
    }
    if (gs_) {
        // Sky as celestial sphere — view ray reconstructed from camera in
        // shader, so rotating the camera does not rotate the sky.
        const float fogR = 0.62f, fogG = 0.72f, fogB = 0.84f; // matches horizDay
        sky_.render(w, h, gs_->worldTime, cam_, elapsed_,
                    gs_->worldSeed, fogR, fogG, fogB);
    }
    if (gs_) {
        // Sync camera to player tile position with eye height above terrain.
        float wx = 0, wz = 0;
        Renderer3D::tile_to_world(playerX_, playerY_, wx, wz);
        float groundM = renderer3d_.sample_height_m(playerX_, playerY_);
        const float groundEyeM = groundM + kCameraEyeM;
        if (playerFlying_) {
            flightCamY_ = std::clamp(
                flightCamY_, groundEyeM, groundEyeM + kFlightMaxAboveGroundM);
            cam_.pos = {wx, flightCamY_, wz};
        } else {
            flightCamY_ = groundEyeM;
            cam_.pos = {wx, groundEyeM, wz};
        }
        // Visual water plane = `WATER_LEVEL` (single source of truth in
        // `base_generator.h`). The same constant drives heightmap remap
        // (water cells map to [0, WATER_LEVEL] via squared deep-ocean
        // curve; land cells map to [WATER_LEVEL + kLandMargin, 1.0] via
        // linear lift), structure culling in `renderer_3d`, and the
        // visible water surface here. Keeping these aligned eliminates
        // the "shore submerged" / "land below water" artefacts: every
        // land pixel sits at least `kLandMargin` above the plane after
        // bilinear blend, every water pixel sits at most WATER_LEVEL.
        const bool hasteAura =
            spellbook_has_sustained(gs_->player.spellBook, "haste");
        const bool flightAura =
            playerFlying_
            || spellbook_has_sustained(gs_->player.spellBook, "flight");
        renderer3d_.render(w, h, cam_, gs_->worldTime, WATER_LEVEL, &mgr_, ecs_,
                           hasteAura, flightAura, playerX_, playerY_,
                           elapsed_);
    }
}

} // namespace sm::sub
