#include "ui/overlays.h"
#include "macro/map_generator.h"
#include "macro/biomes.h"
#include "macro/army.h"
#include "macro/economy.h"
#include "macro/items.h"
#include "macro/politik.h"
#include "content/spells/spell_types.h"
#include "content/plot/encounters.h"
#include "events/event_bus.h"
#include "gl/gl.h"
#include "gl/helpers.h"
#include "imgui.h"
#include "sub/seamless_manager.h"
#include "sub/map_data.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace sm::ui {

void draw_diplomacy(GameState& gs, bool* open) {
    if (!open || !*open) return;
    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Diplomacy", open)) {
        ImGui::Text("Factions: %zu", gs.factions.size());
        ImGui::Separator();
        for (const auto& [id, f] : gs.factions) {
            ImGui::Text("%-16s  rep:%4d", f.name.c_str(),
                gs.player.reputation.count(id) ? gs.player.reputation.at(id) : 0);
        }
        ImGui::Separator();
        ImGui::Text("Kingdoms: %zu", gs.politik.kingdoms.size());
        for (const auto& k : gs.politik.kingdoms) {
            ImGui::Text("  - %s  (%zu cities)", k.name.c_str(), k.cityIdxs.size());
        }
    }
    ImGui::End();
}

namespace {
const char* kUnitNames[kUnitTypeCount] = {"Swordsman", "Archer", "Spearman", "Horseman"};
const char* kLineageNames[] = {"Empire", "Magika", "Timaert", "Barbarians"};

const char* mood_label(SettlementMood m) {
    switch (m) {
        case SettlementMood::Prosperous: return "Prosperous";
        case SettlementMood::Stable:     return "Stable";
        case SettlementMood::Tense:      return "Tense";
        case SettlementMood::Unrest:     return "Unrest";
        case SettlementMood::Revolt:     return "Revolt";
    }
    return "?";
}

ImU32 mood_color(SettlementMood m) {
    switch (m) {
        case SettlementMood::Prosperous: return IM_COL32( 90, 220, 120, 255);
        case SettlementMood::Stable:     return IM_COL32(220, 220, 220, 255);
        case SettlementMood::Tense:      return IM_COL32(240, 200,  80, 255);
        case SettlementMood::Unrest:     return IM_COL32(220, 130,  80, 255);
        case SettlementMood::Revolt:     return IM_COL32(230,  70,  70, 255);
    }
    return IM_COL32(220, 220, 220, 255);
}

int rest_cost(SettlementMood m) {
    switch (m) {
        case SettlementMood::Prosperous: return 5;
        case SettlementMood::Stable:     return 10;
        case SettlementMood::Tense:      return 15;
        case SettlementMood::Unrest:     return 20;
        case SettlementMood::Revolt:     return 30;
    }
    return 10;
}

const char* objective_kind_label(ObjectiveKind k) {
    switch (k) {
        case ObjectiveKind::VisitCell:    return "Visit";
        case ObjectiveKind::FindLocation: return "Find";
        case ObjectiveKind::DeliverItems: return "Deliver";
        case ObjectiveKind::DestroyNpc:   return "Destroy";
        case ObjectiveKind::WaitAt:       return "Wait";
        case ObjectiveKind::InteractCell: return "Interact";
    }
    return "?";
}

const char* quest_category_label(QuestCategory c) {
    switch (c) {
        case QuestCategory::Main:       return "main";
        case QuestCategory::Side:       return "side";
        case QuestCategory::Procedural: return "procedural";
    }
    return "?";
}

const char* item_type_label(ItemType t) {
    switch (t) {
        case ItemType::Weapon:   return "Weapon";
        case ItemType::Armor:    return "Armor";
        case ItemType::Potion:   return "Potion";
        case ItemType::Food:     return "Food";
        case ItemType::Material: return "Material";
        case ItemType::Misc:     return "Misc";
    }
    return "?";
}

bool is_equipment_item(const ItemDef& def) {
    return def.type == ItemType::Weapon || def.type == ItemType::Armor;
}

void draw_item_effect(const ItemEffect& e) {
    bool any = false;
    auto part = [&any](const char* label, int v) {
        if (v == 0) return;
        if (any) ImGui::SameLine();
        ImGui::Text("%+d %s", v, label);
        any = true;
    };
    part("HP", e.hp);
    part("MP", e.mp);
    part("SP", e.sp);
    part("STR", e.str);
    part("END", e.end);
    part("AGI", e.agi);
    if (!any) ImGui::TextDisabled("-");
}

int active_route_count_for(const GameState& gs, int settlementId) {
    int count = 0;
    for (const auto& route : gs.activeTradeRoutes) {
        if (route.valid &&
            (route.originId == settlementId || route.destId == settlementId)) {
            ++count;
        }
    }
    return count;
}

void draw_info_overview_row(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value);
}

void draw_info_overview_row(const char* label, int value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::Text("%d", value);
}

void draw_info_overview_row(const char* label, float value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::Text("%.1f", value);
}

void draw_objective_line(const Objective& o) {
    const char* done = o.completed ? "[x]" : "[ ]";
    switch (o.kind) {
        case ObjectiveKind::VisitCell:
            ImGui::BulletText("%s Visit (%d,%d) radius %.1f",
                              done, o.ix, o.iy, o.radius);
            break;
        case ObjectiveKind::FindLocation:
            ImGui::BulletText("%s Find location in cell (%d,%d)",
                              done, o.cellX, o.cellY);
            break;
        case ObjectiveKind::DeliverItems:
            ImGui::BulletText("%s Deliver %d x %s to settlement %d",
                              done, o.quantity, o.itemId.c_str(), o.targetSettlementId);
            break;
        case ObjectiveKind::DestroyNpc:
            ImGui::BulletText("%s Defeat type %d: %d/%d",
                              done, o.npcType, o.killed, o.count);
            break;
        case ObjectiveKind::WaitAt:
            ImGui::BulletText("%s Wait at (%d,%d): %d/%d hours",
                              done, o.ix, o.iy, o.hoursWaited, o.hoursRequired);
            break;
        case ObjectiveKind::InteractCell:
            ImGui::BulletText("%s %s at (%d,%d)",
                              done, o.action.c_str(), o.ix, o.iy);
            break;
    }
}

void draw_reward_line(const Reward& r) {
    switch (r.kind) {
        case RewardKind::Gold:
            ImGui::BulletText("%d gold", r.amount);
            break;
        case RewardKind::Xp:
            ImGui::BulletText("%d xp", r.amount);
            break;
        case RewardKind::Item:
            ImGui::BulletText("%d x %s", r.amount, r.itemId.c_str());
            break;
        case RewardKind::Reputation:
            ImGui::BulletText("%+d reputation: %s", r.delta, r.faction.c_str());
            break;
        case RewardKind::Event:
            ImGui::BulletText("Event reward");
            break;
    }
}

ImGuiTabItemFlags selected_tab(CharacterPanelTab current, CharacterPanelTab tab) {
    return current == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
}

ImGuiTabItemFlags selected_tab(SettlementPanelTab current, SettlementPanelTab tab) {
    return current == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
}

void draw_inventory_table(const Inventory& inv, bool showValue) {
    if (inv.stacks.empty()) {
        ImGui::TextDisabled("(empty)");
        return;
    }
    const int cols = showValue ? 4 : 3;
    if (ImGui::BeginTable("inventory_table", cols,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Item");
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        if (showValue) ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableHeadersRow();
        for (const auto& st : inv.stacks) {
            const ItemDef* def = item_def(st.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", def ? def->name : st.id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", st.id.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("x%d", st.count);
            if (showValue) {
                ImGui::TableNextColumn();
                ImGui::Text("%d g", def ? def->value * st.count : 0);
            }
        }
        ImGui::EndTable();
    }
}
} // namespace

void draw_character_panel(GameState& gs, bool* open, CharacterPanelTab* tab) {
    if (!open || !*open) return;

    PlayerState& p = gs.player;
    const CharacterPanelTab current = tab ? *tab : CharacterPanelTab::Stats;
    const DerivedBonuses derived = calculate_derived(p.attributes, p.skills);
    const float carryWeight = inventory_weight(p.inventory);
    const float carryCap = get_carry_capacity(p.attributes, p.skills);
    const int armyTotal = total_units(p.army);
    const int armyUpkeep = calculate_army_upkeep(p.army, p.attributes.cha);

    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Character", open)) {
        ImGui::Text("%s  Level %d  Age %d days",
                    p.name.empty() ? "Wanderer" : p.name.c_str(),
                    p.levelData.level, p.ageDays);
        ImGui::SameLine();
        ImGui::TextDisabled("EXP %d / %d", p.levelData.exp, p.levelData.expToNext);
        ImGui::Separator();

        if (ImGui::BeginTabBar("##character_tabs")) {
            const bool statsOpen = ImGui::BeginTabItem("Stats", nullptr,
                selected_tab(current, CharacterPanelTab::Stats));
            if (tab && ImGui::IsItemClicked()) *tab = CharacterPanelTab::Stats;
            if (statsOpen) {
                if (ImGui::BeginTable("stats_grid", 3,
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Vitals");
                    ImGui::TableSetupColumn("Attributes");
                    ImGui::TableSetupColumn("Derived");
                    ImGui::TableHeadersRow();
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("HP %d / %d", p.combatStats.currentHp, p.combatStats.maxHp);
                    ImGui::Text("MP %d / %d", p.combatStats.currentMp, p.combatStats.maxMp);
                    ImGui::Text("SP %d / %d", p.combatStats.currentSp, p.combatStats.maxSp);
                    ImGui::Text("Gold %d", p.gold);
                    ImGui::TableNextColumn();
                    ImGui::Text("STR %d", p.attributes.str);
                    ImGui::Text("VIT %d", p.attributes.vit);
                    ImGui::Text("END %d", p.attributes.end);
                    ImGui::Text("WIL %d", p.attributes.wil);
                    ImGui::Text("INT %d", p.attributes.intl);
                    ImGui::Text("WIS %d", p.attributes.wis);
                    ImGui::Text("LCK %d", p.attributes.lck);
                    ImGui::Text("CHA %d", p.attributes.cha);
                    ImGui::Text("SPD %d", p.attributes.spd);
                    ImGui::TableNextColumn();
                    ImGui::Text("Phys dmg +%.0f", derived.rawPhysDamage);
                    ImGui::Text("Spell dmg +%.0f", derived.rawSpellDamage);
                    ImGui::Text("EXP x%.2f", derived.expMult);
                    ImGui::Text("Move x%.2f", derived.moveSpeedMult);
                    ImGui::Text("Trade %.0f%%", derived.tradeDiscount * 100.0f);
                    ImGui::Text("Crit %.1f%%", derived.critBase * 100.0f);
                    ImGui::Text("Carry %.1f / %.0f kg", carryWeight, carryCap);
                    ImGui::EndTable();
                }
                ImGui::Spacing();
                if (ImGui::BeginTable("skills_grid", 2,
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Skill");
                    ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableHeadersRow();
                    auto skill_row = [](const char* name, int value) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", name);
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", value);
                    };
                    skill_row("Bodybuilding", p.skills.bodybuilding);
                    skill_row("Meditation", p.skills.meditation);
                    skill_row("Travel", p.skills.travel);
                    skill_row("Fighter", p.skills.fighter);
                    skill_row("Endurance", p.skills.endurance);
                    skill_row("Spellcraft", p.skills.spellcraft);
                    skill_row("Weightlifting", p.skills.weightlifting);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            const bool inventoryOpen = ImGui::BeginTabItem("Inventory", nullptr,
                selected_tab(current, CharacterPanelTab::Inventory));
            if (tab && ImGui::IsItemClicked()) *tab = CharacterPanelTab::Inventory;
            if (inventoryOpen) {
                ImGui::Text("Stacks: %zu  Items: %d  Weight: %.1f / %.0f kg",
                            p.inventory.stacks.size(), p.inventory.total(), carryWeight, carryCap);
                draw_inventory_table(p.inventory, true);
                ImGui::EndTabItem();
            }

            const bool armyOpen = ImGui::BeginTabItem("Army", nullptr,
                selected_tab(current, CharacterPanelTab::Army));
            if (tab && ImGui::IsItemClicked()) *tab = CharacterPanelTab::Army;
            if (armyOpen) {
                ImGui::Text("Total: %d  Upkeep: %d g/day", armyTotal, armyUpkeep);
                if (ImGui::BeginTable("army_grid", 3,
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Unit");
                    ImGui::TableSetupColumn("Count");
                    ImGui::TableSetupColumn("Upkeep");
                    ImGui::TableHeadersRow();
                    for (auto t : kAllUnitTypes) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", kUnitNames[std::size_t(t)]);
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", p.army.get(t));
                        ImGui::TableNextColumn();
                        ImGui::Text("%d g/day", upkeep_cost(t));
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            const bool equipmentOpen = ImGui::BeginTabItem("Equipment", nullptr,
                selected_tab(current, CharacterPanelTab::Equipment));
            if (tab && ImGui::IsItemClicked()) *tab = CharacterPanelTab::Equipment;
            if (equipmentOpen) {
                ImGui::Text("Equipment data model missing");
                ImGui::TextDisabled("PlayerState has inventory, attributes, skills, army, and spells.");
                ImGui::TextDisabled("No equipped weapon/armor slots are serialized in save version %d.",
                                    gs.version);
                ImGui::Separator();
                ImGui::TextUnformatted("Equip-capable inventory items");
                int equipStacks = 0;
                if (ImGui::BeginTable("equipment_candidates", 5,
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Item");
                    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                    ImGui::TableSetupColumn("Effect");
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableHeadersRow();
                    for (const auto& st : p.inventory.stacks) {
                        const ItemDef* def = item_def(st.id);
                        if (!def || !is_equipment_item(*def)) continue;
                        ++equipStacks;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", def->name);
                        ImGui::TextDisabled("%s", def->id);
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", item_type_label(def->type));
                        ImGui::TableNextColumn();
                        ImGui::Text("x%d", st.count);
                        ImGui::TableNextColumn();
                        draw_item_effect(def->effect);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("carried only");
                    }
                    ImGui::EndTable();
                }
                if (equipStacks == 0) {
                    ImGui::TextDisabled("No weapon or armor stacks are currently in inventory.");
                }
                ImGui::Separator();
                ImGui::TextDisabled(
                    "Equip/unequip needs PlayerEquipment slots, compatibility rules, "
                    "stat recomputation, and save serialization.");
                ImGui::EndTabItem();
            }

            const bool spellsOpen = ImGui::BeginTabItem("Spells", nullptr,
                selected_tab(current, CharacterPanelTab::Spells));
            if (tab && ImGui::IsItemClicked()) *tab = CharacterPanelTab::Spells;
            if (spellsOpen) {
                if (p.spellBookSpellIds.empty()) {
                    ImGui::TextDisabled("(none)");
                } else if (ImGui::BeginTable("spell_grid", 3,
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Id");
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Mana");
                    ImGui::TableHeadersRow();
                    for (const auto& id : p.spellBookSpellIds) {
                        const SpellDef* def = spell_registry().find(id);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", id.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", def ? def->name.c_str() : "(unknown)");
                        ImGui::TableNextColumn();
                        if (def) ImGui::Text("%d", def->manaCost);
                        else ImGui::TextDisabled("-");
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void draw_settlement(GameState& gs,
                     int settlementId,
                     const std::vector<Quest>& availableQuests,
                     std::vector<Quest>& activeQuests,
                     QuestEngine& questEngine,
                     EventBus& bus,
                     SettlementPanelTab* tab,
                     bool* open) {
    if (!open || !*open) return;
    Settlement* s = nullptr;
    for (auto& c : gs.settlements) if (c.id == settlementId) { s = &c; break; }
    const SettlementPanelTab current = tab ? *tab : SettlementPanelTab::Info;

    ImGui::SetNextWindowSize(ImVec2(760, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settlement", open)) {
        if (!s) {
            ImGui::Text("No settlement selected (id=%d).", settlementId);
            ImGui::End();
            return;
        }

        // ── Banner ──
        const char* lineage = "?";
        const char* kingdom = "Unaligned";
        if (s->kingdomIdx >= 0 && s->kingdomIdx < int(gs.politik.kingdoms.size())) {
            const auto& k = gs.politik.kingdoms[std::size_t(s->kingdomIdx)];
            kingdom = k.name.c_str();
            std::size_t li = std::size_t(k.lineage);
            if (li < std::size(kLineageNames)) lineage = kLineageNames[li];
        }
        ImGui::PushFont(nullptr);
        ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.50f, 1.0f), "%s", s->name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("(City)");
        ImGui::Text("Kingdom: %s   Lineage: %s", kingdom, lineage);
        ImGui::Text("Population: %d   Economy: %s", s->population,
                    s->economy.empty() ? "—" : s->economy.c_str());
        ImGui::TextColored(ImColor(mood_color(s->mood)), "Mood: %s", mood_label(s->mood));
        ImGui::Text("Wealth: %.0f g   Happiness: %.0f%%",
                    s->eco.wealth, s->eco.happiness * 100.0f);
        ImGui::Separator();

        // ── Tabs ──
        if (ImGui::BeginTabBar("##settab")) {
            // Info
            const bool infoOpen = ImGui::BeginTabItem("Info", nullptr,
                selected_tab(current, SettlementPanelTab::Info));
            if (tab && ImGui::IsItemClicked()) *tab = SettlementPanelTab::Info;
            if (infoOpen) {
                ImGui::TextWrapped("Welcome to %s.", s->name.c_str());
                ImGui::TextDisabled("A city with population %d and a %s economy.",
                                    s->population,
                                    s->economy.empty() ? "unknown" : s->economy.c_str());
                ImGui::Spacing();

                if (ImGui::BeginTable("settlement_info", 2,
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("Value");
                    ImGui::TableHeadersRow();
                    draw_info_overview_row("Population", s->population);
                    draw_info_overview_row("Mood", mood_label(s->mood));
                    draw_info_overview_row("Economy", s->economy.c_str());
                    draw_info_overview_row("Kingdom index", s->kingdomIdx);
                    draw_info_overview_row("Wealth", s->eco.wealth);
                    draw_info_overview_row("Happiness", s->eco.happiness);
                    draw_info_overview_row("Garrison units", total_units(s->garrison));
                    draw_info_overview_row("Inventory stacks", int(s->inventory.stacks.size()));
                    draw_info_overview_row("Inventory items", s->inventory.total());
                    draw_info_overview_row("Active trade routes",
                                           active_route_count_for(gs, s->id));
                    draw_info_overview_row("History samples",
                                           int(s->history.population.size()));
                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::BeginDisabled();
                ImGui::Button("Enter City");
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Native settlement entry is routed through Enter / In, not this overlay callback.");
                }
                ImGui::SameLine();
                if (ImGui::Button("Trade") && tab) {
                    *tab = SettlementPanelTab::Trade;
                }
                ImGui::EndTabItem();
            }

            // Trade
            const bool tradeOpen = ImGui::BeginTabItem("Trade", nullptr,
                selected_tab(current, SettlementPanelTab::Trade));
            if (tab && ImGui::IsItemClicked()) *tab = SettlementPanelTab::Trade;
            if (tradeOpen) {
                ImGui::Text("Player gold: %d", gs.player.gold);
                ImGui::Separator();

                ImGui::Columns(2, "trade_cols", true);
                ImGui::TextUnformatted("Settlement stock");
                ImGui::BeginChild("##settlement_stock", ImVec2(0, 260), true);
                if (s->inventory.stacks.empty()) {
                    ImGui::TextDisabled("(empty)");
                } else {
                    bool changed = false;
                    for (std::size_t i = 0; i < s->inventory.stacks.size() && !changed; ++i) {
                        const std::string& id = s->inventory.stacks[i].id;
                        const int count = s->inventory.stacks[i].count;
                        const ItemDef* def = item_def(id);
                        const int price = def
                            ? player_buy_price(def->value, gs.player.attributes.cha, 0)
                            : 0;
                        const bool canBuy = def && count > 0 && gs.player.gold >= price;
                        ImGui::PushID(int(i));
                        if (!canBuy) ImGui::BeginDisabled();
                        if (ImGui::Button("Buy", ImVec2(52, 0))) {
                            gs.player.gold -= price;
                            gs.player.inventory.add(id, 1);
                            s->inventory.remove(id, 1);
                            changed = true;
                        }
                        if (!canBuy) ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !def) {
                            ImGui::SetTooltip("Unknown item id");
                        }
                        ImGui::SameLine();
                        ImGui::Text("%s x%d  %d g",
                                    def ? def->name : id.c_str(), count, price);
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();

                ImGui::NextColumn();
                ImGui::TextUnformatted("Your inventory");
                ImGui::BeginChild("##player_stock", ImVec2(0, 260), true);
                if (gs.player.inventory.stacks.empty()) {
                    ImGui::TextDisabled("(empty)");
                } else {
                    bool changed = false;
                    for (std::size_t i = 0; i < gs.player.inventory.stacks.size() && !changed; ++i) {
                        const std::string& id = gs.player.inventory.stacks[i].id;
                        const int count = gs.player.inventory.stacks[i].count;
                        const ItemDef* def = item_def(id);
                        const int price = def
                            ? player_sell_price(def->value, gs.player.attributes.cha, 0)
                            : 0;
                        ImGui::PushID(int(i));
                        if (!def) ImGui::BeginDisabled();
                        if (ImGui::Button("Sell", ImVec2(52, 0))) {
                            gs.player.gold += price;
                            s->inventory.add(id, 1);
                            gs.player.inventory.remove(id, 1);
                            changed = true;
                        }
                        if (!def) ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !def) {
                            ImGui::SetTooltip("Unknown item id");
                        }
                        ImGui::SameLine();
                        ImGui::Text("%s x%d  %d g",
                                    def ? def->name : id.c_str(), count, price);
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                ImGui::Columns(1);

                ImGui::Separator();
                if (ImGui::BeginTable("economy_prices", 3,
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Resource");
                    ImGui::TableSetupColumn("Stock");
                    ImGui::TableSetupColumn("Price");
                    ImGui::TableHeadersRow();
                    for (std::size_t i = 0; i < kNumResources; ++i) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", kResourceNames[i]);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.1f", s->eco.resources[i]);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.1f", s->eco.resourcePrices[i]);
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            // Garrison
            const bool garrisonOpen = ImGui::BeginTabItem("Garrison", nullptr,
                selected_tab(current, SettlementPanelTab::Garrison));
            if (tab && ImGui::IsItemClicked()) *tab = SettlementPanelTab::Garrison;
            if (garrisonOpen) {
                int total = total_units(s->garrison);
                ImGui::Text("Total: %d units", total);
                ImGui::Spacing();
                if (ImGui::BeginTable("garrison", 2,
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                    for (auto t : kAllUnitTypes) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", kUnitNames[std::size_t(t)]);
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", s->garrison.get(t));
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            // Recruit
            const bool recruitOpen = ImGui::BeginTabItem("Recruit", nullptr,
                selected_tab(current, SettlementPanelTab::Recruit));
            if (tab && ImGui::IsItemClicked()) *tab = SettlementPanelTab::Recruit;
            if (recruitOpen) {
                ImGui::Text("Player gold: %d", gs.player.gold);
                ImGui::Spacing();
                for (auto t : kAllUnitTypes) {
                    int cost  = hire_cost(t);
                    int avail = s->garrison.get(t);
                    int owned = gs.player.army.get(t);
                    ImGui::PushID(int(t));
                    bool can = avail > 0 && gs.player.gold >= cost;
                    if (!can) ImGui::BeginDisabled();
                    if (ImGui::Button("Hire")) {
                        int paid = hire_unit(gs.player.army, s->garrison, t, gs.player.gold);
                        gs.player.gold -= paid;
                    }
                    if (!can) ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::Text("%-10s  cost %d g   avail %d   you have %d",
                                kUnitNames[std::size_t(t)], cost, avail, owned);
                    ImGui::PopID();
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Daily upkeep: %d g",
                    calculate_army_upkeep(gs.player.army,
                        gs.player.attributes.cha));
                ImGui::EndTabItem();
            }
            // Inventory
            const bool inventoryOpen = ImGui::BeginTabItem("Inventory", nullptr,
                selected_tab(current, SettlementPanelTab::Inventory));
            if (tab && ImGui::IsItemClicked()) *tab = SettlementPanelTab::Inventory;
            if (inventoryOpen) {
                if (s->inventory.stacks.empty()) {
                    ImGui::TextDisabled("(empty)");
                } else {
                    if (ImGui::BeginTable("inv", 2,
                            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
                        for (const auto& st : s->inventory.stacks) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", st.id.c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("x%d", st.count);
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }
            // History (population over time)
            const bool historyOpen = ImGui::BeginTabItem("History", nullptr,
                selected_tab(current, SettlementPanelTab::History));
            if (tab && ImGui::IsItemClicked()) *tab = SettlementPanelTab::History;
            if (historyOpen) {
                if (s->history.population.empty()) {
                    ImGui::TextDisabled("(no history yet)");
                } else {
                    std::vector<float> v;
                    v.reserve(s->history.population.size());
                    for (int p : s->history.population) v.push_back(float(p));
                    ImGui::PlotLines("Population", v.data(), int(v.size()),
                                     0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 100));
                    ImGui::Text("Earliest: %d   Latest: %d",
                        s->history.population.front(),
                        s->history.population.back());
                }
                ImGui::EndTabItem();
            }
            // Rest
            const bool restOpen = ImGui::BeginTabItem("Rest", nullptr,
                selected_tab(current, SettlementPanelTab::Rest));
            if (tab && ImGui::IsItemClicked()) *tab = SettlementPanelTab::Rest;
            if (restOpen) {
                int cost = rest_cost(s->mood);
                ImGui::Text("Inn rest: %d g (mood-priced)", cost);
                ImGui::Text("Restores HP / MP / SP to full.");
                ImGui::Spacing();
                bool can = gs.player.gold >= cost;
                if (!can) ImGui::BeginDisabled();
                if (ImGui::Button("Rest at Inn")) {
                    gs.player.gold -= cost;
                    gs.player.combatStats.currentHp = gs.player.combatStats.maxHp;
                    gs.player.combatStats.currentMp = gs.player.combatStats.maxMp;
                    gs.player.combatStats.currentSp = gs.player.combatStats.maxSp;
                }
                if (!can) ImGui::EndDisabled();
                if (!can) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "not enough gold");
                }
                ImGui::EndTabItem();
            }
            // Quests
            const bool questsOpen = ImGui::BeginTabItem("Quests", nullptr,
                selected_tab(current, SettlementPanelTab::Quests));
            if (tab && ImGui::IsItemClicked()) *tab = SettlementPanelTab::Quests;
            if (questsOpen) {
                ImGui::Text("Available today: %zu", availableQuests.size());
                ImGui::Separator();
                for (const auto& q : availableQuests) {
                    const bool known = questEngine.is_known(activeQuests, gs.player, q.id);
                    ImGui::PushID(q.id.c_str());
                    if (ImGui::TreeNode("quest", "%s%s", q.title.c_str(),
                                        known ? " (known)" : "")) {
                        ImGui::TextWrapped("%s", q.description.c_str());
                        ImGui::Text("Difficulty: %d", q.difficulty);
                        ImGui::TextUnformatted("Objectives");
                        for (const auto& o : q.objectives) draw_objective_line(o);
                        ImGui::TextUnformatted("Rewards");
                        for (const auto& r : q.rewards) draw_reward_line(r);
                        if (known) ImGui::BeginDisabled();
                        if (ImGui::Button("Accept")) {
                            questEngine.accept(activeQuests, q, gs.player, bus);
                        }
                        if (known) ImGui::EndDisabled();
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void draw_quest_log(GameState& gs,
                    std::vector<Quest>& quests,
                    QuestEngine& questEngine,
                    EventBus& bus,
                    int* selectedQuestIndex,
                    bool* open) {
    if (!open || !*open) return;

    int localSelected = 0;
    int& selected = selectedQuestIndex ? *selectedQuestIndex : localSelected;
    if (selected < 0) selected = 0;
    if (selected >= int(quests.size())) {
        selected = quests.empty() ? 0 : int(quests.size()) - 1;
    }

    int abandonIndex = -1;

    ImGui::SetNextWindowSize(ImVec2(640, 430), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Quest Journal", open)) {
        if (ImGui::Button("Close [Q]")) {
            *open = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Active: %zu  Done: %zu",
            quests.size(), gs.player.completedQuestIds.size());
        ImGui::Separator();

        if (quests.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 70.0f));
            ImGui::TextDisabled("No active quests. Visit a settlement to find work.");
        } else {
            constexpr float kListW = 190.0f;
            ImGui::BeginChild("##quest_list", ImVec2(kListW, 0.0f), true);
            for (int i = 0; i < int(quests.size()); ++i) {
                const Quest& q = quests[std::size_t(i)];
                ImGui::PushID(q.id.c_str());
                if (ImGui::Selectable(q.title.c_str(), selected == i)) {
                    selected = i;
                }
                ImGui::TextDisabled("%s", quest_category_label(q.category));
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("##quest_detail", ImVec2(0.0f, 0.0f), true);
            const Quest& q = quests[std::size_t(selected)];
            ImGui::Text("%s", q.title.c_str());
            ImGui::TextDisabled("%s  Difficulty: %d",
                quest_category_label(q.category), q.difficulty);
            if (q.expireDay >= 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("Expires: day %d", q.expireDay);
            }

            ImGui::Spacing();
            ImGui::TextWrapped("%s", q.description.c_str());
            ImGui::Separator();

            ImGui::TextUnformatted("Objectives");
            if (q.objectives.empty()) {
                ImGui::TextDisabled("(none)");
            } else {
                for (const auto& o : q.objectives) {
                    ImGui::TextDisabled("%s", objective_kind_label(o.kind));
                    ImGui::SameLine();
                    draw_objective_line(o);
                }
            }

            ImGui::Spacing();
            ImGui::TextUnformatted("Rewards");
            if (q.rewards.empty()) {
                ImGui::TextDisabled("(none)");
            } else {
                for (const auto& r : q.rewards) draw_reward_line(r);
            }

            ImGui::Separator();
            if (ImGui::Button("Abandon Quest")) {
                abandonIndex = selected;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Uses QuestEngine::abandon; emits QuestAbandoned and removes the active quest.");
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();

    if (abandonIndex >= 0 && abandonIndex < int(quests.size())) {
        const std::string id = quests[std::size_t(abandonIndex)].id;
        questEngine.abandon(quests, id, bus);
        if (selected >= int(quests.size())) {
            selected = quests.empty() ? 0 : int(quests.size()) - 1;
        }
    }
}

void draw_codex(GameState& gs, bool* open) {
    if (!open || !*open) return;
    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Codex", open)) {
        ImGui::Text("Unlocked entries: %zu", gs.player.codexUnlocked.size());
        ImGui::Separator();
        for (const auto& e : gs.player.codexUnlocked) ImGui::BulletText("%s", e.c_str());
    }
    ImGui::End();
}

void draw_show_dialog(const GameEvent& dialog, bool* open) {
    if (!open || !*open) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540.0f, 0.0f), ImGuiCond_Always);
    if (ImGui::Begin("Event", open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize)) {
        const char* title = dialog.s1.empty() ? "Event" : dialog.s1.c_str();
        const char* body = dialog.s2.empty() ? "(no description)" : dialog.s2.c_str();
        const int choiceCount = dialog.ix > 0 ? dialog.ix : 1;

        ImGui::Text("%s", title);
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(body);
        ImGui::PopTextWrapPos();
        ImGui::Separator();

        ImGui::TextDisabled("Choices in payload: %d", choiceCount);
        if (choiceCount > 1) {
            ImGui::BeginDisabled();
            ImGui::Button("Choice labels/effects unavailable", ImVec2(-FLT_MIN, 0.0f));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Missing backend: native ShowDialog GameEvent exposes only title, body, and choice count.");
            }
        }

        if (ImGui::Button("Continue", ImVec2(-FLT_MIN, 0.0f))) {
            *open = false;
        }
    }
    ImGui::End();
}

// Pre-rendered minimap cache. Built lazily on first open and rebuilt
// whenever the world seed changes (so loading a save refreshes the bitmap).
namespace {
struct MiniMapCache {
    GLuint        tex      = 0;
    int           w        = 0;
    int           h        = 0;
    std::uint32_t seed     = 0;
    int           srcW     = 0;
    int           srcH     = 0;
};

void build_minimap(MiniMapCache& mm, const TerrainData& terrain, std::uint32_t seed) {
    constexpr int kMaxSide = 256;
    int side = std::min(kMaxSide, std::max(terrain.width, terrain.height));
    int W = side, H = side;
    std::vector<std::uint8_t> rgba(std::size_t(W) * H * 4);
    const int srcW = terrain.width, srcH = terrain.height;
    for (int y = 0; y < H; ++y) {
        // Y-flip so the minimap matches the macroworld viewport convention
        // (world +Y is screen UP). Pixel y=0 (top) shows world y=H-1.
        int sy = std::min(srcH - 1, (H - 1 - y) * srcH / H);
        for (int x = 0; x < W; ++x) {
            int sx = std::min(srcW - 1, x * srcW / W);
            std::size_t s = std::size_t(sy) * srcW + sx;
            std::uint8_t hb   = terrain.rgba[s * 4 + 0];
            std::uint8_t mb   = terrain.rgba[s * 4 + 1];
            std::uint8_t tb   = terrain.rgba[s * 4 + 2];
            std::uint8_t mask = terrain.rgba[s * 4 + 3];
            float r, g, b;
            if (!mask || hb < std::uint8_t(0.40f * 255.0f)) {
                // Water — depth shading.
                float depth = 1.0f - (float(hb) / (0.40f * 255.0f));
                if (depth < 0) depth = 0; if (depth > 1) depth = 1;
                r = 0.10f + 0.08f * (1.0f - depth);
                g = 0.20f + 0.20f * (1.0f - depth);
                b = 0.40f + 0.20f * (1.0f - depth);
            } else {
                Biome bi = biome_from_climate(float(tb) / 255.0f, float(mb) / 255.0f);
                const auto& def = kBiomes[std::size_t(bi)];
                r = def.r; g = def.g; b = def.b;
                // Subtle altitude shading.
                float h01 = float(hb) / 255.0f;
                float shade = 0.85f + (h01 - 0.5f) * 0.35f;
                r *= shade; g *= shade; b *= shade;
            }
            std::size_t d = std::size_t(y) * W + x;
            rgba[d * 4 + 0] = std::uint8_t(std::clamp(r, 0.0f, 1.0f) * 255.0f);
            rgba[d * 4 + 1] = std::uint8_t(std::clamp(g, 0.0f, 1.0f) * 255.0f);
            rgba[d * 4 + 2] = std::uint8_t(std::clamp(b, 0.0f, 1.0f) * 255.0f);
            rgba[d * 4 + 3] = 255;
        }
    }
    if (mm.tex) glDeleteTextures(1, &mm.tex);
    mm.tex  = gl_make_texture_rgba8(W, H, rgba.data(), GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    mm.w    = W;
    mm.h    = H;
    mm.seed = seed;
    mm.srcW = srcW;
    mm.srcH = srcH;
}
} // namespace

void draw_map_overlay(GameState& gs, const TerrainData& terrain, bool* open) {
    if (!open || !*open) return;
    static MiniMapCache mm;
    if (mm.tex == 0 || mm.seed != gs.worldSeed
        || mm.srcW != terrain.width || mm.srcH != terrain.height) {
        build_minimap(mm, terrain, gs.worldSeed);
    }

    ImGui::SetNextWindowSize(ImVec2(620, 720), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Map", open)) {
        ImGui::Text("Size: %d x %d   Seed: 0x%X",
            terrain.width, terrain.height, gs.worldSeed);
        ImGui::Text("Player: %.1f, %.1f", gs.player.x, gs.player.y);
        ImGui::Text("Cities: %zu  Villages: %zu",
            gs.politik.cities.size(), gs.villages.size());
        ImGui::Separator();

        // Available width drives the on-screen size; keep aspect.
        float avail = ImGui::GetContentRegionAvail().x;
        float disp  = std::min(avail, 560.0f);
        ImVec2 size(disp, disp * float(mm.h) / float(mm.w));
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(mm.tex), size);

        // Overlay markers — convert world coords to image-space pixels.
        // Minimap is Y-flipped (world +Y → screen UP), so marker Y mirrors.
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        const float sx  = size.x / float(terrain.width);
        const float sy  = size.y / float(terrain.height);
        auto wy = [&](int worldY) {
            return origin.y + (float(terrain.height - 1 - worldY)) * sy;
        };
        // Cities (yellow rings).
        for (const auto& c : gs.politik.cities) {
            ImVec2 p(origin.x + float(c.x) * sx, wy(c.y));
            dl->AddCircleFilled(p, 3.0f, IM_COL32(255, 220, 90, 230), 8);
            dl->AddCircle(p, 3.5f, IM_COL32(40, 30, 0, 255), 8, 1.0f);
        }
        // Villages (small brown dots).
        for (const auto& v : gs.villages) {
            ImVec2 p(origin.x + float(v.x) * sx, wy(v.y));
            dl->AddCircleFilled(p, 1.6f, IM_COL32(180, 140, 90, 220), 6);
        }
        // Player crosshair (cyan).
        {
            ImVec2 p(origin.x + gs.player.x * sx,
                     origin.y + (float(terrain.height - 1) - gs.player.y) * sy);
            dl->AddCircleFilled(p, 3.0f, IM_COL32(80, 240, 240, 255), 10);
            dl->AddLine(ImVec2(p.x - 7, p.y), ImVec2(p.x + 7, p.y),
                        IM_COL32(80, 240, 240, 255), 1.2f);
            dl->AddLine(ImVec2(p.x, p.y - 7), ImVec2(p.x, p.y + 7),
                        IM_COL32(80, 240, 240, 255), 1.2f);
        }
    }
    ImGui::End();
}

// ── Encounter modal ──────────────────────────────────────────
void draw_encounter_modal(GameState& gs, EventBus& bus) {
    if (gs.subState.kind != GameSubStateKind::Event) return;
    const auto& table = content::encounters();
    int idx = gs.subState.pendingEncounterIdx;
    if (idx < 0 || idx >= int(table.size())) {
        gs.subState.kind = GameSubStateKind::Exploring;
        gs.subState.pendingEncounterIdx = -1;
        return;
    }
    const auto& enc = table[size_t(idx)];

    ImGui::OpenPopup("Encounter");
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (ImGui::BeginPopupModal("Encounter", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", enc.title.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", enc.body.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        for (size_t i = 0; i < enc.choices.size(); ++i) {
            const auto& ch = enc.choices[i];
            ImGui::PushID(int(i));
            if (ImGui::Button(ch.label.c_str(), ImVec2(-1, 0))) {
                bus.emit_all(ch.effects);
                gs.subState.kind = GameSubStateKind::Exploring;
                gs.subState.pendingEncounterIdx = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- Subworld minimap

namespace {

// Cache for the downsampled subworld tile bitmap. Rebuilds when the
// seamless centre cell changes (player crossed a cell boundary) or after
// kRebuildIntervalSec to pick up player-driven mutations (e.g. felled
// trees). Side is fixed; both HUD and full-page draws sample the same
// texture at different sizes.
struct SubMiniMapCache {
    GLuint  tex          = 0;
    int     side         = 384;            // 384² downsample of 3072² (8x)
    int     centerCx     = INT32_MIN;
    int     centerCy     = INT32_MIN;
    double  lastBuildSec = -1e9;
};

ImU32 subworld_tile_color(std::uint8_t t) {
    using namespace sub;
    switch (t) {
    case TILE_WATER:      return IM_COL32( 60, 110, 180, 255);
    case TILE_SHORE:      return IM_COL32(210, 195, 150, 255);
    case TILE_GRASS:      return IM_COL32( 90, 140,  70, 255);
    case TILE_FIELD:      return IM_COL32(180, 170,  90, 255);
    case TILE_TREE_DECOR: return IM_COL32( 40,  85,  45, 255);
    case TILE_ROAD:       return IM_COL32(165, 135,  95, 255);
    case TILE_HOUSE:      return IM_COL32(150, 110,  80, 255);
    case TILE_WALL:       return IM_COL32(120, 120, 125, 255);
    case TILE_SQUARE:     return IM_COL32(190, 190, 180, 255);
    case TILE_ROCK:       return IM_COL32( 95,  95, 100, 255);
    case TILE_EMPTY:      [[fallthrough]];
    default:              return IM_COL32( 70,  90,  60, 255);
    }
}

void build_sub_minimap(SubMiniMapCache& mm, const sub::SeamlessSubworldManager& mgr) {
    const auto& tiles = mgr.tiles();
    const auto& heights = mgr.heightmap();
    if (tiles.size() != std::size_t(sub::kFullSize) * sub::kFullSize) return;
    if (heights.size() != tiles.size()) return;
    const int side = mm.side;
    std::vector<std::uint8_t> rgba(std::size_t(side) * side * 4);
    const int src = sub::kFullSize;
    for (int y = 0; y < side; ++y) {
        // Y-flip so world +Y points UP on the minimap.
        const int sy = std::min(src - 1, (side - 1 - y) * src / side);
        for (int x = 0; x < side; ++x) {
            const int sx = std::min(src - 1, x * src / side);
            const std::uint8_t t = tiles[std::size_t(sy) * src + sx];
            const ImU32 base = subworld_tile_color(t);
            float h = heights[std::size_t(sy) * src + sx];
            // Relief shading: blend color with elevation (0.0 = deep, 2.0 = high)
            float shade = 0.85f + 0.35f * std::clamp((h - 0.5f) / 1.5f, 0.0f, 1.0f);
            std::size_t d = (std::size_t(y) * side + x) * 4;
            rgba[d + 0] = std::uint8_t(std::clamp(float(base & 0xFF) * shade, 0.0f, 255.0f));
            rgba[d + 1] = std::uint8_t(std::clamp(float((base >> 8) & 0xFF) * shade, 0.0f, 255.0f));
            rgba[d + 2] = std::uint8_t(std::clamp(float((base >> 16) & 0xFF) * shade, 0.0f, 255.0f));
            rgba[d + 3] = std::uint8_t((base >> 24) & 0xFF);
        }
    }
    if (mm.tex) glDeleteTextures(1, &mm.tex);
    mm.tex = gl_make_texture_rgba8(side, side, rgba.data(),
                                   GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    mm.centerCx     = mgr.center_cx();
    mm.centerCy     = mgr.center_cy();
    mm.lastBuildSec = ImGui::GetTime();
}

SubMiniMapCache& sub_minimap_cache() {
    static SubMiniMapCache mm;
    return mm;
}

void ensure_sub_minimap(const sub::SeamlessSubworldManager& mgr) {
    auto& mm = sub_minimap_cache();
    const double now = ImGui::GetTime();
    const bool centerChanged =
        mm.centerCx != mgr.center_cx() || mm.centerCy != mgr.center_cy();
    const bool stale = (now - mm.lastBuildSec) > 2.0;
    if (mm.tex == 0 || centerChanged || stale) {
        build_sub_minimap(mm, mgr);
    }
}

// Convert (worldX, worldY) in [0..kFullSize] to image-space UVs in [0..1].
// Y-flipped to match build_sub_minimap.
ImVec2 sub_world_to_uv(float worldX, float worldY) {
    const float fs = float(sub::kFullSize);
    float u = std::clamp(worldX / fs, 0.0f, 1.0f);
    float v = std::clamp(1.0f - (worldY / fs), 0.0f, 1.0f);
    return ImVec2(u, v);
}

} // namespace

void draw_subworld_minimap_hud(const sub::SeamlessSubworldManager& mgr,
                               float playerX, float playerY,
                               float cameraYaw,
                               int viewportW, int viewportH) {
    ensure_sub_minimap(mgr);
    auto& mm = sub_minimap_cache();
    if (!mm.tex) return;

    constexpr float kRadius = 70.0f;
    constexpr float kMargin = 16.0f;
    // Window-space coords (ImGui foreground draw list is in window coords).
    ImVec2 center(float(viewportW) - kMargin - kRadius, kMargin + kRadius);

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Sample a window around the player covering ~one macro cell so the
    // HUD shows tactical surroundings without losing detail. The window is
    // a square in world space whose half-extent is kHalfTiles.
    constexpr float kHalfTiles = float(sub::kCellSize) / 2.0f; // ~512 tiles
    const ImVec2 puv = sub_world_to_uv(playerX, playerY);
    const float duv = kHalfTiles / float(sub::kFullSize);
    ImVec2 uv0(std::clamp(puv.x - duv, 0.0f, 1.0f),
               std::clamp(puv.y - duv, 0.0f, 1.0f));
    ImVec2 uv1(std::clamp(puv.x + duv, 0.0f, 1.0f),
               std::clamp(puv.y + duv, 0.0f, 1.0f));

    // Draw the minimap as a rotated quad so 'up' is always forward.
    // Compute the four corners of the quad, rotated by cameraYaw.
    ImVec2 quad[4];
    float ca = std::cos(cameraYaw), sa = std::sin(cameraYaw);
    float r = kRadius;
    // Corners: (-r,-r), (+r,-r), (+r,+r), (-r,+r)
    for (int i = 0; i < 4; ++i) {
        float x = ((i == 0 || i == 3) ? -r : r);
        float y = ((i < 2) ? -r : r);
        float rx = x * ca - y * sa;
        float ry = x * sa + y * ca;
        quad[i] = ImVec2(center.x + rx, center.y + ry);
    }
    // UVs: (uv0.x,uv0.y), (uv1.x,uv0.y), (uv1.x,uv1.y), (uv0.x,uv1.y)
    ImVec2 uvs[4] = {ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv0.y), ImVec2(uv1.x, uv1.y), ImVec2(uv0.x, uv1.y)};

    // Draw the minimap quad first.
    dl->AddImageQuad(static_cast<ImTextureID>(mm.tex),
        quad[0], quad[1], quad[2], quad[3],
        uvs[0], uvs[1], uvs[2], uvs[3]);
    // Draw the minimap quad first.
    dl->AddImageQuad(static_cast<ImTextureID>(mm.tex),
        quad[0], quad[1], quad[2], quad[3],
        uvs[0], uvs[1], uvs[2], uvs[3]);
    // True circular clipping: overlay a filled black circle with alpha to mask corners.
    dl->AddCircleFilled(center, kRadius + 1.0f, IM_COL32(0, 0, 0, 180), 64);
    dl->AddCircle(center, kRadius + 0.5f, IM_COL32(20, 20, 20, 230), 48, 3.0f);
    dl->AddCircle(center, kRadius - 1.5f, IM_COL32(220, 200, 140, 200), 48, 1.0f);
    // Border ring + frame (drawn after mask for clarity).
    dl->AddCircle(center, kRadius + 0.5f, IM_COL32(20, 20, 20, 230), 64, 3.0f);
    dl->AddCircle(center, kRadius - 1.5f, IM_COL32(220, 200, 140, 200), 64, 1.0f);

    // Player marker — always points up (forward on rotated minimap).
    const float ms = 7.0f;
    ImVec2 p0(center.x, center.y - ms);
    ImVec2 p1(center.x - ms * 0.6f, center.y + ms * 0.7f);
    ImVec2 p2(center.x + ms * 0.6f, center.y + ms * 0.7f);
    dl->AddTriangleFilled(p0, p1, p2, IM_COL32(255, 240, 100, 255));
    dl->AddTriangle(p0, p1, p2, IM_COL32(20, 20, 20, 230), 1.5f);

    (void)viewportH; // top-right anchored — height unused for now.
}

void draw_subworld_map_overlay(const sub::SeamlessSubworldManager& mgr,
                               float playerX, float playerY,
                               float cameraYaw,
                               bool* open) {
    if (!open || !*open) return;
    ensure_sub_minimap(mgr);
    auto& mm = sub_minimap_cache();
    if (!mm.tex) return;

    ImGui::SetNextWindowSize(ImVec2(680, 760), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Subworld Map", open)) {
        ImGui::Text("Centre cell: (%d, %d)", mgr.center_cx(), mgr.center_cy());
        ImGui::Text("Player (subworld): %.1f, %.1f", playerX, playerY);
        ImGui::Separator();

        const float avail = ImGui::GetContentRegionAvail().x;
        const float disp  = std::min(avail, 620.0f);
        const ImVec2 size(disp, disp);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        // Draw the minimap as a rotated quad (same as HUD, but larger)
        ImVec2 center = ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
        float r = size.x * 0.5f;
        float ca = std::cos(cameraYaw), sa = std::sin(cameraYaw);
        ImVec2 quad[4];
        for (int i = 0; i < 4; ++i) {
            float x = ((i == 0 || i == 3) ? -r : r);
            float y = ((i < 2) ? -r : r);
            float rx = x * ca - y * sa;
            float ry = x * sa + y * ca;
            quad[i] = ImVec2(center.x + rx, center.y + ry);
        }
        ImVec2 uvs[4] = {ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1)};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Draw minimap quad first
        dl->AddImageQuad(static_cast<ImTextureID>(mm.tex),
            quad[0], quad[1], quad[2], quad[3],
            uvs[0], uvs[1], uvs[2], uvs[3]);
        // Draw minimap quad first
        dl->AddImageQuad(static_cast<ImTextureID>(mm.tex),
            quad[0], quad[1], quad[2], quad[3],
            uvs[0], uvs[1], uvs[2], uvs[3]);
        // True circular clipping: overlay a filled black circle with alpha to mask corners.
        dl->AddCircleFilled(center, r + 1.0f, IM_COL32(0, 0, 0, 180), 64);
        dl->AddCircle(center, r + 0.5f, IM_COL32(20, 20, 20, 200), 64, 2.0f);
        // Border ring
        dl->AddCircle(center, r + 0.5f, IM_COL32(20, 20, 20, 230), 64, 2.5f);
        // 3×3 cell border lines (rotated)
        const ImU32 cellLine = IM_COL32(20, 20, 20, 140);
        for (int i = 1; i < 3; ++i) {
            float t = float(i) / 3.0f;
            for (int j = 0; j < 2; ++j) {
                float tx = (j == 0) ? -r + t * 2 * r : r - t * 2 * r;
                float ty = (j == 0) ? -r : r;
                float x0 = tx * ca - ty * sa;
                float y0 = tx * sa + ty * ca;
                float x1 = tx * ca - (-ty) * sa;
                float y1 = tx * sa + (-ty) * ca;
                dl->AddLine(ImVec2(center.x + x0, center.y + y0), ImVec2(center.x + x1, center.y + y1), cellLine, 1.0f);
            }
        }
        // Player marker (always points up)
        ImVec2 puv = sub_world_to_uv(playerX, playerY);
        ImVec2 pp(center.x + (puv.x - 0.5f) * size.x, center.y + (puv.y - 0.5f) * size.y);
        dl->AddCircleFilled(pp, 7.0f, IM_COL32(255, 240, 100, 255), 16);
        dl->AddCircle(pp, 8.0f, IM_COL32(20, 20, 20, 230), 16, 2.0f);
    }
    ImGui::End();
}

} // namespace sm::ui
