// Subworld AI tick — the BRAIN pass. Wander/Flee decide what a body wants
// and write SubworldAi.wantVx/Vy; the ONE mover (driven from
// SubworldEngine) is the ONE owner of every body's position and executes
// that intent with the same separation, solids and terrain as any fighter.
// Nothing here ever writes Position, so nothing can integrate twice in one
// frame.
#pragma once
#include <cstdint>
#include "macro/anketa.h"
#include "sub/objects.h"   // SubObjects — арена сцены, явный параметр (ломоть E)
#include "tables/faction.h"

namespace sm { struct MacroStore; }

namespace sm::sub {

// The ambient "did anything notice me" radius — the SAME quantity as a
// fighter's default sight, read from its one home (macro/army.h
// kNpcSightDefaultM) instead of a second 200 kept here by hand.
constexpr float kDetectionRadius  = kNpcSightDefaultM;
// kHostileThreshold moved to macro/faction.h (Session 15): the macro squad
// threat step needs the same line the battle masks draw, and one number
// cannot live in two headers. Unqualified uses in sm::sub still resolve.
constexpr int   kHitRepPenalty    = -1;

// Ключ — СЛОТ арены ((ii-б) ломтя 7): колбэк зовётся из живого цикла тика,
// голый слот внутри тика законен; entity-ключ был последним entt-литералом
// мозга (round-trip to_integral).
using PlayerThreatFn = bool (*)(void* user, int slot);
// Рельеф под точкой, метры — восприятие ЗЕМЛИ ПОД СОБОЙ для вертикального
// намерения летуна (полёт-посадка 2026-09-10: «все воспринимают x/y/z»).
// nullptr = мозг слеп по вертикали и wantVz не пишет (headless-тесты).
using GroundHeightFn = float (*)(void* user, float x, float y);

// A brain needs no collision gate: walls, bounds and the crowd are the
// steering pass's business. The SolidCanStandFn parameter died with the
// private integrator.
//
// `playerFlagRef`/`playerSquadRef` — два хэндла {слот, ID} GameState (1е
// кластер 5, M-220): «без сознания» = флаг стоит НЕ на родном скваде.
// Субмиру даются ссылки, не мир; пустые хэндлы (id 0 — «никто») у фикстуры
// без игрока честно значат «сознание не терялось».
// `playerZ` — ВЫСОТА ИГРОКА, и без неё закон изотропии нарушался молча:
// гейт паники мерил «насколько он близко» в ПЛОСКОСТИ, то есть летящий игрок
// на любой высоте пугал всякого зверя в 60 планарных метрах, а зверь на
// вершине утёса панировал от того, кто стоит под утёсом. Направление бегства
// остаётся ПЛОСКИМ — ноги бегут по земле (законное 2D, оговорка закона).
// `objs`/`st` — арена сцены и гладкая память макро-сквадов ЯВНО (ломоть E:
// ctx-мосты умерли; st nullable — фикстура без мира законна, «без сознания»
// тогда не судится). Мёртвый параметр playerEntityId умер с обвязкой: тело
// звало его /*playerEnt*/, все звонящие передавали 0u.
void tick_npc_ai(SubObjects& objs, MacroStore* st,
                 float playerX, float playerY, float playerZ, float dt,
                 PlayerThreatFn threatFn = nullptr,
                 void* threatUser = nullptr,
                 GroundHeightFn heightFn = nullptr,
                 void* heightUser = nullptr,
                 MacroHandle playerFlagRef = {},
                 MacroHandle playerSquadRef = {});

} // namespace sm::sub
