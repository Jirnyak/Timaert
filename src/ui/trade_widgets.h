// Universal trade-window chrome (owner, W2b playtest; PACKAGE deal, owner
// ruling 2026-08-07). Trade is ONE system over any two Inventories, so its
// windows share one face:
//   · every trade screen shows the player's CARRY WEIGHT, exactly as the
//     inventory screen states it;
//   · trade is a PACKAGE: +/− stages lines from BOTH shelves (step = the
//     shared Amount field, default 1, anything <= 1 READS as 1), the footer
//     faces the two totals, and ONE Deal button settles the whole package
//     through barter_swap — all-or-nothing. The law: the player's GIVEN
//     value must cover the TAKEN; any excess is his own generosity.
//   · coin is a ware IN the package: currency rows list and stage like any
//     line, at FACE value on both sides — letting charisma price a coin
//     would mint money out of a round trip.
// Both trade panels (settlement, NPC) draw these; a future container screen
// draws the same pieces and is done.
#pragma once
#include <imgui.h>

#include <string>

#include "macro/attributes.h"
#include "macro/currency.h"
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

// The shared staging step. Clamps in place so the player SEES the rule.
inline void draw_trade_amount_input(int* amount) {
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("Amount", amount);
    if (*amount < 1) *amount = 1;
}

// The counterparty's purse (owner, W2d): money is faction COIN living in
// the same Inventory as the goods (macro/currency.h). Their coin rows are
// wares like any other — this line is the at-a-glance sum of them.
inline void draw_counterparty_gold(const Inventory& theirs) {
    ImGui::SameLine();
    ImGui::TextDisabled("Their coin: %d", wallet_value(theirs));
}

inline void draw_trade_item_tooltip(const ItemDef* item) {
    if (!item || !ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(item->name);
    if (item->description && item->description[0] != '\0') {
        ImGui::TextWrapped("%s", item->description);
    }
    ImGui::Text("Weight: %.2f kg", double(item->weight));
    ImGui::EndTooltip();
}

// ── The package deal ─────────────────────────────────────────────────────

// Staged lines of one open trade screen, unique per id (barter_stage keeps
// the invariant).
struct BarterState {
    BarterPackage take;   // their shelf → the player
    BarterPackage give;   // the player → them
    void clear() { take.clear(); give.clear(); }
    bool empty() const { return take.empty() && give.empty(); }
};

inline int barter_staged(const BarterPackage& pkg, const std::string& id) {
    for (const auto& line : pkg)
        if (line.first == id) return line.second;
    return 0;
}

inline void barter_stage(BarterPackage& pkg, const std::string& id, int n) {
    for (auto it = pkg.begin(); it != pkg.end(); ++it) {
        if (it->first == id) {
            if (n <= 0) pkg.erase(it);
            else it->second = n;
            return;
        }
    }
    if (n > 0) pkg.push_back({id, n});
}

// The shelf moved under an open panel (a caravan bought it out, the day
// crafted): clamp the staged lines to what is still real instead of
// clearing the whole deal.
inline void barter_clamp(BarterPackage& pkg, const Inventory& shelf) {
    for (std::size_t i = 0; i < pkg.size();) {
        const int have = shelf.count(pkg[i].first);
        if (have <= 0) {
            pkg.erase(pkg.begin() + std::ptrdiff_t(i));
            continue;
        }
        if (pkg[i].second > have) pkg[i].second = have;
        ++i;
    }
}

// One shelf column. Every row stages into `pkg` by +/− (step = Amount);
// prices come from `unitPrice(id, def, n)` — the caller's own law columns
// (stock, charisma, context) at POST-TRADE quantity n, so every line pays
// its slippage — except currency, which is ALWAYS face value. Returns the
// staged package's total value.
template <class UnitPriceFn>
inline int draw_barter_column(const char* childId, const Inventory& shelf,
                              BarterPackage& pkg, int step,
                              UnitPriceFn unitPrice) {
    barter_clamp(pkg, shelf);
    int total = 0;
    ImGui::BeginChild(childId, ImVec2(0, 260), true);
    if (shelf.used_slots() == 0) ImGui::TextDisabled("(empty)");
    // Walk the OCCUPIED slots of the flat store. `i` is the slot index, so a
    // row's ImGui identity follows its slot rather than its position in a
    // shifting list — a stack that empties no longer renames the widget under
    // the one after it.
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        const ItemRef& ref = shelf.slots[std::size_t(i)];
        if (ref.empty()) continue;
        const ItemDef* def = item_def_at(int(ref.def));
        const std::string id = def ? def->id : std::string();
        const int count = ref.count;
        const bool coin = def && is_currency_item(id.c_str());
        const int staged = barter_staged(pkg, id);
        const int next = staged + step > count ? count : staged + step;
        ImGui::PushID(i);
        const bool canAdd = def && staged < count;
        if (!canAdd) ImGui::BeginDisabled();
        if (ImGui::Button("+", ImVec2(24, 0))) barter_stage(pkg, id, next);
        if (!canAdd) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !def)
            ImGui::SetTooltip("Unknown item id");
        ImGui::SameLine();
        if (staged <= 0) ImGui::BeginDisabled();
        if (ImGui::Button("-", ImVec2(24, 0)))
            barter_stage(pkg, id, staged - step < 0 ? 0 : staged - step);
        if (staged <= 0) ImGui::EndDisabled();
        ImGui::SameLine();
        // The row previews the unit price of the NEXT press; the staged
        // line below is valued at its own post-trade quantity.
        const int unit = !def ? 0
                         : coin ? def->value
                                : unitPrice(id, *def, next > 0 ? next : step);
        ImGui::Text("%s x%d  %d g", def ? def->name : id.c_str(), count, unit);
        draw_trade_item_tooltip(def);
        if (staged > 0 && def) {
            const int lineValue = coin ? def->value * staged
                                       : unitPrice(id, *def, staged) * staged;
            total += lineValue;
            ImGui::SameLine();
            ImGui::TextDisabled("| deal x%d = %d g", staged, lineValue);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    return total;
}

// The footer: the two totals face each other and ONE button settles the
// whole package. Returns true on a settled deal — the caller writes its
// own message and log lines from the two values.
inline bool draw_barter_deal_button(BarterState& st,
                                    Inventory& yours, Inventory& theirs,
                                    int giveValue, int takeValue) {
    ImGui::Text("You give: %d g", giveValue);
    ImGui::SameLine();
    ImGui::TextUnformatted("   ");
    ImGui::SameLine();
    ImGui::Text("You receive: %d g", takeValue);
    ImGui::SameLine();
    const bool balanced = giveValue >= takeValue;
    const bool can = !st.empty() && balanced;
    bool dealt = false;
    if (!can) ImGui::BeginDisabled();
    if (ImGui::Button("Deal", ImVec2(80, 0)))
        dealt = barter_swap(yours, theirs, st.give, st.take);
    if (!can) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (st.empty())
            ImGui::SetTooltip("Stage items with + first.");
        else if (!balanced)
            ImGui::SetTooltip("Your side must cover the value you take.");
    }
    ImGui::SameLine();
    const bool clearable = !st.empty();
    if (!clearable) ImGui::BeginDisabled();
    if (ImGui::Button("Clear", ImVec2(64, 0))) st.clear();
    if (!clearable) ImGui::EndDisabled();
    if (dealt) st.clear();
    return dealt;
}

} // namespace sm::ui
