// THE one name for "a thing on the macro map you can interact with" — and the
// one pair of doors to what it owns (меню-сессия, owner verdicts 2026-09-11).
//
// THE FACT THESE DOORS REST ON (verified 2026-09-11): a squad and a landmark
// already hold the SAME types — one Inventory (its bag, market, granary and
// treasury in one, coins included) and one SoldierSquad (its roster / its
// garrison; population.md: «гарнизон = армия ландмарка»). What differed was
// only the ADDRESS: the squad carries them as ECS components on its leader
// entity (ecs::NpcInventory / ecs::SquadRoster), the landmark as bare fields
// of its record in gs.landmarks. Every consumer that wanted "this object's
// store" had to know which book of addresses to open — which is exactly how
// the trade panel grew two wrappers around one barter and the settlement
// menu hardcoded LandmarkType::City (PLAY-1 / PLAY-2).
//
// So: storage stays where DOD wants it (a dense landmark vector, ECS squads —
// neither moves, the save format does not change), and the ADDRESSING becomes
// one door, the same law as apply_damage or effective_behaviour: data declares,
// one door answers. The universal interaction menu, the one trade wrapper and
// the popup rows are all written against MapSubject and never learn which
// book the address came from. If the military layer one day makes landmarks
// entities, only the bodies of these two functions change.
//
// A MapSubject is TRANSIENT UI/runtime state (an entt handle is not save
// material — same ruling as the PreBattle target): name things by it inside
// a frame, never across a save. Fail closed everywhere: an absent layer, a
// dead entity or an unknown id answers nullptr, which every caller treats as
// "no such counterparty" (S6 zero contribution).
#pragma once

#include "ecs/world.h"
#include "macro/landmark_registry.h"
#include "macro/macro_world.h"
#include "macro/map_actions.h"
#include "macro/state.h"

#include <cstdint>

namespace sm {

enum class MapSubjectKind : std::uint8_t { None = 0, Squad, Landmark };

struct MapSubject {
    MapSubjectKind kind = MapSubjectKind::None;
    entt::entity   squad = entt::null;   // valid when kind == Squad: the
                                         //   leader entity (the squad IS its
                                         //   leader, CANON S14)
    std::int32_t   landmark = -1;        // valid when kind == Landmark: the
                                         //   world-unique Landmark::id (v54,
                                         //   ONE id space, ANY kind — a
                                         //   village names itself here as
                                         //   honestly as a city)
};

inline MapSubject subject_of_squad(entt::entity e) {
    return MapSubject{MapSubjectKind::Squad, e, -1};
}
inline MapSubject subject_of_landmark(int id) {
    return MapSubject{MapSubjectKind::Landmark, entt::null, id};
}

// ── THE store door ───────────────────────────────────────────────────────
// The subject's universal Inventory: bag == market == treasury (economy.md).
// Coins live inside it as stacks, so wallet_value / transfer_value /
// barter_swap need nothing beyond what this door returns.
inline Inventory* store_of(const MacroWorld& w, MapSubject s) {
    switch (s.kind) {
    case MapSubjectKind::Squad: {
        if (!w.world || !w.world->reg.valid(s.squad)) return nullptr;
        auto* c = w.world->reg.try_get<ecs::NpcInventory>(s.squad);
        return c ? &c->inv : nullptr;
    }
    case MapSubjectKind::Landmark: {
        if (!w.gs) return nullptr;
        Landmark* lm = landmark_by_id(*w.gs, int(s.landmark));
        return lm ? &lm->inventory : nullptr;
    }
    case MapSubjectKind::None: break;
    }
    return nullptr;
}

// ── THE roster door ──────────────────────────────────────────────────────
// The subject's standing men: a squad's members (everyone but the leader) or
// a landmark's garrison — ONE SoldierSquad shape, so hire_npc, upkeep and the
// strike-through loan already speak it on both sides.
inline SoldierSquad* roster_of(const MacroWorld& w, MapSubject s) {
    switch (s.kind) {
    case MapSubjectKind::Squad: {
        if (!w.world || !w.world->reg.valid(s.squad)) return nullptr;
        auto* r = w.world->reg.try_get<ecs::SquadRoster>(s.squad);
        return r ? &r->squad : nullptr;
    }
    case MapSubjectKind::Landmark: {
        if (!w.gs) return nullptr;
        Landmark* lm = landmark_by_id(*w.gs, int(s.landmark));
        return lm ? &lm->garrison : nullptr;
    }
    case MapSubjectKind::None: break;
    }
    return nullptr;
}

// ── THE verbs door ───────────────────────────────────────────────────────
// What this subject OFFERS (macro/map_actions.h bits) — the universal menu
// lists exactly these rows. Landmarks declare theirs in the registry column
// (`actions`, plus `walkable` folded in as Enter — one mask out, no second
// byte in). A squad's vocabulary is the macro NPC's constant set: talk,
// trade, attack — availability NOW (hostility, a live counterparty, an
// actual bag) is the menu row's predicate, never a second table here.
inline std::uint16_t actions_of(const MacroWorld& w, MapSubject s) {
    switch (s.kind) {
    case MapSubjectKind::Squad: {
        if (!w.world || !w.world->reg.valid(s.squad)) return 0;
        return kMapActTalk | kMapActTrade | kMapActAttack;
    }
    case MapSubjectKind::Landmark: {
        if (!w.gs) return 0;
        const Landmark* lm = landmark_by_id(*w.gs, int(s.landmark));
        if (!lm) return 0;
        const LandmarkDef& def = landmark_def(lm->type);
        return std::uint16_t(def.actions
                             | (def.walkable ? kMapActEnter : 0));
    }
    case MapSubjectKind::None: break;
    }
    return 0;
}

} // namespace sm
