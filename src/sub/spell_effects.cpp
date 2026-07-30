#include "sub/spell_effects.h"

#include "ecs/components.h"
#include "sub/base_generator.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace sm::sub {

namespace {

constexpr int kMaxSpellReaps = 512;
constexpr float kHitFlashDuration = 0.15f;
constexpr std::uint32_t kSpellEventIdMask = std::uint32_t{2147483647};

// Combat hit radius for entity `e`. Twin of the copy in sub/engine.cpp (melee);
// keep the two in lockstep. Prefer an explicit BodyRadius, then the AI mover's
// radius, then the billboard's scale, then a coarse fallback for anything that
// declares none of those.
float target_radius(const entt::registry& reg, entt::entity e) {
    if (const auto* br = reg.try_get<ecs::BodyRadius>(e)) return br->radius;
    if (const auto* ai = reg.try_get<ecs::SubworldAi>(e)) return ai->radius;
    if (const auto* sp = reg.try_get<ecs::Sprite>(e)) return sp->scale;
    return 6.0f;
}

// Player-side ownership is decided purely by the owner entity's tags — the
// old `ownerId == 0` sentinel is gone (Inc 4d): player-cast projectiles carry
// the real player entity id, exactly like NPC missiles carry their firer's.
bool projectile_owner_is_player_side(const entt::registry& reg,
                                     const ecs::Projectile& p) {
    const entt::entity owner = entt::entity(p.ownerId);
    return reg.valid(owner)
        && reg.any_of<ecs::PlayerTag, ecs::PlayerSoldierTag>(owner);
}

bool is_spell_target(const entt::registry& reg, entt::entity e,
                     const ecs::Projectile& p,
                     SpellCanHitFn canHitFn,
                     void* canHitUser) {
    // No owner self-exclusion (Inc 4d): a projectile carries no immunity for
    // its own caster. Everyone can hit everyone, the caster included — the
    // caster is kept off its OWN muzzle purely by spawn geometry
    // (caster_spawn_offset / the NPC muzzle offset), and its own AoE blast
    // still catches it if it stands in the blast.
    if (!reg.any_of<ecs::Health>(e)) return false;
    if (reg.any_of<ecs::Dead>(e)) return false;
    if (reg.any_of<ecs::Projectile>(e)) return false;
    if (!reg.any_of<ecs::SubworldTag>(e) && !reg.any_of<ecs::PlayerTag>(e)) {
        return false;
    }
    // NO faction shield (owner design decision 2026-07-30): projectiles and
    // spells are faction-agnostic — they strike whoever stands in their path,
    // ally or enemy. Friendly fire is real; formations must respect their own
    // archers. (Projectile.friendlyFire survives as the AoE-blast marker the
    // spell tables use — it no longer gates WHO can be hit.)
    if (canHitFn && !canHitFn(canHitUser, p,
                              std::uint32_t(entt::to_integral(e)))) {
        return false;
    }
    return true;
}

void queue_reap(std::array<entt::entity, kMaxSpellReaps>& reaps,
                int& reapCount,
                entt::entity e) {
    if (reapCount >= kMaxSpellReaps) return;
    for (int i = 0; i < reapCount; ++i) {
        if (reaps[std::size_t(i)] == e) return;
    }
    reaps[std::size_t(reapCount++)] = e;
}

void emit_npc_death(EventBus* bus, entt::entity e,
                    const ecs::Projectile& p,
                    const ecs::NPCKind* kind) {
    if (!bus || !kind) return;
    GameEvent ev{EventTag::NpcDeath};
    ev.a = std::uint32_t(entt::to_integral(e));
    ev.b = p.ownerId;
    ev.ix = int(kind->type);
    ev.iy = int(p.spellId & kSpellEventIdMask);
    bus->emit(ev);
}

void apply_spell_damage(ecs::World& w,
                        std::array<entt::entity, kMaxSpellReaps>& reaps,
                        int& reapCount,
                        EventBus* bus,
                        entt::entity target,
                        const ecs::Projectile& p,
                        float damage,
                        SpellDamageLogFn logFn,
                        void* logUser,
                        SpellCanHitFn canHitFn,
                        void* canHitUser) {
    (void)reaps;
    (void)reapCount;
    if (damage <= 0.0f || !w.reg.valid(target)) return;
    if (!is_spell_target(w.reg, target, p, canHitFn, canHitUser)) return;
    auto* hp = w.reg.try_get<ecs::Health>(target);
    if (!hp) return;
    const bool playerOwned = projectile_owner_is_player_side(w.reg, p);
    const bool lethal = hp->hp > 0.0f && hp->hp - damage <= 0.0f;
    w.reg.emplace_or_replace<ecs::LastHit>(target, p.ownerId, playerOwned);
    w.reg.emplace_or_replace<ecs::HitFlash>(
        target, ecs::HitFlash{kHitFlashDuration});
    // Universal impact-VFX marker (Inc C): a pure-ECS tag, so this renderer-free
    // TU stays free of particle types (mirrors HitFlash). The engine's
    // tick_damage_fx drains it into a blood/dust burst the same tick. A spell
    // that lands on flesh bleeds exactly like a melee hit — one path, no per-
    // spell code.
    w.reg.emplace_or_replace<ecs::DamageFx>(target, ecs::DamageFx{lethal});
    hp->hp -= damage;
    if (playerOwned && logFn
        && !w.reg.any_of<ecs::PlayerTag, ecs::PlayerSoldierTag>(target)) {
        logFn(logUser, std::uint32_t(entt::to_integral(target)),
              damage, lethal);
    }
    if (hp->hp <= 0.0f && !w.reg.any_of<ecs::Dead>(target)) {
        w.reg.emplace<ecs::Dead>(target);
        emit_npc_death(bus, target, p, w.reg.try_get<ecs::NPCKind>(target));
    }
}

entt::entity find_projectile_hit(ecs::World& w,
                                 entt::entity projectile,
                                 const ecs::Position& pos,
                                 const ecs::Projectile& p,
                                 SpellCanHitFn canHitFn,
                                 void* canHitUser) {
    auto targets = w.reg.view<ecs::Position, ecs::Health>(
        entt::exclude<ecs::Dead>);
    for (auto e : targets) {
        if (e == projectile) continue;
        if (!is_spell_target(w.reg, e, p, canHitFn, canHitUser)) continue;
        const auto& tp = targets.get<ecs::Position>(e);
        const float r = p.radius + target_radius(w.reg, e);
        const float dx = tp.x - pos.x;
        const float dy = tp.y - pos.y;
        const float dz = tp.z - pos.z;
        if (dx * dx + dy * dy + dz * dz <= r * r) return e;
    }
    return entt::null;
}

void apply_spell_blast(ecs::World& w,
                       std::array<entt::entity, kMaxSpellReaps>& reaps,
                       int& reapCount,
                       EventBus* bus,
                       const ecs::Position& pos,
                       const ecs::Projectile& p,
                       SpellDamageLogFn logFn,
                       void* logUser,
                       SpellCanHitFn canHitFn,
                       void* canHitUser) {
    if (p.blastRadius <= 0.0f) return;
    auto targets = w.reg.view<ecs::Position, ecs::Health>(
        entt::exclude<ecs::Dead>);
    for (auto e : targets) {
        if (!is_spell_target(w.reg, e, p, canHitFn, canHitUser)) continue;
        const auto& tp = targets.get<ecs::Position>(e);
        const float dx = tp.x - pos.x;
        const float dy = tp.y - pos.y;
        const float dz = tp.z - pos.z;
        const float r = p.blastRadius;
        if (dx * dx + dy * dy + dz * dz <= r * r) {
            apply_spell_damage(w, reaps, reapCount, bus, e, p, p.damage,
                               logFn, logUser, canHitFn, canHitUser);
        }
    }
}

void apply_spell_beam(ecs::World& w,
                      std::array<entt::entity, kMaxSpellReaps>& reaps,
                      int& reapCount,
                      EventBus* bus,
                      const ecs::Position& beamPos,
                      const ecs::Projectile& p,
                      SpellDamageLogFn logFn,
                      void* logUser,
                      SpellCanHitFn canHitFn,
                      void* canHitUser) {
    if (p.beamLength <= 0.0f || p.damage <= 0.0f) return;
    const float len = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
    if (len <= 0.001f) return;
    const float nx = p.vx / len;
    const float ny = p.vy / len;
    const float nz_b = p.vz / len;
    auto targets = w.reg.view<ecs::Position, ecs::Health>(
        entt::exclude<ecs::Dead>);
    for (auto e : targets) {
        if (!is_spell_target(w.reg, e, p, canHitFn, canHitUser)) continue;
        const auto& tp = targets.get<ecs::Position>(e);
        const float dx = tp.x - p.originX;
        const float dy = tp.y - p.originY;
        // Beam origin Z: the beam entity's position is at the beam midpoint,
        // so the origin Z = beamPos.z - nz_b * beamLength/2.
        const float originZ = beamPos.z - nz_b * (p.beamLength * 0.5f);
        const float dz = tp.z - originZ;
        const float along = dx * nx + dy * ny + dz * nz_b;
        if (along < 0.0f || along > p.beamLength) continue;
        const float px = dx - nx * along;
        const float py = dy - ny * along;
        const float pz = dz - nz_b * along;
        const float r = p.radius * 2.0f + target_radius(w.reg, e);
        if (px * px + py * py + pz * pz <= r * r) {
            apply_spell_damage(w, reaps, reapCount, bus, e, p, p.damage,
                               logFn, logUser, canHitFn, canHitUser);
        }
    }
}

bool already_chained(const std::array<entt::entity, 8>& chainHits,
                     int hitCount,
                     entt::entity e) {
    for (int i = 0; i < hitCount; ++i) {
        if (chainHits[std::size_t(i)] == e) return true;
    }
    return false;
}

void apply_spell_chain(ecs::World& w,
                       std::array<entt::entity, kMaxSpellReaps>& reaps,
                       int& reapCount,
                       EventBus* bus,
                       entt::entity first,
                       const ecs::Projectile& p,
                       SpellDamageLogFn logFn,
                       void* logUser,
                       SpellCanHitFn canHitFn,
                       void* canHitUser) {
    if (p.chainRemaining <= 0 || p.chainDecay <= 0.0f || p.chainRadius <= 0.0f) {
        return;
    }
    std::array<entt::entity, 8> chainHits{};
    int hitCount = 0;
    chainHits[std::size_t(hitCount++)] = first;

    entt::entity current = first;
    float damage = p.damage * p.chainDecay;
    for (int i = 0; i < p.chainRemaining && hitCount < int(chainHits.size()); ++i) {
        if (!w.reg.valid(current)) break;
        const auto* cp = w.reg.try_get<ecs::Position>(current);
        if (!cp) break;

        entt::entity best = entt::null;
        float bestD2 = p.chainRadius * p.chainRadius;
        auto targets = w.reg.view<ecs::Position, ecs::Health>(
            entt::exclude<ecs::Dead>);
        for (auto e : targets) {
            if (!is_spell_target(w.reg, e, p, canHitFn, canHitUser)) continue;
            if (already_chained(chainHits, hitCount, e)) continue;
            const auto& tp = targets.get<ecs::Position>(e);
            const float dx = tp.x - cp->x;
            const float dy = tp.y - cp->y;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= bestD2) {
                bestD2 = d2;
                best = e;
            }
        }
        if (best == entt::null) break;
        apply_spell_damage(w, reaps, reapCount, bus, best, p, damage,
                           logFn, logUser, canHitFn, canHitUser);
        chainHits[std::size_t(hitCount++)] = best;
        current = best;
        damage *= p.chainDecay;
    }
}

} // namespace

void tick_spell_projectiles(ecs::World& w,
                            EventBus* bus,
                            float dt,
                            SpellDamageLogFn logFn,
                            void* logUser,
                            SpellCanHitFn canHitFn,
                            void* canHitUser,
                            SpellFxEmitFn fxFn,
                            void* fxUser,
                            float (*heightFn)(void*, float, float),
                            void* heightUser,
                            bool (*solidFn)(void*, float, float, float),
                            void* solidUser) {
    auto view = w.reg.view<ecs::Position, ecs::Projectile>();
    std::array<entt::entity, kMaxSpellReaps> reaps{};
    int reapCount = 0;

    for (auto e : view) {
        if (!w.reg.valid(e)) continue;
        auto& pos = view.get<ecs::Position>(e);
        auto& p = view.get<ecs::Projectile>(e);

        p.lifeTimer -= dt;
        if (p.lifeTimer <= 0.0f) {
            if (p.kind == ecs::Projectile::Beam) {
                apply_spell_beam(w, reaps, reapCount, bus, pos, p,
                                 logFn, logUser, canHitFn, canHitUser);
            } else if (p.explodeOnExpiry) {
                apply_spell_blast(w, reaps, reapCount, bus, pos, p,
                                  logFn, logUser, canHitFn, canHitUser);
                // Detonated on expiry (only bolts with a blast do this) — burst.
                if (fxFn) fxFn(fxUser, SpellFxEvent::Impact,
                               std::uint32_t(e), pos.x, pos.y, pos.z,
                               pos.x, pos.y, pos.z,
                               p.blastRadius);
            }
            queue_reap(reaps, reapCount, e);
            continue;
        }

        if (p.visualOnly) continue;

        const float prevX = pos.x, prevY = pos.y, prevZ = pos.z;
        pos.x += p.vx * dt;
        pos.y += p.vy * dt;
        pos.z += p.vz * dt;
        const float groundM = heightFn ? heightFn(heightUser, pos.x, pos.y) : 0.0f;
        if (pos.x < 0.0f || pos.y < 0.0f
            || pos.x > float(kFullSize) || pos.y > float(kFullSize)) {
            queue_reap(reaps, reapCount, e);
            continue;
        }

        if (pos.z < groundM) {
            // Hit the ground! Snap to ground level for the blast effect.
            pos.z = groundM;
            if (p.blastRadius > 0.0f) {
                apply_spell_blast(w, reaps, reapCount, bus, pos, p, logFn, logUser, canHitFn, canHitUser);
            }
            queue_reap(reaps, reapCount, e);
            continue;
        }

        if (solidFn && solidFn(solidUser, pos.x, pos.y, pos.z)) {
            // Flew into a solid structure (wall, house, gate lintel): the same
            // honest collision as terrain — blast where it struck, with an
            // impact burst so a bolt dying on masonry flashes like any other.
            if (p.blastRadius > 0.0f) {
                apply_spell_blast(w, reaps, reapCount, bus, pos, p, logFn,
                                  logUser, canHitFn, canHitUser);
            }
            if (fxFn) fxFn(fxUser, SpellFxEvent::Impact, std::uint32_t(e),
                           pos.x, pos.y, pos.z, pos.x, pos.y, pos.z,
                           p.blastRadius);
            queue_reap(reaps, reapCount, e);
            continue;
        }

        // Shed a trail over the segment the bolt just crossed (tinted engine-
        // side by the bolt's own Sprite). Bolts only — beams are visualOnly and
        // returned above.
        if (fxFn) fxFn(fxUser, SpellFxEvent::Trail, std::uint32_t(e),
                       prevX, prevY, prevZ, pos.x, pos.y, pos.z,
                       p.blastRadius);

        const entt::entity hit =
            find_projectile_hit(w, e, pos, p, canHitFn, canHitUser);
        if (hit != entt::null) {
            if (p.blastRadius > 0.0f) {
                apply_spell_blast(w, reaps, reapCount, bus, pos, p,
                                  logFn, logUser, canHitFn, canHitUser);
            } else {
                apply_spell_damage(w, reaps, reapCount, bus, hit, p, p.damage,
                                   logFn, logUser, canHitFn, canHitUser);
                apply_spell_chain(w, reaps, reapCount, bus, hit, p,
                                  logFn, logUser, canHitFn, canHitUser);
            }
            // Impact burst at the hit point (before the bolt is reaped so its
            // Sprite tint is still readable engine-side).
            if (fxFn) fxFn(fxUser, SpellFxEvent::Impact, std::uint32_t(e),
                           pos.x, pos.y, pos.z, pos.x, pos.y, pos.z,
                           p.blastRadius);
            queue_reap(reaps, reapCount, e);
        }
    }

    for (int i = 0; i < reapCount; ++i) {
        const entt::entity e = reaps[std::size_t(i)];
        if (w.reg.valid(e)) w.reg.destroy(e);
    }
}

} // namespace sm::sub
