// Quest engine — objective evaluation + reward dispatch. Mirrors quest-engine.ts.
#pragma once
#include <vector>
#include "events/quests/quest_types.h"
#include "events/event_bus.h"
#include "macro/state.h"

namespace sm {

class QuestEngine {
public:
    // Evaluate every active quest objective against last-tick events + player.
    // Marks objectives complete, completes quests, dispatches rewards.
    // `bag` is the container rewards are paid into and delivery objectives are
    // checked against — the player's bag is an ordinary NpcInventory on his
    // squad entity now (macro/player_entity.h), so the engine is handed the
    // container instead of reaching into PlayerState for one. Null = no world
    // yet: nothing is paid and no delivery can complete.
    // `head` is what the player REMEMBERS — the ordinary AgentMemory on the
    // same squad entity, where an unpayable fine is written down as a debt
    // fact. Handed in for the same reason `bag` is: this layer may not reach
    // into the ECS, and PlayerState is not a second store for either.
    void tick(std::vector<Quest>& active,
              EventBus& bus,
              GameState& gs,
              Inventory* bag,
              AgentMemory* head);

    // Accepting is the moment an offer becomes an object of the world, so
    // this is where its ordinal is issued (gs.nextQuestOrdinal — the one
    // quest-identity door, CANON S20.1). Takes GameState for that issuer.
    void accept(std::vector<Quest>& active,
                Quest q,
                GameState& gs,
                EventBus& bus);
    void abandon(std::vector<Quest>& active, std::uint32_t ordinal,
                 EventBus& bus);
    // Has the player already taken or settled THIS OFFER? Compared by the
    // offer's provenance triple (quest_types.h same_offer) against the
    // active list and the settled-offer memory — an offer regenerates from
    // the seed every day, so its identity is its provenance, never an
    // ordinal it does not have yet.
    bool is_known(const std::vector<Quest>& active,
                  const PlayerState& player,
                  const Quest& offer) const;
};

// Project active quests onto gs.markers as gold "!" quest pins — one per
// incomplete objective whose completion is anchored to a world cell (VisitCell,
// FindLocation, DeliverItems -> target settlement, WaitAt, InteractCell; a
// DestroyNpc "kill N" objective has no fixed cell and is skipped). Rebuilds the
// whole "quest_" marker set, so it is idempotent and also reconciles stale pins
// carried in from a loaded save. Universal: no per-quest special-casing — the
// cell resolver mirrors eval_objective() field-for-field.
void rebuild_quest_markers(GameState& gs, const std::vector<Quest>& active);

} // namespace sm
