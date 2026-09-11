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

#include <cstdio>
#include <string>

#include "macro/attributes.h"
#include "macro/character_sheet.h"
#include "macro/currency.h"
#include "macro/items.h"
#include "macro/player_entity.h"   // player_effective_sheet — the haggler door
#include "macro/squad.h"           // standing_bonuses_of
#include "macro/state.h"

namespace sm::ui {

// ── THE one trade wrapper (Инк 5 меню-сессии, 2026-09-11) ────────────────
// The DEAL was always one (barter_swap); what had split in two was the
// WRAPPER around it — the NPC window and the settlement tab each kept its
// own statics, its own five-line haggler resolve and its own copy of the
// two-column layout. These three pieces are that wrapper, once:
//   · PlayerHaggler / player_haggler — who is shopping, through the one
//     effective-sheet door;
//   · BarterWrapState — the staging state one counter keeps (each open
//     window owns ONE: two counters can be open at once, their staged
//     packages must not alias);
//   · draw_barter_body — the two shelves and the one Deal button. The
//     caller supplies only what genuinely differs: the shelf, its label,
//     the two price laws (demand is the SUBJECT's business: a town prices
//     by econSite + population, a lone trader has none) and the fact to
//     record when the deal settles.

struct PlayerHaggler {
    CharacterSheet sheet{};     // the EFFECTIVE sheet (phase 4)
    BonusTotals    standing{};  // same totals, for the derived carry cells
    int cha = 0;
    int trade = 0;              // the Trade rank haggling beside CHA (ph. 6)
};

inline PlayerHaggler player_haggler(ecs::World& w) {
    PlayerHaggler h;
    const entt::entity squad = player_squad_entity(w);
    if (squad != entt::null) h.standing = standing_bonuses_of(w, squad);
    h.sheet = player_effective_sheet(w);
    h.cha   = h.sheet.attributes.of(AttributeId::Cha);
    h.trade = h.sheet.skills.of(SkillId::Trade);
    return h;
}

// (BarterWrapState — the third piece — is declared below BarterState,
// which it wraps.)

// `sheet` is the shopper's EFFECTIVE one (phase 4) — the caller owns the door
// (player_effective_sheet); this line just prints what that back can hold.
// `standing` — the same totals that built the sheet, for the derived CarryKg
// cells the sheet copy cannot carry (bonus.h affix tail).
inline void draw_trade_carry_line(const CharacterSheet& sheet,
                                  const Inventory& bag,
                                  const BonusTotals& standing) {
    ImGui::SameLine();
    ImGui::TextDisabled("Carry %.1f / %.0f kg",
                        double(inventory_weight(bag)),
                        double(get_carry_capacity(sheet.attributes,
                                                  sheet.skills, standing)));
}

// ── The rolled instance's shopfront ───────────────────────────────────────

// The name's tint is FUNCTIONAL, not a hardcoded rarity ladder (owner
// verdict 2026-09-07): one lerp from plain to charged, driven by how many
// affix cells speak over the format's cap. Add a ninth cell someday and the
// gradient re-derives itself.
inline ImVec4 affix_tint(int affixes) {
    const float t = float(affixes) / float(kMaxItemAffixes);
    // plain parchment white → deep gold
    return ImVec4(1.0f, 1.0f - 0.35f * t, 1.0f - 0.85f * t, 1.0f);
}

// The one spelling of an instance's title line: name, the suffix its first
// affix gives it, the tint its count earns. Every panel that names a stack
// calls this, so a rolled thing cannot look plain in one window and charged
// in another.
inline void draw_item_ref_name(const ItemRef& st, const ItemDef& def) {
    const int n = affix_count(st);
    if (n == 0) {
        ImGui::Text("%s", def.name);
        return;
    }
    const char* suffix = affix_suffix(st);
    ImGui::TextColored(affix_tint(n), suffix[0] ? "%s %s" : "%s%s",
                       def.name, suffix);
}

// ...and of what it DOES: the row's innate bonuses and the instance's rolled
// affixes, one vocabulary (they are the same type), one line style.
inline void draw_item_ref_bonuses(const ItemRef& st, const ItemDef& def) {
    bool any = false;
    const auto line = [&any](Bonus b) {
        if (b.row == 0 || b.value == 0) return;
        if (any) ImGui::SameLine();
        ImGui::Text("%+d %s", int(b.value), bonus_def(BonusId(b.row)).label);
        any = true;
    };
    for (const Bonus& b : def.bonus) line(b);
    for (int i = 0; i < kMaxItemAffixes; ++i) line(st.affix_at(i));
    if (!any) ImGui::TextDisabled("-");
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

// One counter's whole wrapper state (Инк 5): the staged package, the
// Amount step, the receipt line and whose counter it is. Each open trade
// window owns ONE — two counters can be open at once, their staged
// packages must not alias.
struct BarterWrapState {
    BarterState barter{};
    int  amount = 1;            // shared staging step (Amount)
    char message[160] = "";     // the last deal's receipt line
    int  key = -1;              // whose counter this staging belongs to

    // Another counterparty = another deal: drop the message AND the
    // staged package (the law both old wrappers spelled by hand).
    void reset(int newKey) {
        key = newKey;
        message[0] = '\0';
        barter.clear();
    }
    void sync_to(int newKey) {
        if (key != newKey) reset(newKey);
    }
    void set_deal_message(int gave, int took) {
        std::snprintf(message, sizeof(message),
                      "Deal: gave %d g, received %d g.", gave, took);
    }
};

// Lines are keyed by SLOT of the source shelf (currency.h BarterLine —
// «торговля пер-стак», owner verdict 2026-09-07): a rolled sword and its
// bare twin are two lines with two prices, never one id.
inline int barter_staged(const BarterPackage& pkg, int slot) {
    for (const BarterLine& line : pkg)
        if (line.slot == slot) return line.count;
    return 0;
}

inline void barter_stage(BarterPackage& pkg, int slot, std::uint16_t def,
                         int n) {
    for (auto it = pkg.begin(); it != pkg.end(); ++it) {
        if (it->slot == slot) {
            if (n <= 0) pkg.erase(it);
            else { it->count = n; it->def = def; }
            return;
        }
    }
    if (n > 0) pkg.push_back({slot, n, def});
}

// The shelf moved under an open panel (a caravan bought it out, the day
// crafted): clamp the staged lines to what is still real instead of
// clearing the whole deal. A slot that emptied — or was re-filled with a
// DIFFERENT row — drops its line; a shrunk stack clamps.
inline void barter_clamp(BarterPackage& pkg, const Inventory& shelf) {
    for (std::size_t i = 0; i < pkg.size();) {
        const int slot = pkg[i].slot;
        const ItemRef* s = slot >= 0 && slot < kMaxInventorySlots
            ? &shelf.slots[std::size_t(slot)] : nullptr;
        if (!s || s->empty() || s->def != pkg[i].def) {
            pkg.erase(pkg.begin() + std::ptrdiff_t(i));
            continue;
        }
        if (pkg[i].count > s->count) pkg[i].count = s->count;
        ++i;
    }
}

// ── THE INVENTORY GRID (владелец 2026-09-11: «инвентарь-СЕТКА 16×16») ────
// One widget for every container view — the character's bag and both
// counters of a deal draw THE SAME grid. The data always was this shape
// (kMaxInventorySlots = 256 «16×16, the player's grid»); only the UI drew
// a list. A cell shows the item's short mark and count; hovering names it
// in full (title, affixes, bonuses, value + the caller's own lines).
// `staged` (nullable) draws the deal's RESERVATION on the very stack it
// comes from — «резервировать то, что на продажу, в стеке продающего»
// (владелец): no third grid, the goods stay where they lie, the cell shows
// «count−staged» and a gold border. Returns the clicked slot; the CALLER
// decides what a click means (select, stage, release).
struct GridClick { int slot = -1; bool right = false; };

template <class TooltipExtraFn>
inline GridClick draw_inventory_grid(const char* strId, const Inventory& inv,
                                     const BarterPackage* staged,
                                     TooltipExtraFn tooltipExtra) {
    GridClick out;
    constexpr int kGridCols = 16;
    static_assert(kMaxInventorySlots % kGridCols == 0,
                  "the grid draws every slot of the flat store");
    const float cell = ImGui::GetTextLineHeight() * 1.8f;
    ImGui::PushID(strId);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int slot = 0; slot < kMaxInventorySlots; ++slot) {
        if (slot % kGridCols != 0) ImGui::SameLine(0.0f, 2.0f);
        const ItemRef& ref = inv.slots[std::size_t(slot)];
        const ItemDef* def =
            ref.empty() ? nullptr : item_def_at(int(ref.def));
        ImGui::PushID(slot);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("c", ImVec2(cell, cell));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && def) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                out = GridClick{slot, false};
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                out = GridClick{slot, true};
        }
        const int stagedN = staged ? barter_staged(*staged, slot) : 0;
        const ImU32 bg = def ? IM_COL32(38, 32, 24, 255)
                             : IM_COL32(24, 22, 18, 160);
        const ImU32 border =
            stagedN > 0        ? IM_COL32(255, 210, 90, 255)
            : (hovered && def) ? IM_COL32(220, 220, 220, 200)
                               : IM_COL32(90, 80, 60, 120);
        dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), bg, 3.0f);
        dl->AddRect(p, ImVec2(p.x + cell, p.y + cell), border, 3.0f);
        if (def) {
            // Short mark: the name's first two letters, tinted like the
            // shopfront title (one lerp, no second rarity dictionary).
            char mark[3] = {def->name[0],
                            def->name[0] ? def->name[1] : '\0', '\0'};
            dl->AddText(ImVec2(p.x + 3.0f, p.y + 2.0f),
                        ImGui::GetColorU32(affix_tint(affix_count(ref))),
                        mark);
            char cnt[16];
            if (stagedN > 0)
                std::snprintf(cnt, sizeof(cnt), "%d", ref.count - stagedN);
            else
                std::snprintf(cnt, sizeof(cnt), "%d", ref.count);
            const ImVec2 ts = ImGui::CalcTextSize(cnt);
            dl->AddText(ImVec2(p.x + cell - ts.x - 2.0f,
                               p.y + cell - ts.y - 1.0f),
                        stagedN > 0 ? IM_COL32(255, 210, 90, 255)
                                    : IM_COL32(225, 225, 225, 220),
                        cnt);
            if (hovered) {
                ImGui::BeginTooltip();
                draw_item_ref_name(ref, *def);
                ImGui::TextDisabled("%s", def->id);
                draw_item_ref_bonuses(ref, *def);
                ImGui::Text("x%d   value %d g", ref.count, value_of(ref));
                if (stagedN > 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                                       "reserved for the deal: %d", stagedN);
                tooltipExtra(ref, *def, stagedN);
                ImGui::EndTooltip();
            }
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return out;
}

inline GridClick draw_inventory_grid(const char* strId, const Inventory& inv,
                                     const BarterPackage* staged = nullptr) {
    return draw_inventory_grid(strId, inv, staged,
                               [](const ItemRef&, const ItemDef&, int) {});
}

// A staged package's value against its shelf — the sum draw_barter_column
// used to accumulate while drawing rows; the grid separates the ink from
// the arithmetic. Coin is ALWAYS face value.
template <class UnitPriceFn>
inline int barter_package_value(const BarterPackage& pkg,
                                const Inventory& shelf,
                                UnitPriceFn unitPrice) {
    int total = 0;
    for (const BarterLine& line : pkg) {
        if (line.slot < 0 || line.slot >= kMaxInventorySlots) continue;
        const ItemRef& ref = shelf.slots[std::size_t(line.slot)];
        if (ref.empty()) continue;
        const ItemDef* def = item_def_at(int(ref.def));
        if (!def) continue;
        total += is_currency_item(def->id)
            ? def->value * line.count
            : unitPrice(ref, *def, line.count) * line.count;
    }
    return total;
}

// (draw_barter_column — the LIST shelf — died 2026-09-11 with the grid:
// the 16x16 grid above is THE container view, list and grid were becoming
// two parallel shopfronts.)

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

// The BODY every counter shares: receipt line, the two 16×16 grids, the
// one Deal button. Staging is IN-PLACE (владелец: «резервировать то, что
// на продажу, в стеке продающего»): LMB reserves +Amount from the stack
// under the cursor, RMB releases — no third grid, the goods stay where
// they lie, the gold border and «count−staged» show the reservation.
// `onDeal(gave, took)` fires on a settled package — the caller records
// ITS fact and nothing else; the receipt message writes itself.
template <class BuyFn, class SellFn, class OnDeal>
inline void draw_barter_body(const char* stockLabel,
                             BarterWrapState& st,
                             Inventory& playerBag, Inventory& shelf,
                             BuyFn buyUnit, SellFn sellUnit, OnDeal onDeal) {
    if (st.message[0] != '\0') {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", st.message);
    }
    ImGui::TextDisabled("LMB reserves +Amount for the deal, RMB releases.");
    ImGui::Separator();
    barter_clamp(st.barter.take, shelf);
    barter_clamp(st.barter.give, playerBag);
    const auto stage_click = [&](GridClick c, BarterPackage& pkg,
                                 const Inventory& bag) {
        if (c.slot < 0) return;
        const ItemRef& ref = bag.slots[std::size_t(c.slot)];
        if (ref.empty()) return;
        const int step = st.amount > 1 ? st.amount : 1;
        const int cur = barter_staged(pkg, c.slot);
        int want = c.right ? cur - step : cur + step;
        if (want < 0) want = 0;
        if (want > ref.count) want = ref.count;
        barter_stage(pkg, c.slot, ref.def, want);
    };
    ImGui::Columns(2, "barter_cols", true);
    ImGui::TextUnformatted(stockLabel);
    stage_click(
        draw_inventory_grid("##shelf", shelf, &st.barter.take,
                            [&](const ItemRef& r, const ItemDef& d, int n) {
                                ImGui::Text("buy at %d g each",
                                            buyUnit(r, d, n > 0 ? n : 1));
                            }),
        st.barter.take, shelf);
    ImGui::NextColumn();
    ImGui::TextUnformatted("Your inventory");
    stage_click(
        draw_inventory_grid("##player_shelf", playerBag, &st.barter.give,
                            [&](const ItemRef& r, const ItemDef& d, int n) {
                                ImGui::Text("sell at %d g each",
                                            sellUnit(r, d, n > 0 ? n : 1));
                            }),
        st.barter.give, playerBag);
    ImGui::Columns(1);
    const int takeValue =
        barter_package_value(st.barter.take, shelf, buyUnit);
    const int giveValue =
        barter_package_value(st.barter.give, playerBag, sellUnit);
    if (draw_barter_deal_button(st.barter, playerBag, shelf,
                                giveValue, takeValue)) {
        st.set_deal_message(giveValue, takeValue);
        onDeal(giveValue, takeValue);
    }
}

} // namespace sm::ui
