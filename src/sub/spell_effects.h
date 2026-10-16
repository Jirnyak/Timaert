// Native spell projectile/effect ticking for subworld ECS entities.
#pragma once

#include <cstdint>
#include <limits>
#include "events/event_bus.h"
#include "sub/caps.h"       // kMaxSubObjects — длина буфера кандидатов В ТИПЕ
#include "sub/damage.h"
#include "sub/objects.h"    // SubObjects — арена сцены, явный параметр (ломоть E)

namespace sm { struct MacroStore; }

namespace sm::sub {

// Груз всех трёх швов ниже — СЛОТ арены (ступень (iv) ломтя 7): всё
// внутритиковое, ID не нужен — голый u16 законен внутри тика (objects.h).
using SpellDamageLogFn = void (*)(void* user,
                                  std::uint16_t targetSlot,
                                  int damage,
                                  bool lethal);
using SpellCanHitFn = bool (*)(void* user,
                               const ecs::Projectile& projectile,
                               std::uint16_t targetSlot);

// Broad-phase candidate enumeration — the battle bucket grids, reached through
// the engine the same way height and solidity are. Fills `out` with the arena
// SLOTS whose bodies' CENTRES may lie within `r` of (x, y), and returns how
// many. The promise is a SUPERSET: extras are harmless (the exact 3D checks in
// this TU are the only filter that decides hits), a MISS is a bug — so the
// provider pads the query with everything the caller cannot know: the fattest
// body radius in the crowd and the largest per-tick step (the grid is built
// before steering moves bodies). The caller asks in terms of its own geometry
// alone.
//
// ПОЛНОТА ОБЕЩАНА ВСЕГДА (M-261). Что из этого держит ТИП, а что только
// контракт, — замерено мутацией, и разница названа честно:
//   • «мой буфер короче капа» — НЕСОБИРАЕМО, и это настоящая стена: `out`
//     есть ССЫЛКА НА МАССИВ длиной ровно в кап, так что указатель или
//     короткий массив дают `error: incompatible function pointer types`
//     с обеими длинами в тексте (проверено: буфер [64] → сборка красная).
//     Отдельного числа длины у двери больше нет — кап читают обе стороны из
//     одного объявления, второй копии не существует;
//   • «не обещаю полноты» — снято из КОНТРАКТА и из всех провайдеров дерева
//     (счёт беззнаковый, сентинела у него нет), но СТЕНОЙ это НЕ является:
//     `return -1;` из такой функции есть законное сужение, и под
//     `-Wall -Wextra -Werror` оно проходит МОЛЧА (проверено мутацией: 0
//     диагностик) — `-Wsign-conversion` в проекте не включён. Поэтому
//     провайдер, вернувший −1, отдал бы счёт 4 294 967 295 и увёл
//     потребителя за буфер. Стража на это НЕТ сознательно: охранять
//     невозможное значило бы вернуть ту же мёртвую ветку, которую этот
//     наряд и снёс. Держит здесь РЕВЬЮ, и это сказано вслух, а не выдано
//     за типовую гарантию.
//
// Прежде дверь умела ответить -1 («полноту не обещаю»), и ОБА её входа были
// МЕРТВЫ по построению — один кап `kMaxSubObjects` обслуживает И арену, И
// толпу, И этот буфер разом, а слот у каждого тела СВОЙ (`alloc` отказывает
// на полной арене, и все четыре двери рождения отказываются без слота), так
// что ни сбор боя не усекался, ни кандидаты не могли перерасти буфер.
// Усечка жила только комментарием — и уводила потребителя в полный скан,
// которого он никогда не делал.
//
// Null = сетки нет ВОВСЕ (безголовые свидетели) — полный скан арены. Это
// честное ЗНАЧЕНИЕ, а не усечка: «не спрашивал» против «спросил и, может,
// соврал». Остаток долга назван вслух в M-261.
using SpellNeighborsFn =
    std::uint32_t (*)(void* user, float x, float y, float r,
                      std::uint16_t (&out)[kMaxSubObjects]);

// Transient-VFX seam for the spell tick. `event` selects what happened; the
// engine turns it into a particle burst (this TU stays renderer/particle-free,
// exactly like SpellDamageLogFn keeps it log-free). `boltSlot` is ALWAYS the
// bolt's arena slot (its columns are intact at every call site — the reap
// frees the slot only at tick end) so the engine can read its Sprite tint and
// colour the effect. `ax,ay`→`bx,by` is the tile-space segment the bolt
// travelled this tick (Trail); for Impact `bx,by` is the detonation point and
// `ax,ay` == `bx,by`. `blastRadius` is the bolt's (0 for a point hit) — the
// engine picks the burst archetype from it.
enum class SpellFxEvent : std::uint8_t { Trail = 0, Impact = 1 };
using SpellFxEmitFn = void (*)(void* user,
                               SpellFxEvent event,
                               std::uint16_t boltSlot,
                               float ax, float ay, float az,
                               float bx, float by, float bz,
                               float blastRadius);

// «На стороне игрока ли владелец снаряда» — закон атрибуции репутации и
// лога (iii-б): владелец — ссылка сцены парой {ownerSlot, ownerId}, судит
// её ТОЛЬКО ref_slot (мёртвый кастер и перерождённый жилец равно «ничей»).
// Форма — АРЕНА, не реестр: заголовок чист от entt (храповик M-188).
bool projectile_owner_is_player_side(const SubObjects& objs,
                                     const ecs::Projectile& p);

// ШИРОКАЯ ФАЗА — ПАРАМЕТР ОБЯЗАТЕЛЬНЫЙ, И ЭТО ВЕСЬ СМЫСЛ ЕЁ МЕСТА В СПИСКЕ
// (M-261). Стояла она в умолчанном хвосте со значением `nullptr`, и это
// значение включало ПОЛНЫЙ ПРОХОД КАПА (65536 слотов) внутри тика — ровно
// то, что запрещает граница O(N) (§6). Убрать одно умолчание из середины
// хвоста нельзя (в C++ умолчания только хвостовые), поэтому дверь встала
// ТРЕТЬИМ аргументом, к обязательным: теперь «забыл подать сетку» не
// собирается, а не «не случается». Свидетелям перечислитель даёт фикстура
// (`all_live_slots@tests/scene_objects_fixture.h`).
// `objs` — арена сцены, `st` — гладкая память макро-сквадов (ломоть E:
// ctx-мосты умерли, оба идут явно). st nullable: рана без записи ложится
// на колонку арены (фолбэк slot_pools_of — безголовые свидетели).
void tick_spell_projectiles(SubObjects& objs,
                            MacroStore* st,
                            EventBus* bus,
                            float dt,
                            SpellNeighborsFn neighborsFn,
                            void* neighborsUser,
                            SpellDamageLogFn logFn = nullptr,
                            void* logUser = nullptr,
                            SpellCanHitFn canHitFn = nullptr,
                            void* canHitUser = nullptr,
                            SpellFxEmitFn fxFn = nullptr,
                            void* fxUser = nullptr,
                            float (*heightFn)(void*, float, float) = nullptr,
                            void* heightUser = nullptr,
                            // Solid-structure test (sub/collide.h via the
                            // engine): bolts detonate on walls/houses exactly
                            // like they detonate on terrain. Optional — null
                            // keeps structures transparent (headless tests).
                            bool (*solidFn)(void*, float, float, float) = nullptr,
                            void* solidUser = nullptr,
                            // THE LID of the 3×3 box. The window is a closed
                            // volume: four walls in XY and a floor of terrain
                            // and masonry were always checked, but the top was
                            // open, so a bolt aimed at the sky flew straight
                            // (projectiles carry no gravity) until its life ran
                            // out. That made the world ANISOTROPIC — bounded
                            // sideways, endless upward — and left the lifetime
                            // as the only thing standing in for geometry.
                            // Same surface flying bodies are clamped to, so the
                            // window has ONE ceiling. Infinity = open sky, which
                            // is the old behaviour and what headless tests with
                            // no renderer get.
                            float ceilingM
                                = std::numeric_limits<float>::infinity());

} // namespace sm::sub
