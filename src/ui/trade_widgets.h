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

// One shelf column. Every STACK stages into `pkg` by +/− (step = Amount);
// prices come from `unitPrice(ref, def, n)` — the caller's own law columns
// (stock, charisma, context) at POST-TRADE quantity n, so every line pays
// its slippage — except currency, which is ALWAYS face value. The callback
// takes the INSTANCE (owner verdict 2026-09-07 «цена везде через value_of»):
// a rolled stack prices its affixes, its bare twin two slots down does not,
// and each is its own line. Returns the staged package's total value.
template <class UnitPriceFn>
inline int draw_barter_column(const char* childId, const Inventory& shelf,
                              BarterPackage& pkg, int step,
                              UnitPriceFn unitPrice) {
    barter_clamp(pkg, shelf);
    int total = 0;
    ImGui::BeginChild(childId, ImVec2(0, 260), true);
    if (shelf.used_slots() == 0) ImGui::TextDisabled("(empty)");
    // Walk the OCCUPIED slots of the flat store. `i` is the slot index — the
    // package's own key, so a row's ImGui identity and its staged line both
    // follow the STACK rather than a name two stacks share.
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        const ItemRef& ref = shelf.slots[std::size_t(i)];
        if (ref.empty()) continue;
        const ItemDef* def = item_def_at(int(ref.def));
        const int count = ref.count;
        const bool coin = def && is_currency_item(def->id);
        const int staged = barter_staged(pkg, i);
        const int next = staged + step > count ? count : staged + step;
        ImGui::PushID(i);
        const bool canAdd = def && staged < count;
        if (!canAdd) ImGui::BeginDisabled();
        if (ImGui::Button("+", ImVec2(24, 0)))
            barter_stage(pkg, i, ref.def, next);
        if (!canAdd) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !def)
            ImGui::SetTooltip("Unknown item id");
        ImGui::SameLine();
        if (staged <= 0) ImGui::BeginDisabled();
        if (ImGui::Button("-", ImVec2(24, 0)))
            barter_stage(pkg, i, ref.def,
                         staged - step < 0 ? 0 : staged - step);
        if (staged <= 0) ImGui::EndDisabled();
        ImGui::SameLine();
        // The row previews the unit price of the NEXT press; the staged
        // line below is valued at its own post-trade quantity.
        const int unit = !def ? 0
                         : coin ? def->value
                                : unitPrice(ref, *def, next > 0 ? next : step);
        if (def) {
            // The ONE shopfront spelling: a rolled stack shows its suffix
            // and tint on the counter exactly as in the bag.
            draw_item_ref_name(ref, *def);
            ImGui::SameLine();
            ImGui::Text("x%d  %d g", count, unit);
        } else {
            ImGui::Text("(unknown) x%d", count);
        }
        draw_trade_item_tooltip(def);
        if (staged > 0 && def) {
            const int lineValue = coin ? def->value * staged
                                       : unitPrice(ref, *def, staged) * staged;
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

// The BODY every counter shares: receipt line, the two shelves in two
// columns, the one Deal button. `onDeal(gave, took)` fires on a settled
// package — the caller records ITS fact (a squad's ordinal, a landmark's
// id) and nothing else; the receipt message writes itself.
template <class BuyFn, class SellFn, class OnDeal>
inline void draw_barter_body(const char* stockLabel,
                             BarterWrapState& st,
                             Inventory& playerBag, Inventory& shelf,
                             BuyFn buyUnit, SellFn sellUnit, OnDeal onDeal) {
    if (st.message[0] != '\0') {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", st.message);
    }
    ImGui::Separator();
    ImGui::Columns(2, "barter_cols", true);
    ImGui::TextUnformatted(stockLabel);
    const int takeValue = draw_barter_column("##shelf", shelf,
                                             st.barter.take, st.amount,
                                             buyUnit);
    ImGui::NextColumn();
    ImGui::TextUnformatted("Your inventory");
    const int giveValue = draw_barter_column("##player_shelf", playerBag,
                                             st.barter.give, st.amount,
                                             sellUnit);
    ImGui::Columns(1);
    if (draw_barter_deal_button(st.barter, playerBag, shelf,
                                giveValue, takeValue)) {
        st.set_deal_message(giveValue, takeValue);
        onDeal(giveValue, takeValue);
    }
}

} // namespace sm::ui
