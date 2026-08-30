#include "sub/spell_effects.h"

#include "ecs/components.h"
#include "sub/base_generator.h"
#include "sub/body.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace sm::sub {

namespace {

constexpr int kMaxSpellReaps = 512;
// DamageSource::spellId rides into a SIGNED event field (damage.cpp writes it
// as `int(src.spellId)`), so the top bit is masked off to keep that cast in
// range — the mask is INT32_MAX by derivation, not a magic number.
constexpr std::uint32_t kSpellEventIdMask =
    std::uint32_t{std::numeric_limits<std::int32_t>::max()};

// Combat hit radius: THE one in sub/body.h, the same call melee makes. This file
// used to keep a private copy that promised in a comment to stay in lockstep
// with the melee one and did not — it never consulted the monster or NPC tables,
// and its last-resort value was 6.0 against melee's 0.55, so a body was a
// different size depending on the weapon aimed at it.


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

// ── Broad phase ────────────────────────────────────────────────────────────
// The candidate buffer matches the battle snapshot's own ceiling
// (kMaxBodyCrowd = 16384): the grid can never hold more bodies than that,
// so overflow is impossible by construction and the -1 arm below is reserved
// for a broad phase that KNOWS it is incomplete (a truncated gather).
constexpr int kMaxSpellNeighbors = 16384;

// Enumerate every body that MAY matter within `r` of (cx, cy) and hand each
// (entity, position) to `fn`. Three arms, one promise — nobody is missed:
//   • a broad phase that answers: its candidates, a strict superset;
//   • a broad phase that returns -1 ("cannot promise completeness"): the
//     full registry scan, exactly the pre-grid behaviour;
//   • no broad phase at all (headless tests, harnesses): the full scan.
// The exact hit rules — is_spell_target and the 3D distances — stay in the
// callers; this helper decides only WHO gets asked, never who gets hit.
template <typename Fn>
void for_each_spell_candidate(ecs::World& w,
                              SpellNeighborsFn neighborsFn,
                              void* neighborsUser,
                              float cx, float cy, float r,
                              Fn&& fn) {
    if (neighborsFn) {
        // Static: one 64 KiB buffer for the whole single-threaded spell tick,
        // touched only as far as it is filled.
        static std::uint32_t buf[kMaxSpellNeighbors];
        const int n = neighborsFn(neighborsUser, cx, cy, r,
                                  buf, kMaxSpellNeighbors);
        if (n >= 0) {
            for (int i = 0; i < n; ++i) {
                const entt::entity e = entt::entity(buf[std::size_t(i)]);
                if (!w.reg.valid(e)) continue;
                if (const auto* tp = w.reg.try_get<ecs::Position>(e)) {
                    fn(e, *tp);
                }
            }
            return;
        }
    }
    auto targets = w.reg.view<ecs::Position, ecs::Health>(
        entt::exclude<ecs::Dead>);
    for (auto e : targets) fn(e, targets.get<ecs::Position>(e));
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
    const bool playerOwned = projectile_owner_is_player_side(w.reg, p);
    const DamageSource src{p.ownerId, playerOwned,
                           p.spellId & kSpellEventIdMask};
    const DamageResult hit =
        apply_damage(w.reg, target, src, damage, DamageKind::Spell, bus);
    if (hit.applied <= 0.0f) return;
    if (playerOwned && logFn
        && !w.reg.any_of<ecs::PlayerTag, ecs::PlayerSoldierTag>(target)) {
        logFn(logUser, std::uint32_t(entt::to_integral(target)),
              hit.applied, hit.lethal);
    }
}

// Where along the segment A→B the point P comes closest, clamped to the segment.
float segment_closest_t(float ax, float ay, float az,
                        float bx, float by, float bz,
                        float px, float py, float pz) {
    const float dx = bx - ax, dy = by - ay, dz = bz - az;
    const float len2 = dx * dx + dy * dy + dz * dz;
    if (len2 <= 1e-12f) return 0.0f;          // no travel this tick
    const float t = ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / len2;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// SWEPT, not sampled. A projectile does not teleport: it sweeps the segment it
// crossed this tick, and anything whose body the segment grazes is struck.
//
// This used to test a single POINT — the position after the step — and that is
// the bug behind "the closer the enemy, the more the bolt passes through him".
// A magic bolt covers 6.25 units per tick (400 u/s at 1/64 s) while its contact
// radius against an ordinary body is only ~2, so the old test probed at 11.25,
// 17.5, 23.75 … and was blind everywhere in between: a dead zone from the muzzle
// out to ~8.5 units — which swallows the whole melee band, kPlayerMeleeRange
// being 5 — plus a periodic ~0.85-unit gap in every 6.25 further out.
//
// It also made speed a LIABILITY: the faster the spell, the longer its stride
// and the wider its blind gaps. That is backwards, and it would have been
// inherited by every projectile added later.
//
// The segment is the right primitive for the parabolas the owner plans, not an
// obstacle to them: with gravity the path within ONE tick is the chord of a very
// flat arc, and the chord's deviation is g·dt²/8 = 8·(1/64)²/8 ≈ 0.00024 units
// against a contact radius of ~2. Point sampling, by contrast, gets WORSE under
// gravity, because a falling projectile's stride grows.
//
// `skipOwner` closes the last stretch — see the caller. Returns the EARLIEST hit
// along the sweep, so a bolt strikes the body it reaches first rather than
// whichever body the view happened to list first.
entt::entity find_projectile_hit(ecs::World& w,
                                 entt::entity projectile,
                                 float fromX, float fromY, float fromZ,
                                 const ecs::Position& pos,
                                 const ecs::Projectile& p,
                                 entt::entity skipOwner,
                                 SpellCanHitFn canHitFn,
                                 void* canHitUser,
                                 SpellNeighborsFn neighborsFn,
                                 void* neighborsUser) {
    entt::entity best = entt::null;
    float bestT = 2.0f;
    // Broad phase: the circle around the swept segment's 2D midpoint, wide
    // enough for the segment's own half-length plus the bolt's radius. Target
    // body radii and per-tick drift are the PROVIDER's padding (see
    // SpellNeighborsFn) — the ask here is the bolt's geometry alone.
    const float segX = pos.x - fromX, segY = pos.y - fromY;
    const float qr =
        0.5f * std::sqrt(segX * segX + segY * segY) + p.radius;
    for_each_spell_candidate(w, neighborsFn, neighborsUser,
                             (fromX + pos.x) * 0.5f, (fromY + pos.y) * 0.5f, qr,
        [&](entt::entity e, const ecs::Position& tp) {
            if (e == projectile) return;
            if (e == skipOwner) return;
            if (!is_spell_target(w.reg, e, p, canHitFn, canHitUser)) return;
            const float r = p.radius + body_radius(w.reg, e);
            const float t = segment_closest_t(fromX, fromY, fromZ,
                                              pos.x, pos.y, pos.z,
                                              tp.x, tp.y, tp.z);
            const float cx = fromX + (pos.x - fromX) * t;
            const float cy = fromY + (pos.y - fromY) * t;
            const float cz = fromZ + (pos.z - fromZ) * t;
            const float dx = tp.x - cx, dy = tp.y - cy, dz = tp.z - cz;
            if (dx * dx + dy * dy + dz * dz <= r * r && t < bestT) {
                bestT = t;
                best = e;
            }
        });
    return best;
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
                       void* canHitUser,
                       SpellNeighborsFn neighborsFn,
                       void* neighborsUser) {
    if (p.blastRadius <= 0.0f) return;
    // Broad phase: the blast sphere's own 2D shadow. A centre within
    // blastRadius in 3D is within it in 2D — dropping a coordinate never
    // grows a distance — so the circle is a superset of the sphere.
    for_each_spell_candidate(w, neighborsFn, neighborsUser,
                             pos.x, pos.y, p.blastRadius,
        [&](entt::entity e, const ecs::Position& tp) {
            if (!is_spell_target(w.reg, e, p, canHitFn, canHitUser)) return;
            const float dx = tp.x - pos.x;
            const float dy = tp.y - pos.y;
            const float dz = tp.z - pos.z;
            const float r = p.blastRadius;
            if (dx * dx + dy * dy + dz * dz <= r * r) {
                apply_spell_damage(w, reaps, reapCount, bus, e, p, p.damage,
                                   logFn, logUser, canHitFn, canHitUser);
            }
        });
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
                      void* canHitUser,
                      SpellNeighborsFn neighborsFn,
                      void* neighborsUser) {
    if (p.beamLength <= 0.0f || p.damage <= 0.0f) return;
    const float len = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
    if (len <= 0.001f) return;
    const float nx = p.vx / len;
    const float ny = p.vy / len;
    const float nz_b = p.vz / len;
    // Broad phase: the circle around the beam's own 2D projection — midpoint
    // of the projected segment, radius its half-length plus the beam's width.
    // Projection only shrinks distances, so any body the 3D perpendicular
    // test below could accept lies inside this circle; a steep beam projects
    // SHORT and the circle stays honest because the half-length uses the
    // projected direction, not the 3D length.
    const float qx = p.originX + nx * (p.beamLength * 0.5f);
    const float qy = p.originY + ny * (p.beamLength * 0.5f);
    const float qr = 0.5f * p.beamLength * std::sqrt(nx * nx + ny * ny)
                   + p.radius * 2.0f;
    for_each_spell_candidate(w, neighborsFn, neighborsUser, qx, qy, qr,
        [&](entt::entity e, const ecs::Position& tp) {
            if (!is_spell_target(w.reg, e, p, canHitFn, canHitUser)) return;
            const float dx = tp.x - p.originX;
            const float dy = tp.y - p.originY;
            // Beam origin Z: the beam entity's position is at the beam
            // midpoint, so the origin Z = beamPos.z - nz_b * beamLength/2.
            const float originZ = beamPos.z - nz_b * (p.beamLength * 0.5f);
            const float dz = tp.z - originZ;
            const float along = dx * nx + dy * ny + dz * nz_b;
            if (along < 0.0f || along > p.beamLength) return;
            const float px = dx - nx * along;
            const float py = dy - ny * along;
            const float pz = dz - nz_b * along;
            const float r = p.radius * 2.0f + body_radius(w.reg, e);
            if (px * px + py * py + pz * pz <= r * r) {
                apply_spell_damage(w, reaps, reapCount, bus, e, p, p.damage,
                                   logFn, logUser, canHitFn, canHitUser);
            }
        });
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
                       void* canHitUser,
                       SpellNeighborsFn neighborsFn,
                       void* neighborsUser) {
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
        // Broad phase per hop: the XY grid query pre-filters at chainRadius;
        // the victim itself is judged by honest 3D distance (S13 — all
        // distances are 3D), exactly like the blast sphere above.
        for_each_spell_candidate(w, neighborsFn, neighborsUser,
                                 cp->x, cp->y, p.chainRadius,
            [&](entt::entity e, const ecs::Position& tp) {
                if (!is_spell_target(w.reg, e, p, canHitFn, canHitUser)) return;
                if (already_chained(chainHits, hitCount, e)) return;
                const float dx = tp.x - cp->x;
                const float dy = tp.y - cp->y;
                const float dz = tp.z - cp->z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 <= bestD2) {
                    bestD2 = d2;
                    best = e;
                }
            });
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
                            void* solidUser,
                            SpellNeighborsFn neighborsFn,
                            void* neighborsUser,
                            float ceilingM) {
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
                                 logFn, logUser, canHitFn, canHitUser,
                                 neighborsFn, neighborsUser);
            } else if (p.explodeOnExpiry) {
                apply_spell_blast(w, reaps, reapCount, bus, pos, p,
                                  logFn, logUser, canHitFn, canHitUser,
                                  neighborsFn, neighborsUser);
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
        // LEFT THE BOX. Four walls and now the lid, checked together because
        // they are one rule: the 3×3 window is a closed volume and a projectile
        // that leaves it is gone, whichever face it crossed. Nothing detonates
        // out here — there is no world left to detonate on, exactly as when a
        // bolt flies out the side.
        if (pos.x < 0.0f || pos.y < 0.0f
            || pos.x > float(kFullSize) || pos.y > float(kFullSize)
            || pos.z > ceilingM) {
            queue_reap(reaps, reapCount, e);
            continue;
        }

        if (pos.z < groundM) {
            // Hit the ground! Snap to ground level for the blast effect.
            pos.z = groundM;
            if (p.blastRadius > 0.0f) {
                apply_spell_blast(w, reaps, reapCount, bus, pos, p, logFn,
                                  logUser, canHitFn, canHitUser,
                                  neighborsFn, neighborsUser);
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
                                  logUser, canHitFn, canHitUser,
                                  neighborsFn, neighborsUser);
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

        // The bolt's OWN path, swept, with nobody excluded — the caster very much
        // included. Inc 4d's rule is untouched here: a projectile carries no
        // immunity for whoever fired it.
        entt::entity hit =
            find_projectile_hit(w, e, prevX, prevY, prevZ, pos, p,
                                entt::null, canHitFn, canHitUser,
                                neighborsFn, neighborsUser);

        // THE MUZZLE STRETCH, birth tick only. A bolt is born
        // caster_spawn_offset ahead of its caster (1.5 + 1.5 + 2.0 = 5.0 units
        // for a magic bolt) — which is exactly kPlayerMeleeRange, so an enemy in
        // your face stood INSIDE that gap and the bolt was born behind him.
        // Nothing had ever swept the stretch between the hand and the muzzle, so
        // point-blank casting could not connect at all.
        //
        // The caster is excluded from THIS STRETCH ALONE, and that is the whole
        // exception. Note what it is NOT: it does not say "the bolt knows its
        // friends". The stretch is an artificial back-extension we add to cover
        // the spawn gap, and it necessarily passes straight through the caster's
        // own body — so he is not struck BY THE PART WE ADDED. His exposure to
        // the bolt's real path is unchanged, above and on every later tick, and
        // the AoE blast still catches him like anyone else. No factions, no
        // teams, no friend-or-foe. It reads `p.ownerId`, which the projectile
        // already carries for reputation attribution and the death event, so not
        // one byte of new data exists.
        const bool birthTick = p.lifeTimer + dt >= p.maxLifeTimer - 1e-6f;
        const entt::entity owner = entt::entity(p.ownerId);
        if (birthTick && w.reg.valid(owner)) {
            if (const auto* op = w.reg.try_get<ecs::Position>(owner)) {
                // ONLY if this projectile really was born at THIS owner's
                // muzzle. Not every projectile is: armageddon scatters its
                // meteors up to 160 units from the caster while still stamping
                // him as owner, and a spawner may place a bolt anywhere. Without
                // this guard such a projectile would sweep a segment stretching
                // all the way back to its owner and strike everything along it.
                // The bound is the muzzle geometry itself (caster_spawn_offset =
                // casterRadius + projectileRadius + 2), so it holds exactly when
                // the bolt is leaving the hand that cast it.
                const float bx = prevX - op->x;
                const float by = prevY - op->y;
                const float maxBack =
                    body_radius(w.reg, owner) + p.radius + 2.0f;
                if (bx * bx + by * by <= maxBack * maxBack + 0.01f) {
                    // Caster centre → spawn point. Z stays the muzzle's: the
                    // bolt leaves the hand at muzzle height, not at the feet.
                    const ecs::Position spawnPos{prevX, prevY, prevZ};
                    const entt::entity muzzleHit =
                        find_projectile_hit(w, e, op->x, op->y, prevZ,
                                            spawnPos, p, owner,
                                            canHitFn, canHitUser,
                                            neighborsFn, neighborsUser);
                    // This stretch happens BEFORE the travel above, so whatever
                    // it finds is struck first.
                    if (muzzleHit != entt::null) hit = muzzleHit;
                }
            }
        }
        if (hit != entt::null) {
            if (p.blastRadius > 0.0f) {
                apply_spell_blast(w, reaps, reapCount, bus, pos, p,
                                  logFn, logUser, canHitFn, canHitUser,
                                  neighborsFn, neighborsUser);
            } else {
                apply_spell_damage(w, reaps, reapCount, bus, hit, p, p.damage,
                                   logFn, logUser, canHitFn, canHitUser);
                apply_spell_chain(w, reaps, reapCount, bus, hit, p,
                                  logFn, logUser, canHitFn, canHitUser,
                                  neighborsFn, neighborsUser);
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
