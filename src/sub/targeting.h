// Pure subworld targeting primitive (Increment 5 possession groundwork).
//
// aim_target() selects the entity a shooter standing at (px,py) and facing
// `yaw` would strike within a forward cone. It generalises the shipped
// player-melee selection (SubworldEngine::tick_player_melee, engine.cpp): the
// candidate set is the live subworld hostiles —
//   живые слоты арены с назначенным родом (body_kind,
//   колонка арены — кусок 1) minus Dead
// — excluding the player's own side (AvatarTag / PlayerSoldierTag, i.e. the
// is_player_side predicate at engine.cpp) and the shooter entity itself. Among
// those it returns the NEAREST candidate that is both within `maxRange` and
// inside the aim cone (cos(angle to target) >= cosHalfAngle).
//
// With cosHalfAngle == -1 the cone is the full circle, so the result matches
// melee's omnidirectional nearest-in-range; a narrower cone (e.g. cos 15° for a
// first-person reticle) restricts to what the shooter is looking at.
//
// The function is deliberately pure and free of SubworldEngine state so it is
// unit-testable in isolation and can back both the first-person reticle and the
// macro `control <id>` debug aim without duplication.
#pragma once
#include "ecs/world.h"   // entt::registry/entity + ecs components (aim_target)
#include "sub/caps.h"    // kMaxSubObjects — длина буфера кандидатов В ТИПЕ
#include "sub/objects.h" // SubObjects — арена сцены (melee_pick_target)

namespace sm::sub {

// Forward direction is the NORMALISED 3D look vector (fx, fy, fz) — the same
// one the projectile flies (casting.h CastContext n*), so the reticle and the
// shot agree by construction. Distance and cone are 3D (CANON S13 «всё в бою
// трёхмерно», session Е 2026-09-19: the XY-flat cone let a body far overhead
// steal a level shot, and a dive-bombing caster could not aim down at all).
// `shooter` is excluded from the candidate set (pass entt::null if there is
// no self entity). Returns entt::null when no live enemy lies within both
// range and cone.
// The player-melee auto-aim (owner ruling 2026-08-05): HOSTILES FIRST — the
// nearest body the `isHostile` oracle confirms wins; only when no hostile is
// in reach does the swing fall back to the nearest body of any stripe, so
// striking a neutral stays possible (and keeps its reputation price) but a
// bystander no longer catches the blow meant for the bandit behind him.
// `isHostile` is a callback because hostility needs GameState (reputation),
// which this pure module deliberately does not know. Груз — СЛОТ арены
// (ступень (iv) ломтя 7): всё внутритиковое, голый u16 законен внутри тика.
using HostileFn = bool (*)(void* user, int slot);

// Broad phase for the swing — the battle pick grid, reached exactly like the
// spell contact reaches it (same shape as SpellNeighborsFn, same promise: a
// SUPERSET of everything within `r` of (x, y), ALWAYS complete; null = no grid
// at all, which means the full scan). Длина буфера есть САМ КАП
// (`kMaxSubObjects@src/sub/caps.h`, ССЫЛКОЙ НА МАССИВ), поэтому числа длины у
// двери нет вовсе — ни параметром, ни второй копией: обе стороны читают одно
// объявление, а короткий буфер НЕСОБИРАЕМ. «Полноту не обещаю» (-1) снято
// вместе со спелловой дверью (M-261) — у счёта нет сентинела; НО это
// контракт, а не стена: границу типовых гарантий этой пары смотри у
// `SpellNeighborsFn@src/sub/spell_effects.h`, там она замерена мутацией.
using MeleeNeighborsFn =
    std::uint32_t (*)(void* user, float x, float y, float r,
                      std::uint16_t (&out)[kMaxSubObjects]);

// `range` is the attacker's reach — ONE number (owner ruling 2026-08-27: a
// mob's row states its own attack radius; a man's will come from his spear
// through the equipment increment — but it is always just this number). The
// blow lands on a target's SURFACE: a candidate is in reach when
// dist − body_radius(target) ≤ range, the same law the NPC strike walks
// (sub/movement.cpp reach + radius[target]) — a troll is struck from farther
// away than a frog because a troll is wider, not because anyone branched.
// Возврат — СЛОТ арены, -1 = в досягаемости никого (ступень (iv): форма
// двери — АРЕНА, не реестр; сущность в выборе цели больше не участвует).
// Широкая фаза ОБЯЗАТЕЛЬНА (M-261): прежнее умолчание `nullptr` включало
// полный проход капа 65536 слотов внутри тика — запрет §6. Свидетелям
// перечислитель даёт `all_live_slots@tests/scene_objects_fixture.h`.
int melee_pick_target(const SubObjects& objs,
                      float px, float py, float pz,
                      float range,
                      HostileFn isHostile, void* user,
                      MeleeNeighborsFn neighborsFn,
                      void* neighborsUser);

entt::entity aim_target(entt::registry& reg,
                        float px, float py, float pz,
                        float fx, float fy, float fz,
                        float maxRange, float cosHalfAngle,
                        entt::entity shooter = entt::null);

} // namespace sm::sub
