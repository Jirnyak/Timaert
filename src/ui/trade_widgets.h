// Universal trade-window chrome (owner, W2b playtest). Trade is ONE system
// over any two Inventories, so its windows share one face:
//   · every trade screen shows the player's CARRY WEIGHT, exactly as the
//     inventory screen states it;
//   · every Buy/Sell moves an AMOUNT — one shared input, default 1, and
//     anything <= 1 READS as 1 (typing 0 snaps back); the transfer is
//     ALL-OR-NOTHING: short of gold or of stacks, nothing moves at all
//     (Inventory::remove already refuses partial takes).
// Both trade panels (settlement, NPC) draw these; a future container screen
// draws the same two lines and is done.
#pragma once
#include <imgui.h>

#include "macro/attributes.h"
#include "macro/items.h"
#include "macro/state.h"

namespace sm::ui {

inline void draw_trade_carry_line(const GameState& gs) {
    ImGui::SameLine();
    ImGui::TextDisabled("Carry %.1f / %.0f kg",
                        double(inventory_weight(gs.player.inventory)),
                        double(get_carry_capacity(gs.player.sheet.attributes,
                                                  gs.player.sheet.skills)));
}

// The shared amount field. Clamps in place so the player SEES the rule.
inline void draw_trade_amount_input(int* amount) {
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("Amount", amount);
    if (*amount < 1) *amount = 1;
}

// The counterparty's purse (owner, W2d): gold is universal — a town's
// treasury and a trader's coin live as the "gold" row of their own
// Inventory. Selling draws it DOWN, all-or-nothing: a counterparty that
// cannot pay does not buy. Shown, not implied — and the row itself is
// skipped in the goods list (you do not buy their purse).
inline void draw_counterparty_gold(const Inventory& theirs) {
    ImGui::SameLine();
    ImGui::TextDisabled("Their gold: %d", theirs.count("gold"));
}

} // namespace sm::ui
