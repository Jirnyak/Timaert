#include "ui/overlays.h"
#include "ui/screens.h"   // kTopStatusBarHeight — keep the minimap below the top bar
#include "macro/map_generator.h"
#include "macro/biomes.h"
#include "macro/npc.h"
#include "macro/economy.h"
#include "macro/faction.h"
#include "macro/items.h"
#include "macro/politik.h"
#include "content/spells/spell_book.h"
#include "content/spells/spell_types.h"
#include "content/plot/encounters.h"
#include "content/plot/intro.h"
#include "events/event_bus.h"
#include "ui/ui_gpu.h"
#include "imgui.h"
#include "sub/base_generator.h"
#include "sub/gens/dispatch.h"
#include "sub/seamless_manager.h"
#include "sub/engine.h"
#include "sub/map_data.h"
#include <stb_image.h>
#include <algorithm>
#include <cstdlib>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace sm::ui
{
    namespace
    {
        struct StoryTexture
        {
            const char *key = nullptr;
            ImTextureID tex = 0;
            int w = 0;
            int h = 0;
            bool tried = false;
        };

        constexpr std::size_t kStoryTextureCapacity = 16;
        std::array<StoryTexture, kStoryTextureCapacity> g_storyTextures{};

        bool story_ui_trace_enabled()
        {
            static const bool enabled = std::getenv("TIMAERT_STORY_UI_TRACE") != nullptr;
            return enabled;
        }

        float modal_width(const ImGuiViewport *vp,
                          float preferred,
                          float minimum)
        {
            const float workW = vp ? vp->WorkSize.x : preferred;
            if (workW <= 0.0f)
                return preferred;
            const float margin = workW > minimum + 40.0f ? 40.0f : 16.0f;
            const float floor = std::min(minimum, 160.0f);
            const float available = std::min(workW, std::max(floor, workW - margin));
            return std::min(preferred, available);
        }

        void copy_cstr(char *dst, std::size_t cap, const char *src)
        {
            if (!dst || cap == 0)
                return;
            if (!src)
                src = "";
            std::snprintf(dst, cap, "%s", src);
        }

        const char *asset_rel_path(const char *path)
        {
            if (!path)
                return "";
            return path[0] == '/' ? path + 1 : path;
        }

        unsigned char *load_story_pixels(const char *path, int &w, int &h)
        {
            static const char *kPrefixes[] = {
                "",
                "../",
                "../public/",
                "../../public/",
                "public/",
            };

            const char *rel = asset_rel_path(path);
            char candidate[512];
            int comp = 0;
            for (const char *prefix : kPrefixes)
            {
                const int n = std::snprintf(candidate, sizeof(candidate),
                                            "%s%s", prefix, rel);
                if (n <= 0 || std::size_t(n) >= sizeof(candidate))
                    continue;
                unsigned char *px = stbi_load(candidate, &w, &h, &comp, 4);
                if (px)
                    return px;
            }
            return nullptr;
        }

        const StoryTexture *story_texture_for(const char *path)
        {
            if (!path || path[0] == '\0')
                return nullptr;

            StoryTexture *freeSlot = nullptr;
            for (StoryTexture &slot : g_storyTextures)
            {
                if (slot.key && std::strcmp(slot.key, path) == 0)
                    return slot.tex ? &slot : nullptr;
                if (!slot.key && !freeSlot)
                    freeSlot = &slot;
            }
            if (!freeSlot)
                return nullptr;

            freeSlot->key = path;
            freeSlot->tried = true;
            int w = 0;
            int h = 0;
            unsigned char *px = load_story_pixels(path, w, h);
            if (!px)
            {
                if (story_ui_trace_enabled())
                    std::fprintf(stderr, "[story-ui] missing image: %s\n", path);
                return nullptr;
            }

            freeSlot->tex = create_ui_texture(w, h, px,
                                               /*linear=*/true);
            freeSlot->w = w;
            freeSlot->h = h;
            stbi_image_free(px);
            if (!freeSlot->tex)
                return nullptr;
            if (story_ui_trace_enabled())
                std::fprintf(stderr, "[story-ui] loaded image %s (%dx%d) tex=%u\n",
                             path, w, h, static_cast<unsigned>(freeSlot->tex));
            return freeSlot;
        }

        void draw_story_image(const StoryTexture &tex,
                              float maxW,
                              float maxH,
                              bool center)
        {
            if (tex.w <= 0 || tex.h <= 0 || tex.tex == 0)
                return;
            const float scale = std::min(maxW / float(tex.w), maxH / float(tex.h));
            const ImVec2 size(float(tex.w) * scale, float(tex.h) * scale);
            if (center && size.x < maxW)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (maxW - size.x) * 0.5f);
            ImGui::Image(tex.tex, size);
        }

        void clear_story_slots(StoryOverlayState &state)
        {
            state.selectedChoice.fill(-1);
            state.hasValue.fill(false);
            for (auto &value : state.values)
                value[0] = '\0';
        }

        void trim_or_default(char *buf, std::size_t cap, const char *fallback)
        {
            if (!buf || cap == 0)
                return;
            std::size_t len = std::strlen(buf);
            while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' ||
                               buf[len - 1] == '\r' || buf[len - 1] == '\n'))
            {
                buf[--len] = '\0';
            }
            std::size_t first = 0;
            while (buf[first] == ' ' || buf[first] == '\t' ||
                   buf[first] == '\r' || buf[first] == '\n')
            {
                ++first;
            }
            if (first > 0)
            {
                std::memmove(buf, buf + first, len - first + 1);
                len -= first;
            }
            if (len == 0)
                copy_cstr(buf, cap, fallback && fallback[0] ? fallback : "Traveller");
        }

        std::size_t story_input_capacity(const content::StoryPhaseDef &phase)
        {
            if (phase.maxLength > 0)
            {
                const std::size_t capped =
                    static_cast<std::size_t>(phase.maxLength) + 1u;
                return std::min(kStoryOverlayMaxText, capped);
            }
            return std::min<std::size_t>(kStoryOverlayMaxText, 33u);
        }

        void set_dialog_result(DialogOverlayState &state, const char *message)
        {
            state.hasResult = true;
            copy_cstr(state.resultMessage.data(), state.resultMessage.size(),
                      message && message[0] ? message : "Continue");
        }

        void request_dialog_node_activation(DialogOverlayState &state,
                                            const std::string &nodeId)
        {
            if (nodeId.empty())
                return;
            state.hasNodeActivation = true;
            copy_cstr(state.nodeId.data(), state.nodeId.size(), nodeId.c_str());
        }

        int dialog_choice_gold_cost(const DialogChoicePayload &choice)
        {
            int cost = 0;
            for (const GameEvent &effect : choice.effects)
            {
                if (effect.tag == EventTag::PlayerGoldChange && effect.ix < 0)
                    cost += -effect.ix;
            }
            return cost;
        }

        void log_dialog_choice(GameState &gs,
                               const GameEvent &dialog,
                               const DialogChoicePayload &choice)
        {
            LogEntry entry{};
            entry.type = LogType::World;
            entry.day = gs.worldTime.day;
            entry.message = "Event: ";
            entry.message += dialog.s1.empty() ? "Event" : dialog.s1;
            entry.message += " - ";
            entry.message += choice.label.empty() ? "Continue" : choice.label;
            gs.player.eventLog.push_back(std::move(entry));
        }

        void complete_story(StoryOverlayState &state, EventBus &bus)
        {
            if (!state.story)
                return;

            GameEvent result{EventTag::StoryResult};
            result.storyResult = std::make_shared<StoryResultPayload>();
            result.storyResult->sourceNodeId =
                state.story->sourceNodeId ? state.story->sourceNodeId : "";
            result.storyResult->storyId = state.story->id ? state.story->id : "";

            for (std::size_t i = 0; i < state.story->phaseCount &&
                                    i < kStoryOverlayMaxPhases; ++i)
            {
                const content::StoryPhaseDef &phase = state.story->phases[i];
                if (!phase.id || !state.hasValue[i])
                    continue;
                result.storyResult->values.emplace_back(phase.id, state.values[i].data());
            }

            bus.emit(result);
            state.open = false;
            state.story = nullptr;
        }

        void advance_story_phase(StoryOverlayState &state, EventBus &bus)
        {
            if (!state.story)
                return;
            if (state.phaseIndex + 1 < state.story->phaseCount)
            {
                ++state.phaseIndex;
                state.slideIndex = 0;
                state.inputPrepared = false;
            }
            else
            {
                complete_story(state, bus);
            }
        }

        struct CodexArticle
        {
            const char *id;
            const char *title;
            const char *content;
        };

        struct CodexCategory
        {
            const char *id;
            const char *title;
            const CodexArticle *articles;
            std::size_t count;
        };

        constexpr CodexArticle kCodexLore[] = {
            {
                "cosmology",
                "Cosmology",
                "Torus world created by dead gods.\n\n"
                "Two opposing forces:\n"
                "- Pure Magic: natural, impersonal, knowable energy.\n"
                "- Black Force: void/negation of people's desires and dead gods' whispers.\n\n"
                "When they meet, they annihilate each other. Black artifacts of dead gods exist and destabilize magic.\n\n"
                "Central Prophecy: A \"Black Child\" will be born, marking the end of the Pure Magic era.",
            },
            {
                "mage_rulers",
                "Mage-Rulers",
                "The Magocracy of the Remnants of Magika.\n\n"
                "Arrogant and powerful mages - dukes, lords, archmages - rule the remnants of the Magika kingdoms. "
                "They exploit common people, considering them unworthy of true Pure Magic. In the kingdoms of Magika, "
                "magic is widespread and the primary measure of power. Even simple peasants possess basic spells. "
                "For elite mages, a peasant is a tool, a resource, expendable material.",
            },
            {
                "empire_of_light",
                "Empire of Light",
                "Theocratic empire. Magic is strictly forbidden under death penalty. Public religion; private corruption. "
                "Uses elite anti-mage warriors.\n\n"
                "Great Eunuchs (Shadow Rulers): 13 secret rulers. Publicly religious leaders; secretly serve black cults. "
                "They manipulate prophecy to prepare the world's transition toward the Black Child.",
            },
            {
                "witches",
                "The Witches",
                "Immortal System Entities. Cannot be permanently killed; they reincarnate. Represent metaphysical principles:\n\n"
                "- Nefesh (Life): birth, transformation, cycle. Assigns arbitrary tasks.\n"
                "- Ain (Void): entropy and dissolution. Appears near ruined areas.\n"
                "- Tiferet (Present): \"now\". Focused on immediate action.\n"
                "- Hokma (Memory): recorded past. Knows everything that was written or marked.",
            },
        };

        constexpr CodexArticle kCodexMechanics[] = {
            {
                "attributes",
                "Attributes",
                "Eight primary attributes shape your character:\n\n"
                "- STR (Strength): +1 physical damage per point.\n"
                "- VIT (Vitality): +10 max HP per point.\n"
                "- END (Endurance): +10 max SP per point.\n"
                "- WIL (Willpower): +10 max MP per point.\n"
                "- INT (Intelligence): +1 spell damage per point.\n"
                "- WIS (Wisdom): +1% EXP bonus per point.\n"
                "- LCK (Luck): crit scaling and better loot.\n"
                "- CHA (Charisma): trade discount and relation bonus.\n"
                "- SPD (Speed): movement speed with asymptotic scaling.",
            },
            {
                "perks_skills",
                "Skills & Perks",
                "Skills provide flat base stat increases applied before attribute-based multipliers. They do not modify "
                "attributes directly. Examples include Bodybuilding and martial disciplines.\n\n"
                "Perks are powerful, build-defining choices that provide both significant advantages and disadvantages. "
                "They are gained at level 1 and every 10 levels. Example: \"Immortal\" prevents death from old age, "
                "but requires much more experience to level up.",
            },
        };

        constexpr CodexArticle kCodexEconomy[] = {
            {
                "market",
                "Market System",
                "No global market. All trade is local and emergent. Prices fluctuate based on local supply and demand.\n\n"
                "Demand factor rises when demand outpaces supply, raising the target price. Charisma reduces the commission "
                "when buying from NPCs.",
            },
            {
                "settlements",
                "Settlements & Caravans",
                "Villages gather resources via peasant squads. They store inventory locally and sell to caravans and cities.\n\n"
                "Cities buy resources, produce goods through production chains, spawn caravans for trade, and collect taxes "
                "based on population and trade volume.\n\n"
                "Caravans spawn at cities, load surplus goods, and travel using pathfinding toward destinations with strong "
                "profit estimates.",
            },
        };

        constexpr CodexCategory kCodexCategories[] = {
            {"lore", "Lore & World", kCodexLore, std::size(kCodexLore)},
            {"mechanics", "RPG Mechanics", kCodexMechanics, std::size(kCodexMechanics)},
            {"economy", "Economics", kCodexEconomy, std::size(kCodexEconomy)},
        };

        bool codex_unlocked(const PlayerState &player, const char *id)
        {
            return std::find(player.codexUnlocked.begin(),
                             player.codexUnlocked.end(),
                             id) != player.codexUnlocked.end();
        }

        int first_unlocked_article_index(const PlayerState &player,
                                         const CodexCategory &cat)
        {
            for (std::size_t i = 0; i < cat.count; ++i)
            {
                if (codex_unlocked(player, cat.articles[i].id))
                    return int(i);
            }
            return -1;
        }

        void ensure_codex_selection(const PlayerState &player,
                                    int &category,
                                    int &article)
        {
            const int catCount = int(std::size(kCodexCategories));
            if (category >= 0 && category < catCount)
            {
                const CodexCategory &cat = kCodexCategories[std::size_t(category)];
                if (article >= 0 && article < int(cat.count) &&
                    codex_unlocked(player, cat.articles[std::size_t(article)].id))
                {
                    return;
                }
            }

            for (int ci = 0; ci < catCount; ++ci)
            {
                const int ai = first_unlocked_article_index(
                    player, kCodexCategories[std::size_t(ci)]);
                if (ai >= 0)
                {
                    category = ci;
                    article = ai;
                    return;
                }
            }

            category = -1;
            article = -1;
        }
    }

    void open_story_overlay(StoryOverlayState &state, const content::StoryDef &story)
    {
        state.open = true;
        state.story = &story;
        state.phaseIndex = 0;
        state.slideIndex = 0;
        state.inputPrepared = false;
        clear_story_slots(state);
    }

    bool story_overlay_active(const StoryOverlayState &state)
    {
        return state.open && state.story != nullptr;
    }

    bool set_story_overlay_value(StoryOverlayState &state,
                                 const char *phaseId,
                                 const char *value)
    {
        if (!story_overlay_active(state) || !phaseId || !phaseId[0])
            return false;

        const content::StoryDef &story = *state.story;
        for (std::size_t i = 0; i < story.phaseCount &&
                                i < kStoryOverlayMaxPhases; ++i)
        {
            const content::StoryPhaseDef &phase = story.phases[i];
            if (!phase.id || std::strcmp(phase.id, phaseId) != 0)
                continue;

            const std::size_t cap = phase.kind == content::StoryPhaseKind::Input
                ? story_input_capacity(phase)
                : kStoryOverlayMaxText;
            copy_cstr(state.values[i].data(), cap, value ? value : "");
            if (phase.kind == content::StoryPhaseKind::Input)
                trim_or_default(state.values[i].data(), cap, phase.defaultValue);

            if (phase.kind == content::StoryPhaseKind::Choice &&
                phase.choices && phase.choiceCount > 0)
            {
                state.selectedChoice[i] = -1;
                for (std::size_t c = 0; c < phase.choiceCount; ++c)
                {
                    const char *choiceValue = phase.choices[c].value
                        ? phase.choices[c].value
                        : "";
                    if (std::strcmp(choiceValue, state.values[i].data()) == 0)
                    {
                        state.selectedChoice[i] = int(c);
                        break;
                    }
                }
            }
            state.hasValue[i] = true;
            return true;
        }
        return false;
    }

    bool complete_story_overlay(StoryOverlayState &state, EventBus &bus)
    {
        if (!story_overlay_active(state))
            return false;
        complete_story(state, bus);
        return true;
    }

    void draw_diplomacy(GameState &gs, bool *open, float scale)
    {
        if (!open || !*open)
            return;
        ImGui::SetNextWindowSize(ImVec2(420 * scale, 380 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Diplomacy", open))
        {
            ImGui::SetWindowFontScale(scale);
            ImGui::Text("Factions: %zu", gs.factions.size());
            ImGui::Separator();
            for (const auto &[id, f] : gs.factions)
            {
                ImGui::Text("%-16s  rep:%4d", f.name.c_str(),
                            gs.player.reputation.count(id) ? gs.player.reputation.at(id) : 0);
            }
            ImGui::Separator();
            ImGui::Text("Kingdoms: %zu", gs.politik.kingdoms.size());
            for (const auto &k : gs.politik.kingdoms)
            {
                ImGui::Text("  - %s  (%zu cities)", k.name.c_str(), k.cityIdxs.size());
            }
        }
        ImGui::End();
    }

    namespace
    {
        constexpr int npc_type_count()
        {
            return static_cast<int>(NPCType::Count);
        }

        constexpr NPCType npc_type_at(int idx)
        {
            return static_cast<NPCType>(static_cast<std::uint8_t>(idx));
        }

        const SoldierRecord* first_soldier_of_kind(const SoldierSquad& squad,
                                                   NPCType kind)
        {
            const std::uint8_t raw = static_cast<std::uint8_t>(kind);
            for (const SoldierRecord& soldier : squad.members)
            {
                if (soldier.kind == raw)
                    return &soldier;
            }
            return nullptr;
        }

        const char *mood_label(SettlementMood m)
        {
            switch (m)
            {
            case SettlementMood::Prosperous:
                return "Prosperous";
            case SettlementMood::Stable:
                return "Stable";
            case SettlementMood::Tense:
                return "Tense";
            case SettlementMood::Unrest:
                return "Unrest";
            case SettlementMood::Revolt:
                return "Revolt";
            }
            return "?";
        }

        ImU32 mood_color(SettlementMood m)
        {
            switch (m)
            {
            case SettlementMood::Prosperous:
                return IM_COL32(90, 220, 120, 255);
            case SettlementMood::Stable:
                return IM_COL32(220, 220, 220, 255);
            case SettlementMood::Tense:
                return IM_COL32(240, 200, 80, 255);
            case SettlementMood::Unrest:
                return IM_COL32(220, 130, 80, 255);
            case SettlementMood::Revolt:
                return IM_COL32(230, 70, 70, 255);
            }
            return IM_COL32(220, 220, 220, 255);
        }

        int rest_cost(SettlementMood m)
        {
            switch (m)
            {
            case SettlementMood::Prosperous:
                return 5;
            case SettlementMood::Stable:
                return 10;
            case SettlementMood::Tense:
                return 15;
            case SettlementMood::Unrest:
                return 20;
            case SettlementMood::Revolt:
                return 30;
            }
            return 10;
        }

        int g_settlement_trade_message_id = -1;
        char g_settlement_trade_message[160] = "";

        void clear_settlement_trade_message_for(int settlementId)
        {
            if (g_settlement_trade_message_id == settlementId)
                return;
            g_settlement_trade_message_id = settlementId;
            g_settlement_trade_message[0] = '\0';
        }

        void set_settlement_trade_message(const char *verb,
                                          const ItemDef &item,
                                          int price)
        {
            std::snprintf(g_settlement_trade_message,
                          sizeof(g_settlement_trade_message),
                          "%s %s for %d g", verb, item.name, price);
        }

        void push_settlement_trade_log(GameState &gs,
                                       const char *verb,
                                       const ItemDef &item,
                                       const char *settlementName,
                                       int price)
        {
            char message[192];
            std::snprintf(message, sizeof(message), "%s %s %s %s for %d g",
                          verb, item.name,
                          verb[0] == 'B' ? "from" : "to",
                          settlementName ? settlementName : "settlement",
                          price);
            gs.player.eventLog.push_back({LogType::Economy, message, gs.worldTime.day});
        }

        int trade_overlay_buy_price(int baseValue, float tradeDiscount, SettlementMood mood)
        {
            float mult = 1.0f;
            if (mood == SettlementMood::Prosperous)
                mult -= 0.1f;
            else if (mood == SettlementMood::Unrest)
                mult += 0.2f;
            else if (mood == SettlementMood::Revolt)
                mult += 0.4f;
            mult -= tradeDiscount;
            return std::max(1, int(std::floor(float(baseValue) * std::max(0.1f, mult))));
        }

        int trade_overlay_sell_price(int baseValue)
        {
            return std::max(1, int(std::floor(float(baseValue) * 0.5f)));
        }

        void draw_trade_item_tooltip(const ItemDef *def)
        {
            if (!def || !ImGui::IsItemHovered())
                return;
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(def->name);
            if (def->description && def->description[0] != '\0')
            {
                ImGui::TextWrapped("%s", def->description);
            }
            ImGui::Text("Weight: %.2f kg", double(def->weight));
            ImGui::EndTooltip();
        }

        const char *objective_kind_label(ObjectiveKind k)
        {
            switch (k)
            {
            case ObjectiveKind::VisitCell:
                return "Visit";
            case ObjectiveKind::FindLocation:
                return "Find";
            case ObjectiveKind::DeliverItems:
                return "Deliver";
            case ObjectiveKind::DestroyNpc:
                return "Destroy";
            case ObjectiveKind::WaitAt:
                return "Wait";
            case ObjectiveKind::InteractCell:
                return "Interact";
            }
            return "?";
        }

        const char *quest_category_label(QuestCategory c)
        {
            switch (c)
            {
            case QuestCategory::Main:
                return "main";
            case QuestCategory::Side:
                return "side";
            case QuestCategory::Procedural:
                return "procedural";
            }
            return "?";
        }

        const char *item_type_label(ItemType t)
        {
            switch (t)
            {
            case ItemType::Weapon:
                return "Weapon";
            case ItemType::Armor:
                return "Armor";
            case ItemType::Potion:
                return "Potion";
            case ItemType::Food:
                return "Food";
            case ItemType::Material:
                return "Material";
            case ItemType::Misc:
                return "Misc";
            }
            return "?";
        }

        bool is_equipment_item(const ItemDef &def)
        {
            return def.type == ItemType::Weapon || def.type == ItemType::Armor;
        }

        void draw_item_effect(const ItemEffect &e)
        {
            bool any = false;
            auto part = [&any](const char *label, int v)
            {
                if (v == 0)
                    return;
                if (any)
                    ImGui::SameLine();
                ImGui::Text("%+d %s", v, label);
                any = true;
            };
            part("HP", e.hp);
            part("MP", e.mp);
            part("SP", e.sp);
            part("STR", e.str);
            part("END", e.end);
            part("AGI", e.agi);
            if (!any)
                ImGui::TextDisabled("-");
        }

        int active_route_count_for(const GameState &gs, int settlementId)
        {
            int count = 0;
            for (const auto &route : gs.activeTradeRoutes)
            {
                if (route.valid &&
                    (route.originId == settlementId || route.destId == settlementId))
                {
                    ++count;
                }
            }
            return count;
        }

        struct SettlementPreviewCache
        {
            ImTextureID tex = 0;
            std::uint32_t worldSeed = 0;
            std::uint32_t previewSeed = 0;
            int settlementId = -1;
            int population = -1;
            int houses = 0;
            int walls = 0;
            bool ready = false;
        };

        SettlementPreviewCache &settlement_preview_cache()
        {
            static SettlementPreviewCache cache;
            return cache;
        }

        std::uint32_t settlement_preview_seed(std::uint32_t worldSeed, int settlementId)
        {
            const std::uint32_t id = settlementId >= 0
                                       ? std::uint32_t(settlementId)
                                       : 0u;
            return worldSeed + id * 123u;
        }

        ImU32 settlement_preview_tile_color(std::uint8_t t)
        {
            using namespace sub;
            switch (t)
            {
            case TILE_WATER:
                return IM_COL32(60, 110, 180, 255);
            case TILE_SHORE:
                return IM_COL32(210, 195, 150, 255);
            case TILE_GRASS:
                return IM_COL32(90, 140, 70, 255);
            case TILE_FIELD:
                return IM_COL32(180, 170, 90, 255);
            case TILE_TREE_DECOR:
                return IM_COL32(40, 85, 45, 255);
            case TILE_ROAD:
                return IM_COL32(165, 135, 95, 255);
            case TILE_HOUSE:
                return IM_COL32(150, 110, 80, 255);
            case TILE_WALL:
                return IM_COL32(120, 120, 125, 255);
            case TILE_SQUARE:
                return IM_COL32(190, 190, 180, 255);
            case TILE_ROCK:
                return IM_COL32(95, 95, 100, 255);
            case TILE_EMPTY:
                [[fallthrough]];
            default:
                return IM_COL32(70, 90, 60, 255);
            }
        }

        bool ensure_settlement_preview(SettlementPreviewCache &cache,
                                       const Settlement &s,
                                       std::uint32_t worldSeed)
        {
            const std::uint32_t previewSeed =
                settlement_preview_seed(worldSeed, s.id);
            if (cache.ready &&
                cache.tex != 0 &&
                cache.worldSeed == worldSeed &&
                cache.previewSeed == previewSeed &&
                cache.settlementId == s.id &&
                cache.population == s.population)
            {
                return true;
            }

            sub::CellContext ctx{};
            ctx.cx = 0;
            ctx.cy = 0;
            ctx.macroHeight = 0.55f;
            ctx.biome = Meadow;
            ctx.feature = FT_None;
            ctx.landmarkSettlementId = s.id;
            ctx.landmarkSize = s.population;
            ctx.landmarkKind = sub::CellLandmarkKind::City;
            ctx.seed = previewSeed;

            float nbHeights[9]{};
            Biome nbBiome[9]{};
            std::uint8_t nbFeature[9]{};
            for (int i = 0; i < 9; ++i)
            {
                nbHeights[i] = 0.55f;
                nbBiome[i] = Meadow;
                nbFeature[i] = std::uint8_t(FT_None);
            }

            sub::SubworldMapData map{};
            sub::dispatch_generate(ctx, nbHeights, nbBiome, nbFeature, map);
            const std::size_t expected =
                std::size_t(sub::kCellSize) * sub::kCellSize;
            if (map.tiles.size() < expected || map.heightmap.size() < expected)
            {
                return false;
            }

            constexpr int kPreviewSide = 256;
            constexpr int kCropX = (sub::kCellSize - kPreviewSide) / 2;
            constexpr int kCropY = (sub::kCellSize - kPreviewSide) / 2;
            std::vector<std::uint8_t> rgba(std::size_t(kPreviewSide) *
                                           kPreviewSide * 4u);
            int houses = 0;
            int walls = 0;
            for (const auto &st : map.structures)
            {
                if (st.kind == sub::Structure::House)
                    ++houses;
                else if (st.kind == sub::Structure::Wall)
                    ++walls;
            }

            for (int y = 0; y < kPreviewSide; ++y)
            {
                const int sy = kCropY + y;
                for (int x = 0; x < kPreviewSide; ++x)
                {
                    const int sx = kCropX + x;
                    const std::size_t src =
                        std::size_t(sy) * sub::kCellSize + sx;
                    const ImU32 base = settlement_preview_tile_color(map.tiles[src]);
                    const float h = map.heightmap[src];
                    const float waterLevel = map.waterLevel;
                    const float shade =
                        0.86f + 0.24f *
                        std::clamp((h - waterLevel) /
                                   (1.0f - waterLevel),
                                   0.0f, 1.0f);
                    const std::size_t dst =
                        (std::size_t(y) * kPreviewSide + x) * 4u;
                    rgba[dst + 0] = std::uint8_t(
                        std::clamp(float(base & 0xFFu) * shade, 0.0f, 255.0f));
                    rgba[dst + 1] = std::uint8_t(
                        std::clamp(float((base >> 8) & 0xFFu) * shade, 0.0f, 255.0f));
                    rgba[dst + 2] = std::uint8_t(
                        std::clamp(float((base >> 16) & 0xFFu) * shade, 0.0f, 255.0f));
                    rgba[dst + 3] = std::uint8_t((base >> 24) & 0xFFu);
                }
            }

            if (cache.tex)
                destroy_ui_texture(cache.tex);
            cache.tex = create_ui_texture(kPreviewSide, kPreviewSide,
                                          rgba.data(),
                                          /*linear=*/false);
            cache.worldSeed = worldSeed;
            cache.previewSeed = previewSeed;
            cache.settlementId = s.id;
            cache.population = s.population;
            cache.houses = houses;
            cache.walls = walls;
            cache.ready = cache.tex != 0;
            return cache.ready;
        }

        void draw_info_overview_row(const char *label, const char *value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value);
        }

        void draw_info_overview_row(const char *label, int value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
            ImGui::TableNextColumn();
            ImGui::Text("%d", value);
        }

        void draw_info_overview_row(const char *label, float value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", value);
        }

        void draw_objective_line(const Objective &o)
        {
            const char *done = o.completed ? "[x]" : "[ ]";
            switch (o.kind)
            {
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

        void draw_reward_line(const Reward &r)
        {
            switch (r.kind)
            {
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

        ImGuiTabItemFlags selected_tab(CharacterPanelTab current, CharacterPanelTab tab)
        {
            return current == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        }

        ImGuiTabItemFlags selected_tab(SettlementPanelTab current, SettlementPanelTab tab)
        {
            return current == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        }

        void draw_inventory_table(const Inventory &inv, bool showValue)
        {
            if (inv.stacks.empty())
            {
                ImGui::TextDisabled("(empty)");
                return;
            }
            const int cols = showValue ? 4 : 3;
            if (ImGui::BeginTable("inventory_table", cols,
                                  ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Item");
                ImGui::TableSetupColumn("Id");
                ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                if (showValue)
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableHeadersRow();
                for (const auto &st : inv.stacks)
                {
                    const ItemDef *def = item_def(st.id);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", def ? def->name : st.id.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", st.id.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("x%d", st.count);
                    if (showValue)
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("%d g", def ? def->value * st.count : 0);
                    }
                }
                ImGui::EndTable();
            }
        }

        struct AttributeUiRow
        {
            const char *label;
            const char *desc;
            AttributeId id;
        };

        struct SkillUiRow
        {
            const char *label;
            const char *desc;
            SkillId id;
        };

        constexpr AttributeUiRow kAttributeUiRows[] = {
            {"STR", "+1 physical damage per point", AttributeId::Str},
            {"VIT", "+10 max HP per point", AttributeId::Vit},
            {"END", "+10 max SP per point", AttributeId::End},
            {"WIL", "+10 max MP per point", AttributeId::Wil},
            {"INT", "+1 spell damage per point", AttributeId::Intl},
            {"WIS", "+1% EXP bonus per point", AttributeId::Wis},
            {"LCK", "Crit scaling and loot luck", AttributeId::Lck},
            {"CHA", "Trade discount and relation bonus", AttributeId::Cha},
            {"SPD", "Asymptotic movement speed", AttributeId::Spd},
        };

        constexpr SkillUiRow kSkillUiRows[] = {
            {"Bodybuilding", "+5% max HP per rank", SkillId::Bodybuilding},
            {"Meditation", "+5% max MP per rank", SkillId::Meditation},
            {"Travel", "+3% move speed per rank", SkillId::Travel},
            {"Fighter", "+5% physical damage per rank", SkillId::Fighter},
            {"Endurance", "+5% max SP per rank", SkillId::Endurance},
            {"Spellcraft", "+5% spell damage per rank", SkillId::Spellcraft},
            {"Weightlifting", "+10% carry capacity per rank", SkillId::Weightlifting},
        };

        void reset_player_combat_stats(PlayerState &p)
        {
            p.combatStats = calculate_combat_stats(p.sheet.attributes, p.sheet.skills);
        }
    } // namespace

    void draw_character_panel(GameState &gs, bool *open, CharacterPanelTab *tab, float scale)
    {
        if (!open || !*open)
            return;

        PlayerState &p = gs.player;
        const CharacterPanelTab current = tab ? *tab : CharacterPanelTab::Stats;
        DerivedBonuses derived = calculate_derived(p.sheet.attributes, p.sheet.skills);
        const float carryWeight = inventory_weight(p.inventory);
        const float carryCap = get_carry_capacity(p.sheet.attributes, p.sheet.skills);
        const int armyTotal = total_soldiers(p.army);
        const int armyUpkeep = calculate_squad_upkeep(p.army, p.sheet.attributes.cha);

        ImGui::SetNextWindowSize(ImVec2(760 * scale, 560 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Character", open))
        {
            ImGui::SetWindowFontScale(scale);
            ImGui::Text("%s  Level %d  Age %d days",
                        p.name.empty() ? "Wanderer" : p.name.c_str(),
                        p.sheet.levelData.level, p.ageDays);
            ImGui::SameLine();
            ImGui::TextDisabled("EXP %d / %d", p.sheet.levelData.exp, p.sheet.levelData.expToNext);
            ImGui::Separator();

            if (ImGui::BeginTabBar("##character_tabs"))
            {
                const bool statsOpen = ImGui::BeginTabItem("Stats", nullptr,
                                                           selected_tab(current, CharacterPanelTab::Stats));
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Stats;
                if (statsOpen)
                {
                    if (ImGui::BeginTable("stats_grid", 3,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
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
                        ImGui::Text("Attr pts %d", p.sheet.levelData.attributePoints);
                        ImGui::Text("Skill pts %d", p.sheet.levelData.skillPoints);
                        ImGui::Text("Perk pts %d", p.sheet.levelData.perkPoints);
                        if (p.sheet.levelData.exp >= p.sheet.levelData.expToNext)
                        {
                            if (ImGui::Button("Level Up"))
                            {
                                if (try_level_up(p.sheet.levelData))
                                    reset_player_combat_stats(p);
                            }
                        }
                        ImGui::TableNextColumn();
                        for (const AttributeUiRow &row : kAttributeUiRows)
                        {
                            const int *value = attribute_value(p.sheet.attributes, row.id);
                            if (!value)
                                continue;
                            ImGui::PushID(row.label);
                            ImGui::Text("%s %d", row.label, *value);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", row.desc);
                            ImGui::SameLine();
                            ImGui::BeginDisabled(p.sheet.levelData.attributePoints <= 0);
                            if (ImGui::SmallButton("+"))
                            {
                                if (spend_attribute_point(p.sheet.levelData, p.sheet.attributes, row.id))
                                {
                                    reset_player_combat_stats(p);
                                    derived = calculate_derived(p.sheet.attributes, p.sheet.skills);
                                }
                            }
                            ImGui::EndDisabled();
                            ImGui::PopID();
                        }
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
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Skill");
                        ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableHeadersRow();
                        for (const SkillUiRow &row : kSkillUiRows)
                        {
                            const int *value = skill_value(p.sheet.skills, row.id);
                            if (!value)
                                continue;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", row.label);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", row.desc);
                            ImGui::TableNextColumn();
                            ImGui::PushID(row.label);
                            ImGui::Text("%d", *value);
                            ImGui::SameLine();
                            ImGui::BeginDisabled(p.sheet.levelData.skillPoints <= 0);
                            if (ImGui::SmallButton("+"))
                            {
                                if (spend_skill_point(p.sheet.levelData, p.sheet.skills, row.id))
                                {
                                    reset_player_combat_stats(p);
                                    derived = calculate_derived(p.sheet.attributes, p.sheet.skills);
                                }
                            }
                            ImGui::EndDisabled();
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                    if (ImGui::BeginTable("perks_grid", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Perk");
                        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                        ImGui::TableHeadersRow();
                        for (const PerkInfo &perk : kPerkList)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", perk.name);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s\n%s\n%s",
                                                  perk.description,
                                                  perk.advantage,
                                                  perk.disadvantage);
                            ImGui::TableNextColumn();
                            const bool owned = has_perk(p.sheet.perks, perk.id);
                            if (owned)
                            {
                                ImGui::TextDisabled("active");
                            }
                            else
                            {
                                ImGui::PushID(perk.name);
                                ImGui::BeginDisabled(p.sheet.levelData.perkPoints <= 0);
                                if (ImGui::SmallButton("Choose"))
                                {
                                    add_perk(p.sheet.perks, perk.id);
                                    --p.sheet.levelData.perkPoints;
                                    if (perk.id == PerkID::Talented)
                                    {
                                        if (try_level_up(p.sheet.levelData))
                                            reset_player_combat_stats(p);
                                    }
                                }
                                ImGui::EndDisabled();
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                const bool inventoryOpen = ImGui::BeginTabItem("Inventory", nullptr,
                                                               selected_tab(current, CharacterPanelTab::Inventory));
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Inventory;
                if (inventoryOpen)
                {
                    ImGui::Text("Stacks: %zu  Items: %d  Weight: %.1f / %.0f kg",
                                p.inventory.stacks.size(), p.inventory.total(), carryWeight, carryCap);
                    draw_inventory_table(p.inventory, true);
                    ImGui::EndTabItem();
                }

                const bool armyOpen = ImGui::BeginTabItem("Army", nullptr,
                                                          selected_tab(current, CharacterPanelTab::Army));
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Army;
                if (armyOpen)
                {
                    ImGui::Text("Total: %d  Upkeep: %d g/day", armyTotal, armyUpkeep);
                    if (ImGui::BeginTable("army_grid", 3,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Unit");
                        ImGui::TableSetupColumn("Count");
                        ImGui::TableSetupColumn("Upkeep");
                        ImGui::TableHeadersRow();
                        for (int ti = 0; ti < npc_type_count(); ++ti)
                        {
                            const NPCType t = npc_type_at(ti);
                            const int count = count_soldiers_of_kind(
                                p.army, static_cast<std::uint8_t>(t));
                            if (count <= 0 && !npc_hireable(t))
                                continue;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", npc_def(t).label);
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", count);
                            ImGui::TableNextColumn();
                            ImGui::Text("%d g/day", npc_upkeep_base(t));
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                const bool equipmentOpen = ImGui::BeginTabItem("Equipment", nullptr,
                                                               selected_tab(current, CharacterPanelTab::Equipment));
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Equipment;
                if (equipmentOpen)
                {
                    ImGui::Text("Equipment data model missing");
                    ImGui::TextDisabled("PlayerState has inventory, attributes, skills, army, and spells.");
                    ImGui::TextDisabled("No equipped weapon/armor slots are serialized in save version %d.",
                                        gs.version);
                    ImGui::Separator();
                    ImGui::TextUnformatted("Equip-capable inventory items");
                    int equipStacks = 0;
                    if (ImGui::BeginTable("equipment_candidates", 5,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Item");
                        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                        ImGui::TableSetupColumn("Effect");
                        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ImGui::TableHeadersRow();
                        for (const auto &st : p.inventory.stacks)
                        {
                            const ItemDef *def = item_def(st.id);
                            if (!def || !is_equipment_item(*def))
                                continue;
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
                    if (equipStacks == 0)
                    {
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
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Spells;
                if (spellsOpen)
                {
                    ImGui::Text("MP %d / %d", p.combatStats.currentMp, p.combatStats.maxMp);
                    if (!p.spellBook.activeSpellId.empty())
                    {
                        const SpellDef *active = spell_registry().find(p.spellBook.activeSpellId);
                        ImGui::SameLine();
                        ImGui::TextDisabled("Active: %s",
                                            active ? active->name.c_str()
                                                   : p.spellBook.activeSpellId.c_str());
                    }
                    ImGui::Separator();

                    const auto &learned = p.spellBook.learned;
                    if (learned.empty())
                    {
                        ImGui::TextDisabled("(none)");
                    }
                    else if (ImGui::BeginTable("spell_grid", 9,
                                               ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Name");
                        ImGui::TableSetupColumn("Tier");
                        ImGui::TableSetupColumn("Shape");
                        ImGui::TableSetupColumn("Tags");
                        ImGui::TableSetupColumn("Mana");
                        ImGui::TableSetupColumn("Cooldown");
                        ImGui::TableSetupColumn("Power");
                        ImGui::TableSetupColumn("State");
                        ImGui::TableSetupColumn("Active");
                        ImGui::TableHeadersRow();
                        for (const auto &id : learned)
                        {
                            const SpellDef *def = spell_registry().find(id);
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            if (def)
                            {
                                const char *icon =
                                    (def->icon && def->icon[0] != '\0') ? def->icon : "?";
                                ImGui::Text("%s %s", icon, def->name.c_str());
                            }
                            else
                            {
                                ImGui::Text("%s", id.c_str());
                            }
                            if (def && ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                const char *icon =
                                    (def->icon && def->icon[0] != '\0') ? def->icon : "?";
                                ImGui::Text("%s %s", icon, def->name.c_str());
                                ImGui::TextDisabled("%s / Tier %d / %s",
                                                    spell_rarity_label(def->rarity),
                                                    def->tier,
                                                    spell_shape_label(def->shape));
                                if (def->castTime > 0.0f)
                                    ImGui::TextDisabled("Cast %.1fs / Cooldown %.1fs",
                                                        def->castTime,
                                                        def->cooldown);
                                else
                                    ImGui::TextDisabled("Cast instant / Cooldown %.1fs",
                                                        def->cooldown);
                                if (def->description && def->description[0] != '\0')
                                {
                                    ImGui::Separator();
                                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
                                    ImGui::TextUnformatted(def->description);
                                    ImGui::PopTextWrapPos();
                                }
                                if (def->macroType != MacroEffectType::None)
                                {
                                    ImGui::Separator();
                                    ImGui::TextDisabled("World: %s power %.1f duration %.0f",
                                                        spell_macro_label(def->macroType),
                                                        def->macroPower,
                                                        def->macroDuration);
                                }
                                if (def->prosCount > 0)
                                {
                                    ImGui::Separator();
                                    ImGui::TextUnformatted("Pros");
                                    const std::size_t n = std::min<std::size_t>(
                                        def->prosCount, kMaxSpellFlavorItems);
                                    for (std::size_t i = 0; i < n; ++i)
                                    {
                                        if (def->pros[i] && def->pros[i][0] != '\0')
                                            ImGui::BulletText("%s", def->pros[i]);
                                    }
                                }
                                if (def->consCount > 0)
                                {
                                    ImGui::Separator();
                                    ImGui::TextUnformatted("Cons");
                                    const std::size_t n = std::min<std::size_t>(
                                        def->consCount, kMaxSpellFlavorItems);
                                    for (std::size_t i = 0; i < n; ++i)
                                    {
                                        if (def->cons[i] && def->cons[i][0] != '\0')
                                            ImGui::BulletText("%s", def->cons[i]);
                                    }
                                }
                                ImGui::EndTooltip();
                            }
                            ImGui::TableNextColumn();
                            if (def)
                                ImGui::Text("%d", def->tier);
                            else
                                ImGui::TextDisabled("-");
                            ImGui::TableNextColumn();
                            if (def)
                                ImGui::Text("%s", spell_shape_label(def->shape));
                            else
                                ImGui::TextDisabled("-");
                            ImGui::TableNextColumn();
                            if (def)
                            {
                                if (def->tagCount > 1)
                                {
                                    ImGui::Text("%s/%s",
                                                spell_tag_label(def->tag),
                                                spell_tag_label(def->secondaryTag));
                                }
                                else
                                {
                                    ImGui::Text("%s", spell_tag_label(def->tag));
                                }
                            }
                            else
                            {
                                ImGui::TextDisabled("-");
                            }
                            ImGui::TableNextColumn();
                            if (def && def->sustained)
                                ImGui::Text("%.0f/s", def->manaDrain);
                            else if (def)
                                ImGui::Text("%d", def->manaCost);
                            else
                                ImGui::TextDisabled("-");
                            ImGui::TableNextColumn();
                            const auto cdIt = p.spellBook.cooldowns.find(id);
                            if (cdIt != p.spellBook.cooldowns.end() && cdIt->second > 0.0f)
                                ImGui::Text("%.1fs", cdIt->second);
                            else if (def && def->cooldown > 0.0f)
                                ImGui::Text("%.1fs", def->cooldown);
                            else
                                ImGui::TextDisabled("-");
                            ImGui::TableNextColumn();
                            if (def)
                            {
                                const int dmg = spell_damage(*def, p.sheet.attributes, p.sheet.skills);
                                const int heal = spell_heal(*def, p.sheet.attributes, p.sheet.skills);
                                const int rad = spell_radius(*def, p.sheet.attributes, p.sheet.skills);
                                if (dmg > 0 && rad > 0)
                                    ImGui::Text("%d / r%d", dmg, rad);
                                else if (dmg > 0)
                                    ImGui::Text("%d", dmg);
                                else if (heal > 0)
                                    ImGui::Text("+%d", heal);
                                else
                                    ImGui::TextDisabled("-");
                                if (def->statusEffect && def->statusEffect[0] != '\0')
                                {
                                    if (def->statusDuration > 0.0f)
                                        ImGui::TextDisabled("%s %.0fs",
                                                            def->statusEffect,
                                                            def->statusDuration);
                                    else
                                        ImGui::TextDisabled("%s", def->statusEffect);
                                }
                            }
                            else
                            {
                                ImGui::TextDisabled("-");
                            }
                            ImGui::TableNextColumn();
                            if (def && def->sustained && spellbook_has_sustained(p.spellBook, id))
                            {
                                ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Sustained");
                            }
                            else
                            {
                                const CastCheck check = spellbook_can_cast_ex(
                                    p.spellBook, p.combatStats, id, true);
                                if (check.ok)
                                {
                                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "Ready");
                                }
                                else if (check.cooldownRemaining > 0.0f)
                                {
                                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                                                       "Cooldown");
                                }
                                else
                                {
                                    ImGui::TextDisabled("%s", check.reason.c_str());
                                }
                            }
                            ImGui::TableNextColumn();
                            ImGui::PushID(id.c_str());
                            if (p.spellBook.activeSpellId == id)
                            {
                                ImGui::TextDisabled("Selected");
                            }
                            else if (ImGui::SmallButton("Set"))
                            {
                                spellbook_set_active(p.spellBook, id);
                            }
                            ImGui::PopID();
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

    void draw_settlement(GameState &gs,
                         int settlementId,
                         const std::vector<Quest> &availableQuests,
                         std::vector<Quest> &activeQuests,
                         QuestEngine &questEngine,
                         EventBus &bus,
                         SettlementPanelTab *tab,
                         bool *open,
                         float scale)
    {
        if (!open || !*open)
            return;
        Settlement *s = nullptr;
        for (auto &c : gs.settlements)
            if (c.id == settlementId)
            {
                s = &c;
                break;
            }
        const SettlementPanelTab current = tab ? *tab : SettlementPanelTab::Info;

        ImGui::SetNextWindowSize(ImVec2(760 * scale, 620 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settlement", open))
        {
            ImGui::SetWindowFontScale(scale);
            if (!s)
            {
                ImGui::Text("No settlement selected (id=%d).", settlementId);
                ImGui::End();
                return;
            }

            // ── Banner ──
            const char *lineage = "?";
            const char *kingdom = "Unaligned";
            if (s->kingdomIdx >= 0 && s->kingdomIdx < int(gs.politik.kingdoms.size()))
            {
                const auto &k = gs.politik.kingdoms[std::size_t(s->kingdomIdx)];
                kingdom = k.name.c_str();
                lineage = temperament_label(k.temperament);
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
            if (ImGui::BeginTabBar("##settab"))
            {
                // Info
                const bool infoOpen = ImGui::BeginTabItem("Info", nullptr,
                                                          selected_tab(current, SettlementPanelTab::Info));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Info;
                if (infoOpen)
                {
                    ImGui::TextWrapped("Welcome to %s.", s->name.c_str());
                    ImGui::TextDisabled("A city with population %d and a %s economy.",
                                        s->population,
                                        s->economy.empty() ? "unknown" : s->economy.c_str());
                    ImGui::Spacing();

                    if (ImGui::BeginTable("settlement_info", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                        ImGui::TableSetupColumn("Value");
                        ImGui::TableHeadersRow();
                        draw_info_overview_row("Population", s->population);
                        draw_info_overview_row("Mood", mood_label(s->mood));
                        draw_info_overview_row("Economy", s->economy.c_str());
                        draw_info_overview_row("Kingdom index", s->kingdomIdx);
                        draw_info_overview_row("Wealth", s->eco.wealth);
                        draw_info_overview_row("Happiness", s->eco.happiness);
                        draw_info_overview_row("Garrison units", total_soldiers(s->garrison));
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
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip("Native settlement entry is routed through Enter / In, not this overlay callback.");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Trade") && tab)
                    {
                        *tab = SettlementPanelTab::Trade;
                    }
                    ImGui::EndTabItem();
                }

                // Build
                const bool buildOpen = ImGui::BeginTabItem("Build", nullptr,
                                                           selected_tab(current, SettlementPanelTab::Build));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Build;
                if (buildOpen)
                {
                    ImGui::TextUnformatted("Build actions are disabled.");
                    ImGui::Spacing();
                    ImGui::TextWrapped("The TS SettlementOverlay.svelte currently exposes info, quests, rest, recruit, map, and history tabs only. It does not define build projects, costs, construction time, or effects.");
                    ImGui::TextWrapped("The native Settlement record persists population, mood, inventory, history, garrison, economy, kingdom index, and economy archetype. It has no buildings list or construction queue.");
                    ImGui::Spacing();
                    if (ImGui::BeginTable("build_missing_contracts", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Missing backend", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                        ImGui::TableSetupColumn("Required file / symbol");
                        ImGui::TableHeadersRow();

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted("Persistent building state");
                        ImGui::TableNextColumn();
                        ImGui::TextWrapped("src/macro/state.h::Settlement");

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted("Save/load fields");
                        ImGui::TableNextColumn();
                        ImGui::TextWrapped("src/macro/save.cpp and kSaveVersion");

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted("Project catalog");
                        ImGui::TableNextColumn();
                        ImGui::TextWrapped("TS build data or native data table with costs/effects");

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted("Daily effects");
                        ImGui::TableNextColumn();
                        ImGui::TextWrapped("src/macro/world_tick.cpp settlement tick");

                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                    ImGui::TextDisabled("No decorative build button is shown because there is no backend state that could persist the result.");
                    ImGui::EndTabItem();
                }

                // Trade
                const bool tradeOpen = ImGui::BeginTabItem("Trade", nullptr,
                                                           selected_tab(current, SettlementPanelTab::Trade));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Trade;
                if (tradeOpen)
                {
                    clear_settlement_trade_message_for(s->id);
                    const DerivedBonuses derived =
                        calculate_derived(gs.player.sheet.attributes, gs.player.sheet.skills);
                    ImGui::Text("Player gold: %d", gs.player.gold);
                    ImGui::SameLine();
                    ImGui::TextDisabled("Mood: %s", mood_label(s->mood));
                    if (g_settlement_trade_message[0] != '\0')
                    {
                        ImGui::Spacing();
                        ImGui::TextWrapped("%s", g_settlement_trade_message);
                    }
                    ImGui::Separator();

                    ImGui::Columns(2, "trade_cols", true);
                    ImGui::TextUnformatted("Settlement stock");
                    ImGui::BeginChild("##settlement_stock", ImVec2(0, 260), true);
                    if (s->inventory.stacks.empty())
                    {
                        ImGui::TextDisabled("(empty)");
                    }
                    else
                    {
                        bool changed = false;
                        for (std::size_t i = 0; i < s->inventory.stacks.size() && !changed; ++i)
                        {
                            const std::string &id = s->inventory.stacks[i].id;
                            const int count = s->inventory.stacks[i].count;
                            const ItemDef *def = item_def(id);
                            const int price = def
                                                  ? trade_overlay_buy_price(def->value,
                                                                            derived.tradeDiscount,
                                                                            s->mood)
                                                  : 0;
                            const bool canBuy = def && count > 0 && gs.player.gold >= price;
                            ImGui::PushID(int(i));
                            if (!canBuy)
                                ImGui::BeginDisabled();
                            if (ImGui::Button("Buy", ImVec2(52, 0)))
                            {
                                if (s->inventory.remove(id, 1))
                                {
                                    gs.player.gold -= price;
                                    gs.player.inventory.add(id, 1);
                                    set_settlement_trade_message("Bought", *def, price);
                                    push_settlement_trade_log(gs, "Bought", *def,
                                                              s->name.c_str(), price);
                                }
                                else
                                {
                                    std::snprintf(g_settlement_trade_message,
                                                  sizeof(g_settlement_trade_message),
                                                  "Item unavailable.");
                                }
                                changed = true;
                            }
                            if (!canBuy)
                                ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !def)
                            {
                                ImGui::SetTooltip("Unknown item id");
                            }
                            else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && count <= 0)
                            {
                                ImGui::SetTooltip("Out of stock.");
                            }
                            else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && gs.player.gold < price)
                            {
                                ImGui::SetTooltip("Not enough gold.");
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s x%d  %d g",
                                        def ? def->name : id.c_str(), count, price);
                            draw_trade_item_tooltip(def);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();

                    ImGui::NextColumn();
                    ImGui::TextUnformatted("Your inventory");
                    ImGui::BeginChild("##player_stock", ImVec2(0, 260), true);
                    if (gs.player.inventory.stacks.empty())
                    {
                        ImGui::TextDisabled("(empty)");
                    }
                    else
                    {
                        bool changed = false;
                        for (std::size_t i = 0; i < gs.player.inventory.stacks.size() && !changed; ++i)
                        {
                            const std::string &id = gs.player.inventory.stacks[i].id;
                            const int count = gs.player.inventory.stacks[i].count;
                            const ItemDef *def = item_def(id);
                            const int price = def
                                                  ? trade_overlay_sell_price(def->value)
                                                  : 0;
                            ImGui::PushID(int(i));
                            if (!def)
                                ImGui::BeginDisabled();
                            if (ImGui::Button("Sell", ImVec2(52, 0)))
                            {
                                if (gs.player.inventory.remove(id, 1))
                                {
                                    gs.player.gold += price;
                                    s->inventory.add(id, 1);
                                    set_settlement_trade_message("Sold", *def, price);
                                    push_settlement_trade_log(gs, "Sold", *def,
                                                              s->name.c_str(), price);
                                }
                                else
                                {
                                    std::snprintf(g_settlement_trade_message,
                                                  sizeof(g_settlement_trade_message),
                                                  "Cannot sell.");
                                }
                                changed = true;
                            }
                            if (!def)
                                ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !def)
                            {
                                ImGui::SetTooltip("Unknown item id");
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s x%d  %d g",
                                        def ? def->name : id.c_str(), count, price);
                            draw_trade_item_tooltip(def);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::Columns(1);

                    ImGui::Separator();
                    if (ImGui::BeginTable("economy_prices", 3,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Resource");
                        ImGui::TableSetupColumn("Stock");
                        ImGui::TableSetupColumn("Price");
                        ImGui::TableHeadersRow();
                        for (std::size_t i = 0; i < kNumResources; ++i)
                        {
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
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Garrison;
                if (garrisonOpen)
                {
                    int total = total_soldiers(s->garrison);
                    ImGui::Text("Total: %d units", total);
                    ImGui::Spacing();
                    if (ImGui::BeginTable("garrison", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        for (int ti = 0; ti < npc_type_count(); ++ti)
                        {
                            const NPCType t = npc_type_at(ti);
                            const int count = count_soldiers_of_kind(
                                s->garrison, static_cast<std::uint8_t>(t));
                            if (count <= 0 && !npc_hireable(t))
                                continue;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", npc_def(t).label);
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", count);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
                // Recruit
                const bool recruitOpen = ImGui::BeginTabItem("Recruit", nullptr,
                                                             selected_tab(current, SettlementPanelTab::Recruit));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Recruit;
                if (recruitOpen)
                {
                    ImGui::Text("Player gold: %d", gs.player.gold);
                    ImGui::Spacing();
                    for (int ti = 0; ti < npc_type_count(); ++ti)
                    {
                        const NPCType t = npc_type_at(ti);
                        if (!npc_hireable(t))
                            continue;
                        const SoldierRecord* offer = first_soldier_of_kind(s->garrison, t);
                        int cost = offer ? hire_price_for(*offer) : npc_hire_price_base(t);
                        int avail = count_soldiers_of_kind(
                            s->garrison, static_cast<std::uint8_t>(t));
                        int owned = count_soldiers_of_kind(
                            gs.player.army, static_cast<std::uint8_t>(t));
                        ImGui::PushID(static_cast<int>(t));
                        bool can = avail > 0 && gs.player.gold >= cost;
                        if (!can)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Hire"))
                        {
                            (void)hire_npc(gs.player.army, s->garrison, t, gs.player.gold);
                        }
                        if (!can)
                            ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::Text("%-10s  cost %d g   avail %d   you have %d",
                                    npc_def(t).label, cost, avail, owned);
                        ImGui::PopID();
                    }
                    ImGui::Spacing();
                    ImGui::TextDisabled("Daily upkeep: %d g",
                                        calculate_squad_upkeep(gs.player.army,
                                                               gs.player.sheet.attributes.cha));
                    ImGui::EndTabItem();
                }
                // Map
                const bool mapOpen = ImGui::BeginTabItem("Map", nullptr,
                                                         selected_tab(current, SettlementPanelTab::Map));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Map;
                if (mapOpen)
                {
                    SettlementPreviewCache &preview = settlement_preview_cache();
                    const std::uint32_t previewSeed =
                        settlement_preview_seed(gs.worldSeed, s->id);
                    ImGui::TextDisabled("City preview");
                    ImGui::SameLine();
                    if (ImGui::Button("Refresh"))
                    {
                        preview.ready = false;
                        preview.settlementId = -1;
                    }
                    ImGui::Spacing();

                    if (ensure_settlement_preview(preview, *s, gs.worldSeed))
                    {
                        const float avail = ImGui::GetContentRegionAvail().x;
                        const float side = std::min(360.0f, std::max(180.0f, avail));
                        ImGui::Image(preview.tex,
                                     ImVec2(side, side));
                        ImGui::TextDisabled("Seed: 0x%08X   Population: %d   Houses: %d   Walls: %d",
                                            previewSeed,
                                            s->population,
                                            preview.houses,
                                            preview.walls);
                    }
                    else
                    {
                        ImGui::Dummy(ImVec2(0.0f, 120.0f));
                        ImGui::TextDisabled("Preview unavailable.");
                        ImGui::TextDisabled("Seed: 0x%08X   Population: %d",
                                            previewSeed,
                                            s->population);
                    }
                    ImGui::EndTabItem();
                }
                // Inventory
                const bool inventoryOpen = ImGui::BeginTabItem("Inventory", nullptr,
                                                               selected_tab(current, SettlementPanelTab::Inventory));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Inventory;
                if (inventoryOpen)
                {
                    if (s->inventory.stacks.empty())
                    {
                        ImGui::TextDisabled("(empty)");
                    }
                    else
                    {
                        if (ImGui::BeginTable("inv", 2,
                                              ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                        {
                            for (const auto &st : s->inventory.stacks)
                            {
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
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::History;
                if (historyOpen)
                {
                    if (s->history.population.empty())
                    {
                        ImGui::TextDisabled("(no history yet)");
                    }
                    else
                    {
                        std::vector<float> v;
                        v.reserve(s->history.population.size());
                        for (int p : s->history.population)
                            v.push_back(float(p));
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
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Rest;
                if (restOpen)
                {
                    int cost = rest_cost(s->mood);
                    ImGui::Text("Inn rest: %d g (mood-priced)", cost);
                    ImGui::Text("Restores HP / MP / SP to full.");
                    ImGui::Spacing();
                    bool can = gs.player.gold >= cost;
                    if (!can)
                        ImGui::BeginDisabled();
                    if (ImGui::Button("Rest at Inn"))
                    {
                        gs.player.gold -= cost;
                        gs.player.combatStats.currentHp = gs.player.combatStats.maxHp;
                        gs.player.combatStats.currentMp = gs.player.combatStats.maxMp;
                        gs.player.combatStats.currentSp = gs.player.combatStats.maxSp;
                    }
                    if (!can)
                        ImGui::EndDisabled();
                    if (!can)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "not enough gold");
                    }
                    ImGui::EndTabItem();
                }
                // Quests
                const bool questsOpen = ImGui::BeginTabItem("Quests", nullptr,
                                                            selected_tab(current, SettlementPanelTab::Quests));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Quests;
                if (questsOpen)
                {
                    ImGui::Text("Available today: %zu", availableQuests.size());
                    ImGui::Separator();
                    for (const auto &q : availableQuests)
                    {
                        const bool known = questEngine.is_known(activeQuests, gs.player, q.id);
                        ImGui::PushID(q.id.c_str());
                        if (ImGui::TreeNode("quest", "%s%s", q.title.c_str(),
                                            known ? " (known)" : ""))
                        {
                            ImGui::TextWrapped("%s", q.description.c_str());
                            ImGui::Text("Difficulty: %d", q.difficulty);
                            ImGui::TextUnformatted("Objectives");
                            for (const auto &o : q.objectives)
                                draw_objective_line(o);
                            ImGui::TextUnformatted("Rewards");
                            for (const auto &r : q.rewards)
                                draw_reward_line(r);
                            if (known)
                                ImGui::BeginDisabled();
                            if (ImGui::Button("Accept"))
                            {
                                questEngine.accept(activeQuests, q, gs.player, bus);
                            }
                            if (known)
                                ImGui::EndDisabled();
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

    void draw_quest_log(GameState &gs,
                        std::vector<Quest> &quests,
                        QuestEngine &questEngine,
                        EventBus &bus,
                        int *selectedQuestIndex,
                        bool *open,
                        float scale)
    {
        if (!open || !*open)
            return;

        int localSelected = 0;
        int &selected = selectedQuestIndex ? *selectedQuestIndex : localSelected;
        if (selected < 0)
            selected = 0;
        if (selected >= int(quests.size()))
        {
            selected = quests.empty() ? 0 : int(quests.size()) - 1;
        }

        int abandonIndex = -1;

        ImGui::SetNextWindowSize(ImVec2(640 * scale, 430 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Quest Journal", open))
        {
            ImGui::SetWindowFontScale(scale);
            if (ImGui::Button("Close [Q]"))
            {
                *open = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Active: %zu  Done: %zu",
                                quests.size(), gs.player.completedQuestIds.size());
            ImGui::Separator();

            if (quests.empty())
            {
                ImGui::Dummy(ImVec2(0.0f, 70.0f));
                ImGui::TextDisabled("No active quests. Visit a settlement to find work.");
            }
            else
            {
                constexpr float kListW = 190.0f;
                ImGui::BeginChild("##quest_list", ImVec2(kListW, 0.0f), true);
                for (int i = 0; i < int(quests.size()); ++i)
                {
                    const Quest &q = quests[std::size_t(i)];
                    ImGui::PushID(q.id.c_str());
                    if (ImGui::Selectable(q.title.c_str(), selected == i))
                    {
                        selected = i;
                    }
                    ImGui::TextDisabled("%s", quest_category_label(q.category));
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::EndChild();

                ImGui::SameLine();
                ImGui::BeginChild("##quest_detail", ImVec2(0.0f, 0.0f), true);
                const Quest &q = quests[std::size_t(selected)];
                ImGui::Text("%s", q.title.c_str());
                ImGui::TextDisabled("%s  Difficulty: %d",
                                    quest_category_label(q.category), q.difficulty);
                if (q.expireDay >= 0)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Expires: day %d", q.expireDay);
                }

                ImGui::Spacing();
                ImGui::TextWrapped("%s", q.description.c_str());
                ImGui::Separator();

                ImGui::TextUnformatted("Objectives");
                if (q.objectives.empty())
                {
                    ImGui::TextDisabled("(none)");
                }
                else
                {
                    for (const auto &o : q.objectives)
                    {
                        ImGui::TextDisabled("%s", objective_kind_label(o.kind));
                        ImGui::SameLine();
                        draw_objective_line(o);
                    }
                }

                ImGui::Spacing();
                ImGui::TextUnformatted("Rewards");
                if (q.rewards.empty())
                {
                    ImGui::TextDisabled("(none)");
                }
                else
                {
                    for (const auto &r : q.rewards)
                        draw_reward_line(r);
                }

                ImGui::Separator();
                if (ImGui::Button("Abandon Quest"))
                {
                    abandonIndex = selected;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Uses QuestEngine::abandon; emits QuestFail(abandoned) and removes the active quest.");
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();

        if (abandonIndex >= 0 && abandonIndex < int(quests.size()))
        {
            const std::string id = quests[std::size_t(abandonIndex)].id;
            questEngine.abandon(quests, id, bus);
            if (selected >= int(quests.size()))
            {
                selected = quests.empty() ? 0 : int(quests.size()) - 1;
            }
        }
    }

    void draw_codex(GameState &gs, bool *open, float scale)
    {
        if (!open || !*open)
            return;
        static int selectedCategory = 0;
        static int selectedArticle = 0;
        ensure_codex_selection(gs.player, selectedCategory, selectedArticle);

        ImGui::SetNextWindowSize(ImVec2(900 * scale, 620 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Codex", open))
        {
            ImGui::SetWindowFontScale(scale);
            ImGui::Text("Codex");
            ImGui::SameLine();
            ImGui::TextDisabled("Unlocked entries: %zu", gs.player.codexUnlocked.size());
            ImGui::Separator();

            ImGui::BeginChild("##codex_sidebar", ImVec2(250.0f, 0.0f), true);
            for (int ci = 0; ci < int(std::size(kCodexCategories)); ++ci)
            {
                const CodexCategory &cat = kCodexCategories[std::size_t(ci)];
                const int firstArticle = first_unlocked_article_index(gs.player, cat);
                if (firstArticle < 0)
                    continue;

                const bool catSelected = selectedCategory == ci;
                if (ImGui::Selectable(cat.title, catSelected))
                {
                    selectedCategory = ci;
                    selectedArticle = firstArticle;
                }

                if (selectedCategory == ci)
                {
                    ImGui::Indent(12.0f);
                    for (int ai = 0; ai < int(cat.count); ++ai)
                    {
                        const CodexArticle &entry = cat.articles[std::size_t(ai)];
                        if (!codex_unlocked(gs.player, entry.id))
                            continue;
                        if (ImGui::Selectable(entry.title, selectedArticle == ai))
                            selectedArticle = ai;
                    }
                    ImGui::Unindent(12.0f);
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("##codex_article", ImVec2(0.0f, 0.0f), true);
            if (selectedCategory >= 0 &&
                selectedCategory < int(std::size(kCodexCategories)))
            {
                const CodexCategory &cat =
                    kCodexCategories[std::size_t(selectedCategory)];
                if (selectedArticle >= 0 && selectedArticle < int(cat.count))
                {
                    const CodexArticle &entry =
                        cat.articles[std::size_t(selectedArticle)];
                    ImGui::TextColored(ImVec4(0.95f, 0.36f, 0.24f, 1.0f),
                                       "%s", entry.title);
                    ImGui::TextDisabled("%s / %s", cat.title, entry.id);
                    ImGui::Separator();
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(entry.content);
                    ImGui::PopTextWrapPos();
                }
            }
            else
            {
                ImGui::Dummy(ImVec2(0.0f, 180.0f));
                ImGui::TextDisabled("No codex entries unlocked.");
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void draw_show_dialog(GameState &gs,
                          const GameEvent &dialog,
                          EventBus &bus,
                          DialogOverlayState &state,
                          bool *open)
    {
        if (!open || !*open)
        {
            state = DialogOverlayState{};
            return;
        }

        ImGui::OpenPopup("Event");
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(modal_width(vp, 540.0f, 260.0f), 0.0f),
                                 ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Event", nullptr,
                                   ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_AlwaysAutoResize))
        {
            const char *title = dialog.s1.empty() ? "Event" : dialog.s1.c_str();
            const char *body = dialog.s2.empty() ? "(no description)" : dialog.s2.c_str();
            const std::vector<DialogChoicePayload> *choices =
                dialog.dialogChoices ? dialog.dialogChoices.get() : nullptr;
            const int choiceCount = choices && !choices->empty()
                ? static_cast<int>(choices->size())
                : (dialog.ix > 0 ? dialog.ix : 1);

            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", title);
            ImGui::Separator();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(body);
            ImGui::PopTextWrapPos();
            ImGui::Separator();

            if (state.hasResult)
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextWrapped("%s", state.resultMessage.data());
                ImGui::PopTextWrapPos();
                ImGui::Separator();
                if (ImGui::Button("Continue", ImVec2(-FLT_MIN, 0.0f)))
                {
                    state = DialogOverlayState{};
                    *open = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
                return;
            }

            if (choices && !choices->empty())
            {
                for (int i = 0; i < choiceCount; ++i)
                {
                    const DialogChoicePayload &choice = (*choices)[std::size_t(i)];
                    ImGui::PushID(i);
                    if (ImGui::Button(choice.label.empty() ? "Continue" : choice.label.c_str(),
                                      ImVec2(-FLT_MIN, 0.0f)))
                    {
                        const int goldCost = dialog_choice_gold_cost(choice);
                        if (goldCost > gs.player.gold)
                        {
                            set_dialog_result(state, "Not enough gold!");
                        }
                        else
                        {
                            log_dialog_choice(gs, dialog, choice);
                            request_dialog_node_activation(state, choice.nodeId);
                            if (!choice.effects.empty())
                            {
                                bus.emit_all(choice.effects);
                                set_dialog_result(state,
                                                  choice.label.empty()
                                                      ? "Continue"
                                                      : choice.label.c_str());
                            }
                            else
                            {
                                *open = false;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndPopup();
                return;
            }

            if (dialog.ix <= 0)
            {
                if (ImGui::Button("Continue", ImVec2(-FLT_MIN, 0.0f)))
                {
                    state = DialogOverlayState{};
                    *open = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
                return;
            }

            ImGui::TextDisabled("Choices in payload: %d", choiceCount);
            for (int i = 0; i < choiceCount; ++i)
            {
                ImGui::PushID(i);
                ImGui::BeginDisabled();
                ImGui::Button("Choice label/effect unavailable", ImVec2(-FLT_MIN, 0.0f));
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip(
                        "Missing backend: ShowDialog GameEvent has no DialogChoice label, nodeId, or effects fields; only ix=choice count was provided.");
                }
                ImGui::PopID();
            }

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0.0f)))
            {
                state = DialogOverlayState{};
                *open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void draw_story_overlay(StoryOverlayState &state, EventBus &bus)
    {
        if (!story_overlay_active(state))
            return;

        const content::StoryDef &story = *state.story;
        if (story.phaseCount == 0 || state.phaseIndex >= story.phaseCount)
        {
            state.open = false;
            state.story = nullptr;
            return;
        }

        ImGui::OpenPopup("Story");
        ImGuiViewport *vp = ImGui::GetMainViewport();
        const float maxW = modal_width(vp, 820.0f, 280.0f);
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(maxW, 0.0f), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal("Story", nullptr,
                                    ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        const content::StoryPhaseDef &phase = story.phases[state.phaseIndex];
        const char *heading = phase.title ? phase.title : story.id;

        if (phase.kind == content::StoryPhaseKind::Slides)
        {
            if (phase.slideCount == 0 || !phase.slides)
            {
                advance_story_phase(state, bus);
                ImGui::EndPopup();
                return;
            }
            if (state.slideIndex >= phase.slideCount)
                state.slideIndex = phase.slideCount - 1;

            const content::StorySlide &slide = phase.slides[state.slideIndex];
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", heading);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu / %zu", state.slideIndex + 1, phase.slideCount);
            ImGui::Separator();
            if (const StoryTexture *image = story_texture_for(slide.image))
            {
                const float imageW = ImGui::GetContentRegionAvail().x;
                draw_story_image(*image, imageW, 360.0f, true);
                ImGui::Spacing();
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", slide.narration ? slide.narration : "");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            const bool lastSlide = state.slideIndex + 1 >= phase.slideCount;
            if (ImGui::Button(lastSlide ? "Begin" : "Continue", ImVec2(-FLT_MIN, 0.0f)) ||
                ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                ImGui::IsKeyPressed(ImGuiKey_Space))
            {
                if (!lastSlide)
                    ++state.slideIndex;
                else
                    advance_story_phase(state, bus);
            }
            ImGui::EndPopup();
            return;
        }

        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", heading);
        ImGui::Separator();
        if (phase.description && phase.description[0])
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", phase.description);
            ImGui::PopTextWrapPos();
            ImGui::Separator();
        }

        if (phase.kind == content::StoryPhaseKind::Choice)
        {
            if (!phase.choices || phase.choiceCount == 0)
            {
                ImGui::TextDisabled("No choices supplied by story content.");
                if (ImGui::Button("Continue", ImVec2(-FLT_MIN, 0.0f)))
                    advance_story_phase(state, bus);
                ImGui::EndPopup();
                return;
            }

            bool portraits = false;
            for (std::size_t i = 0; i < phase.choiceCount; ++i)
            {
                if (phase.choices[i].image)
                {
                    portraits = true;
                    break;
                }
            }
            const float availW = ImGui::GetContentRegionAvail().x;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const bool portraitsInline = portraits && phase.choiceCount > 1 &&
                availW >= (140.0f * float(phase.choiceCount)) +
                    spacing * float(phase.choiceCount - 1);
            const float buttonW = portraitsInline
                ? (availW - ImGui::GetStyle().ItemSpacing.x * float(phase.choiceCount - 1)) /
                    float(phase.choiceCount)
                : availW;

            for (std::size_t i = 0; i < phase.choiceCount; ++i)
            {
                const content::StoryChoice &choice = phase.choices[i];
                ImGui::PushID(int(i));
                if (portraits)
                    ImGui::BeginGroup();
                if (portraits)
                {
                    if (const StoryTexture *image = story_texture_for(choice.image))
                    {
                        draw_story_image(*image, buttonW, 220.0f, true);
                    }
                    else
                    {
                        ImGui::Dummy(ImVec2(buttonW, 220.0f));
                    }
                }
                if (ImGui::Button(choice.label ? choice.label : "Choice",
                                  ImVec2(buttonW, portraits ? 40.0f : 0.0f)))
                {
                    if (state.phaseIndex < kStoryOverlayMaxPhases)
                    {
                        state.selectedChoice[state.phaseIndex] = int(i);
                        state.hasValue[state.phaseIndex] = true;
                        copy_cstr(state.values[state.phaseIndex].data(),
                                  state.values[state.phaseIndex].size(),
                                  choice.value);
                    }
                    advance_story_phase(state, bus);
                }
                if (choice.description && choice.description[0])
                {
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + buttonW);
                    ImGui::TextDisabled("%s", choice.description);
                    ImGui::PopTextWrapPos();
                }
                if (portraits)
                {
                    ImGui::EndGroup();
                    if (portraitsInline && i + 1 < phase.choiceCount)
                        ImGui::SameLine();
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
            return;
        }

        if (phase.kind == content::StoryPhaseKind::Input)
        {
            const std::size_t inputCap = story_input_capacity(phase);
            if (state.phaseIndex < kStoryOverlayMaxPhases && !state.inputPrepared)
            {
                copy_cstr(state.values[state.phaseIndex].data(),
                          inputCap,
                          phase.defaultValue);
                state.inputPrepared = true;
            }

            char *input = state.phaseIndex < kStoryOverlayMaxPhases
                ? state.values[state.phaseIndex].data()
                : nullptr;
            if (input)
            {
                ImGui::InputTextWithHint("##story_input",
                                         phase.placeholder ? phase.placeholder : "",
                                         input,
                                         inputCap);
                if (ImGui::Button("Continue", ImVec2(-FLT_MIN, 0.0f)) ||
                    ImGui::IsKeyPressed(ImGuiKey_Enter))
                {
                    trim_or_default(input, inputCap, phase.defaultValue);
                    state.hasValue[state.phaseIndex] = true;
                    advance_story_phase(state, bus);
                }
            }
            else
            {
                ImGui::TextDisabled("Input buffer unavailable.");
            }
        }
        ImGui::EndPopup();
    }

    // Pre-rendered minimap cache. Built lazily on first open and rebuilt
    // whenever the world seed changes (so loading a save refreshes the bitmap).
    namespace
    {
        struct MiniMapCache
        {
            ImTextureID tex = 0;
            int w = 0;
            int h = 0;
            std::uint32_t seed = 0;
            int srcW = 0;
            int srcH = 0;
        };

        void build_minimap(MiniMapCache &mm, const TerrainData &terrain, std::uint32_t seed)
        {
            constexpr int kMaxSide = 256;
            int side = std::min(kMaxSide, std::max(terrain.width, terrain.height));
            int W = side, H = side;
            std::vector<std::uint8_t> rgba(std::size_t(W) * H * 4);
            const int srcW = terrain.width, srcH = terrain.height;
            for (int y = 0; y < H; ++y)
            {
                // Y-flip so the minimap matches the macroworld viewport convention
                // (world +Y is screen UP). Pixel y=0 (top) shows world y=H-1.
                int sy = std::min(srcH - 1, (H - 1 - y) * srcH / H);
                for (int x = 0; x < W; ++x)
                {
                    int sx = std::min(srcW - 1, x * srcW / W);
                    std::size_t s = std::size_t(sy) * srcW + sx;
                    std::uint8_t hb = terrain.rgba[s * 4 + 0];
                    std::uint8_t mb = terrain.rgba[s * 4 + 1];
                    std::uint8_t tb = terrain.rgba[s * 4 + 2];
                    std::uint8_t mask = terrain.rgba[s * 4 + 3];
                    float r, g, b;
                    if (!mask || hb < std::uint8_t(0.40f * 255.0f))
                    {
                        // Water — depth shading.
                        float depth = 1.0f - (float(hb) / (0.40f * 255.0f));
                        if (depth < 0)
                            depth = 0;
                        if (depth > 1)
                            depth = 1;
                        r = 0.10f + 0.08f * (1.0f - depth);
                        g = 0.20f + 0.20f * (1.0f - depth);
                        b = 0.40f + 0.20f * (1.0f - depth);
                    }
                    else
                    {
                        Biome bi = biome_from_climate(float(tb) / 255.0f, float(mb) / 255.0f);
                        const auto &def = kBiomes[std::size_t(bi)];
                        r = def.r;
                        g = def.g;
                        b = def.b;
                        // Subtle altitude shading.
                        float h01 = float(hb) / 255.0f;
                        float shade = 0.85f + (h01 - 0.5f) * 0.35f;
                        r *= shade;
                        g *= shade;
                        b *= shade;
                    }
                    std::size_t d = std::size_t(y) * W + x;
                    rgba[d * 4 + 0] = std::uint8_t(std::clamp(r, 0.0f, 1.0f) * 255.0f);
                    rgba[d * 4 + 1] = std::uint8_t(std::clamp(g, 0.0f, 1.0f) * 255.0f);
                    rgba[d * 4 + 2] = std::uint8_t(std::clamp(b, 0.0f, 1.0f) * 255.0f);
                    rgba[d * 4 + 3] = 255;
                }
            }
            if (mm.tex)
                destroy_ui_texture(mm.tex);
            mm.tex = create_ui_texture(W, H, rgba.data(), /*linear=*/true);
            mm.w = W;
            mm.h = H;
            mm.seed = seed;
            mm.srcW = srcW;
            mm.srcH = srcH;
        }
    } // namespace

    void draw_map_overlay(GameState &gs, const TerrainData &terrain, bool *open, float scale)
    {
        if (!open || !*open)
            return;
        static MiniMapCache mm;
        if (mm.tex == 0 || mm.seed != gs.worldSeed || mm.srcW != terrain.width || mm.srcH != terrain.height)
        {
            build_minimap(mm, terrain, gs.worldSeed);
        }

        ImGui::SetNextWindowSize(ImVec2(620 * scale, 720 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("World Map", open))
        {
            ImGui::SetWindowFontScale(scale);
            ImGui::Text("Size: %d x %d   Seed: 0x%X",
                        terrain.width, terrain.height, gs.worldSeed);
            ImGui::Text("Player: %.1f, %.1f", gs.player.x, gs.player.y);
            ImGui::Text("Cities: %zu  Villages: %zu",
                        gs.politik.cities.size(), gs.villages.size());
            ImGui::Separator();

            // Available width drives the on-screen size; keep aspect.
            float avail = ImGui::GetContentRegionAvail().x;
            float disp = std::min(avail, 560.0f);
            ImVec2 size(disp, disp * float(mm.h) / float(mm.w));
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Image(mm.tex, size);

            // Overlay markers — convert world coords to image-space pixels.
            // Minimap is Y-flipped (world +Y → screen UP), so marker Y mirrors.
            ImDrawList *dl = ImGui::GetWindowDrawList();
            const float sx = size.x / float(terrain.width);
            const float sy = size.y / float(terrain.height);
            auto wy = [&](int worldY)
            {
                return origin.y + (float(terrain.height - 1 - worldY)) * sy;
            };
            // Cities (yellow rings).
            for (const auto &c : gs.politik.cities)
            {
                ImVec2 p(origin.x + float(c.x) * sx, wy(c.y));
                dl->AddCircleFilled(p, 3.0f, IM_COL32(255, 220, 90, 230), 8);
                dl->AddCircle(p, 3.5f, IM_COL32(40, 30, 0, 255), 8, 1.0f);
            }
            // Villages (small brown dots).
            for (const auto &v : gs.villages)
            {
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
    void draw_encounter_modal(GameState &gs, EventBus &bus)
    {
        if (gs.subState.kind != GameSubStateKind::Event)
            return;
        const auto &table = content::encounters();
        int idx = gs.subState.pendingEncounterIdx;
        if (idx < 0 || idx >= int(table.size()))
        {
            gs.subState.kind = GameSubStateKind::Exploring;
            gs.subState.pendingEncounterIdx = -1;
            return;
        }
        const auto &enc = table[size_t(idx)];

        ImGui::OpenPopup("Encounter");
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 0));
        if (ImGui::BeginPopupModal("Encounter", nullptr,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", enc.title.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s", enc.body.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            for (size_t i = 0; i < enc.choices.size(); ++i)
            {
                const auto &ch = enc.choices[i];
                ImGui::PushID(int(i));
                if (ImGui::Button(ch.label.c_str(), ImVec2(-1, 0)))
                {
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

    namespace
    {

        // Cache for the downsampled subworld tile bitmap. Rebuilds when the
        // seamless centre cell changes (player crossed a cell boundary) or after
        // kRebuildIntervalSec to pick up player-driven mutations (e.g. felled
        // trees). HUD and M-map keep separate sizes so the always-on HUD stays
        // cheap while the full map can afford richer sampling.
        struct SubMiniMapCache
        {
            ImTextureID tex = 0;
            int side = 384; // 384² downsample of 3072² (8x)
            int centerCx = INT32_MIN;
            int centerCy = INT32_MIN;
            double lastBuildSec = -1e9;
        };

        struct SubMapSample
        {
            std::uint8_t tile = sub::TILE_EMPTY;
            float height = 0.5f;
            int sx = 0;
            int sy = 0;
        };

        ImU32 subworld_tile_color(std::uint8_t t)
        {
            using namespace sub;
            switch (t)
            {
            case TILE_WATER:
                return IM_COL32(60, 110, 180, 255);
            case TILE_SHORE:
                return IM_COL32(210, 195, 150, 255);
            case TILE_GRASS:
                return IM_COL32(90, 140, 70, 255);
            case TILE_FIELD:
                return IM_COL32(180, 170, 90, 255);
            case TILE_TREE_DECOR:
                return IM_COL32(40, 85, 45, 255);
            case TILE_ROAD:
                return IM_COL32(165, 135, 95, 255);
            case TILE_HOUSE:
                return IM_COL32(150, 110, 80, 255);
            case TILE_WALL:
                return IM_COL32(120, 120, 125, 255);
            case TILE_SQUARE:
                return IM_COL32(190, 190, 180, 255);
            case TILE_ROCK:
                return IM_COL32(95, 95, 100, 255);
            case TILE_EMPTY:
                [[fallthrough]];
            default:
                return IM_COL32(70, 90, 60, 255);
            }
        }

        std::uint8_t choose_detail_tile(const int *counts,
                                        int countTotal,
                                        std::uint8_t centerTile)
        {
            using namespace sub;
            if (counts[TILE_WALL] > 0)
                return TILE_WALL;
            if (counts[TILE_HOUSE] > 0)
                return TILE_HOUSE;
            if (counts[TILE_SQUARE] > 0)
                return TILE_SQUARE;
            if (counts[TILE_ROAD] > 0)
                return TILE_ROAD;
            if (counts[TILE_WATER] * 2 >= countTotal)
                return TILE_WATER;
            if (counts[TILE_SHORE] > 0 ||
                (counts[TILE_WATER] > 0 && counts[TILE_WATER] < countTotal))
                return TILE_SHORE;
            if (counts[TILE_TREE_DECOR] * 4 >= countTotal)
                return TILE_TREE_DECOR;
            if (counts[TILE_FIELD] * 4 >= countTotal)
                return TILE_FIELD;
            if (counts[TILE_ROCK] * 4 >= countTotal)
                return TILE_ROCK;

            int bestTile = centerTile;
            int bestCount = 0;
            for (int t = 0; t <= int(TILE_ROCK); ++t)
            {
                if (counts[t] > bestCount)
                {
                    bestCount = counts[t];
                    bestTile = t;
                }
            }
            return std::uint8_t(bestTile);
        }

        SubMapSample sample_sub_map_pixel(const std::vector<std::uint8_t> &tiles,
                                          const std::vector<float> &heights,
                                          int side,
                                          int x,
                                          int y,
                                          bool detail)
        {
            const int src = sub::kFullSize;
            const int sx0 = std::clamp(x * src / side, 0, src - 1);
            const int sx1 = std::clamp(((x + 1) * src + side - 1) / side, sx0 + 1, src);
            const int sy0 = std::clamp(src - ((y + 1) * src + side - 1) / side, 0, src - 1);
            const int sy1 = std::clamp(src - (y * src / side), sy0 + 1, src);
            const int csx = std::clamp((sx0 + sx1 - 1) / 2, 0, src - 1);
            const int csy = std::clamp((sy0 + sy1 - 1) / 2, 0, src - 1);
            const std::size_t center = std::size_t(csy) * src + csx;
            SubMapSample out{};
            out.tile = tiles[center];
            out.height = heights[center];
            out.sx = csx;
            out.sy = csy;

            if (!detail)
                return out;

            int counts[int(sub::TILE_ROCK) + 1]{};
            float sumH = 0.0f;
            int countTotal = 0;
            for (int sy = sy0; sy < sy1; ++sy)
            {
                const std::size_t row = std::size_t(sy) * src;
                for (int sx = sx0; sx < sx1; ++sx)
                {
                    const std::uint8_t t = tiles[row + sx];
                    if (t <= sub::TILE_ROCK)
                        ++counts[t];
                    sumH += heights[row + sx];
                    ++countTotal;
                }
            }
            if (countTotal > 0)
            {
                out.tile = choose_detail_tile(counts, countTotal, out.tile);
                out.height = sumH / float(countTotal);
            }
            return out;
        }

        float sub_map_height_at(const std::vector<float> &heights, int x, int y)
        {
            const int src = sub::kFullSize;
            x = std::clamp(x, 0, src - 1);
            y = std::clamp(y, 0, src - 1);
            return heights[std::size_t(y) * src + x];
        }

        float sub_map_shade(const std::vector<float> &heights,
                            const SubMapSample &s,
                            bool detail)
        {
            constexpr float kWaterLevel = sm::sub::WATER_LEVEL;
            const float h = s.height;
            const float hL = sub_map_height_at(heights, s.sx - 4, s.sy);
            const float hR = sub_map_height_at(heights, s.sx + 4, s.sy);
            const float hD = sub_map_height_at(heights, s.sx, s.sy - 4);
            const float hU = sub_map_height_at(heights, s.sx, s.sy + 4);
            const float dx = hR - hL;
            const float dy = hU - hD;
            const float slope = std::clamp(std::sqrt(dx * dx + dy * dy) * 6.0f,
                                           0.0f, 1.0f);
            const float light = std::clamp(0.98f + (dy - dx) * 1.35f - slope * 0.22f,
                                           0.66f, 1.26f);

            if (s.tile == sub::TILE_WATER)
            {
                const float depth = std::clamp((kWaterLevel - h) / kWaterLevel,
                                               0.0f, 1.0f);
                return std::clamp(0.96f - depth * 0.28f + (light - 1.0f) * 0.10f,
                                  0.62f, 1.06f);
            }

            const float elev = std::clamp((h - kWaterLevel) /
                                          (1.0f - kWaterLevel),
                                          0.0f, 1.0f);
            float shade = (0.82f + 0.28f * elev) * light;
            if (detail && s.tile != sub::TILE_SHORE)
            {
                const float band = std::fabs(std::fmod(h * 12.0f, 1.0f) - 0.5f);
                if (band > 0.475f)
                    shade *= 0.91f;
            }
            return std::clamp(shade, 0.62f, 1.28f);
        }

        void write_map_pixel(std::vector<std::uint8_t> &rgba,
                             int side,
                             int x,
                             int y,
                             ImU32 base,
                             float shade)
        {
            const std::size_t d = (std::size_t(y) * side + x) * 4u;
            rgba[d + 0] = std::uint8_t(std::clamp(float(base & 0xFFu) * shade, 0.0f, 255.0f));
            rgba[d + 1] = std::uint8_t(std::clamp(float((base >> 8) & 0xFFu) * shade, 0.0f, 255.0f));
            rgba[d + 2] = std::uint8_t(std::clamp(float((base >> 16) & 0xFFu) * shade, 0.0f, 255.0f));
            rgba[d + 3] = std::uint8_t((base >> 24) & 0xFFu);
        }

        void blend_map_pixel(std::vector<std::uint8_t> &rgba,
                             int side,
                             int x,
                             int y,
                             ImU32 color,
                             float alpha)
        {
            if (x < 0 || y < 0 || x >= side || y >= side)
                return;
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            const std::size_t d = (std::size_t(y) * side + x) * 4u;
            const float inv = 1.0f - alpha;
            rgba[d + 0] = std::uint8_t(float(rgba[d + 0]) * inv + float(color & 0xFFu) * alpha);
            rgba[d + 1] = std::uint8_t(float(rgba[d + 1]) * inv + float((color >> 8) & 0xFFu) * alpha);
            rgba[d + 2] = std::uint8_t(float(rgba[d + 2]) * inv + float((color >> 16) & 0xFFu) * alpha);
            rgba[d + 3] = 255u;
        }

        int sub_map_px(float worldX, int side)
        {
            return std::clamp(int(worldX * float(side) / float(sub::kFullSize)),
                              0, side - 1);
        }

        int sub_map_py(float worldY, int side)
        {
            return std::clamp(side - 1 -
                                  int(worldY * float(side) / float(sub::kFullSize)),
                              0, side - 1);
        }

        void draw_map_disc(std::vector<std::uint8_t> &rgba,
                           int side,
                           int cx,
                           int cy,
                           int radius,
                           ImU32 color,
                           float alpha)
        {
            radius = std::max(0, radius);
            const int r2 = radius * radius;
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    if (dx * dx + dy * dy <= r2)
                        blend_map_pixel(rgba, side, cx + dx, cy + dy, color, alpha);
                }
            }
        }

        void draw_map_rect(std::vector<std::uint8_t> &rgba,
                           int side,
                           int cx,
                           int cy,
                           int halfW,
                           int halfH,
                           ImU32 color,
                           float alpha)
        {
            halfW = std::max(0, halfW);
            halfH = std::max(0, halfH);
            for (int py = cy - halfH; py <= cy + halfH; ++py)
            {
                for (int px = cx - halfW; px <= cx + halfW; ++px)
                    blend_map_pixel(rgba, side, px, py, color, alpha);
            }
        }

        void overlay_subworld_roads(std::vector<std::uint8_t> &rgba,
                                    int side,
                                    const sub::SeamlessSubworldManager &mgr)
        {
            if (!mgr.has_composite_roads())
                return;
            std::vector<std::int32_t> roadMask;
            mgr.append_composite_road_mask_indices(roadMask);
            const int radius = side >= 700 ? 1 : 0;
            for (std::int32_t idx : roadMask)
            {
                if (idx < 0)
                    continue;
                const int sx = int(idx % sub::kFullSize);
                const int sy = int(idx / sub::kFullSize);
                draw_map_disc(rgba, side,
                              sub_map_px(float(sx), side),
                              sub_map_py(float(sy), side),
                              radius,
                              IM_COL32(188, 144, 88, 255),
                              0.92f);
            }
        }

        void overlay_subworld_structures(std::vector<std::uint8_t> &rgba,
                                         int side,
                                         const sub::SeamlessSubworldManager &mgr)
        {
            for (const auto &st : mgr.structures())
            {
                const int px = sub_map_px(st.x, side);
                const int py = sub_map_py(st.y, side);
                const int r = std::max(1, int(st.radius * float(side) /
                                              float(sub::kFullSize)));
                switch (st.kind)
                {
                case sub::Structure::House:
                    draw_map_rect(rgba, side, px, py,
                                  std::max(1, r), std::max(1, r / 2),
                                  IM_COL32(150, 82, 58, 255), 0.95f);
                    break;
                case sub::Structure::Wall:
                    draw_map_disc(rgba, side, px, py,
                                  std::max(1, r), IM_COL32(92, 92, 98, 255), 0.95f);
                    break;
                case sub::Structure::Bridge:
                    draw_map_rect(rgba, side, px, py,
                                  std::max(1, r), 1,
                                  IM_COL32(170, 132, 82, 255), 0.92f);
                    break;
                case sub::Structure::Rock:
                    draw_map_disc(rgba, side, px, py,
                                  std::max(1, r), IM_COL32(112, 112, 116, 255), 0.70f);
                    break;
                case sub::Structure::Tree:
                    break;
                }
            }
        }

        void build_sub_map_texture(SubMiniMapCache &mm,
                                   const sub::SeamlessSubworldManager &mgr,
                                   bool detail)
        {
            const auto &tiles = mgr.tiles();
            const auto &heights = mgr.heightmap();
            if (tiles.size() != std::size_t(sub::kFullSize) * sub::kFullSize)
                return;
            if (heights.size() != tiles.size())
                return;
            const int side = mm.side;
            std::vector<std::uint8_t> rgba(std::size_t(side) * side * 4);
            for (int y = 0; y < side; ++y)
            {
                for (int x = 0; x < side; ++x)
                {
                    const SubMapSample s =
                        sample_sub_map_pixel(tiles, heights, side, x, y, detail);
                    const ImU32 base = subworld_tile_color(s.tile);
                    const float shade = sub_map_shade(heights, s, detail);
                    write_map_pixel(rgba, side, x, y, base, shade);
                }
            }
            if (detail)
            {
                overlay_subworld_roads(rgba, side, mgr);
                overlay_subworld_structures(rgba, side, mgr);
            }
            if (mm.tex)
                destroy_ui_texture(mm.tex);
            mm.tex = create_ui_texture(side, side, rgba.data(),
                                       /*linear=*/true);
            mm.centerCx = mgr.center_cx();
            mm.centerCy = mgr.center_cy();
            mm.lastBuildSec = ImGui::GetTime();
        }

        SubMiniMapCache &sub_minimap_cache()
        {
            static SubMiniMapCache mm;
            return mm;
        }

        SubMiniMapCache &sub_full_map_cache()
        {
            static SubMiniMapCache mm{0, 768, INT32_MIN, INT32_MIN, -1e9};
            return mm;
        }

        void ensure_sub_minimap(const sub::SeamlessSubworldManager &mgr)
        {
            auto &mm = sub_minimap_cache();
            const double now = ImGui::GetTime();
            const bool centerChanged =
                mm.centerCx != mgr.center_cx() || mm.centerCy != mgr.center_cy();
            const bool stale = (now - mm.lastBuildSec) > 2.0;
            if (mm.tex == 0 || centerChanged || stale)
            {
                build_sub_map_texture(mm, mgr, false);
            }
        }

        void ensure_sub_full_map(const sub::SeamlessSubworldManager &mgr)
        {
            auto &mm = sub_full_map_cache();
            const double now = ImGui::GetTime();
            const bool centerChanged =
                mm.centerCx != mgr.center_cx() || mm.centerCy != mgr.center_cy();
            const bool stale = (now - mm.lastBuildSec) > 2.0;
            if (mm.tex == 0 || centerChanged || stale)
            {
                build_sub_map_texture(mm, mgr, true);
            }
        }

        // Convert (worldX, worldY) in [0..kFullSize] to image-space UVs in [0..1].
        // Y-flipped to match build_sub_map_texture.
        ImVec2 sub_world_to_uv(float worldX, float worldY)
        {
            const float fs = float(sub::kFullSize);
            float u = std::clamp(worldX / fs, 0.0f, 1.0f);
            float v = std::clamp(1.0f - (worldY / fs), 0.0f, 1.0f);
            return ImVec2(u, v);
        }

    } // namespace

    void draw_subworld_minimap_hud(const sub::SeamlessSubworldManager &mgr,
                                   float playerX, float playerY,
                                   float cameraYaw,
                                   int viewportW, int viewportH,
                                   const sub::MinimapBlip *blips,
                                   std::size_t blipCount,
                                   float scale, float topInset)
    {
        ensure_sub_minimap(mgr);
        auto &mm = sub_minimap_cache();
        if (!mm.tex)
            return;

        // Size the compass RELATIVE to the window, not in fixed pixels, so it
        // holds the same visual weight at any resolution (a fixed radius was a
        // different fraction of every screen — the bug being fixed here). The
        // radius is a fraction of the SHORTER viewport side, so the disc scales
        // with the screen yet can never overflow the vertical space; clamped so
        // it stays useful on a tiny window and doesn't dominate a huge monitor.
        // ~0.22·min(W,H) → a disc spanning ~0.44 of the shorter side. This one
        // number is the size knob: raise it for a bigger compass, lower for
        // smaller — everything else (dots, arrow, placement) follows. The user's
        // universal-UI `scale` multiplies the whole envelope (fraction, min AND
        // max) so the single choke point below cascades to every derived size.
        constexpr float kRadiusFrac = 0.22f;   // share of the shorter side
        constexpr float kRadiusMin  = 72.0f;
        constexpr float kRadiusMax  = 200.0f;
        constexpr float kMargin = 16.0f;       // inset from the right edge
        constexpr float kTopGap = 10.0f;       // breathing room under the top bar
        const float shortSide = float(std::min(viewportW, viewportH));
        const float radius = std::clamp(shortSide * kRadiusFrac * scale,
                                        kRadiusMin * scale, kRadiusMax * scale);
        // Anchor top-right, but drop the whole disc below the player status bar
        // so it never overlaps it — at any radius. `topInset` is that bar's live
        // (scaled) height, or 0 when the bar is hidden, kept as the single anchor
        // source so the disc follows whatever the top strip is actually doing.
        const ImVec2 center(float(viewportW) - kMargin - radius,
                            topInset + kTopGap + radius);
        const ImVec2 pmin(center.x - radius, center.y - radius);
        const ImVec2 pmax(center.x + radius, center.y + radius);

        ImDrawList *dl = ImGui::GetForegroundDrawList();

        // Sample window around the player covering ~one macro cell.
        // Mirror horizontally (swap U extents) so the minimap handedness
        // matches the first-person view: the camera's right-hand side shows
        // on the right of the compass. Without this the arrow's left/right
        // read inverted relative to the terrain.
        constexpr float kHalfTiles = float(sub::kCellSize) / 2.0f;
        const ImVec2 puv = sub_world_to_uv(playerX, playerY);
        const float duv = kHalfTiles / float(sub::kFullSize);
        const ImVec2 uv0(std::clamp(puv.x + duv, 0.0f, 1.0f),
                         std::clamp(puv.y - duv, 0.0f, 1.0f));
        const ImVec2 uv1(std::clamp(puv.x - duv, 0.0f, 1.0f),
                         std::clamp(puv.y + duv, 0.0f, 1.0f));

        // True circular clip: AddImageRounded with rounding == radius produces
        // a circle when the bounding box is a square. North-up — no rotation;
        // the player triangle below carries the heading instead. This is the
        // standard "compass HUD" look (Skyrim, Baldur's Gate, etc).
        dl->AddImageRounded(mm.tex,
                            pmin, pmax, uv0, uv1,
                            IM_COL32_WHITE, radius);

        // Frame ring.
        dl->AddCircle(center, radius + 0.5f, IM_COL32(20, 20, 20, 230), 64, 3.0f);
        dl->AddCircle(center, radius - 1.5f, IM_COL32(220, 200, 140, 200), 64, 1.0f);

        // NPC / monster blips. Drawn AFTER the terrain image + ring (so they sit
        // on the map) but BEFORE the player marker (so the player stays on top).
        // Each dot's colour comes straight from the shared player_stance() axis,
        // so a red dot is exactly an entity the combat code treats as hostile —
        // one universal, extensible mapping, no parallel table to drift.
        //
        // A blip's world offset from the player maps with the SAME transform as
        // the terrain window above: X mirrored (matching the swapped-U sample)
        // and north-up (+Y world → screen up), scaled so ±kHalfTiles fills the
        // ±radius disc. Result: a dot lands exactly on the tile it represents.
        const float kBlipClip = radius - 3.0f; // keep dots inside the ring
        // Signed stance [-1,+1] → smooth red→yellow→green. The relationship is
        // numeric (per-faction reputation), so we interpolate rather than bucket:
        // the negative half fades hostile-red into neutral-yellow, the positive
        // half neutral-yellow into ally-green. Anchors live here alone.
        auto stance_color = [](float s) -> ImU32
        {
            s = std::clamp(s, -1.0f, 1.0f);
            auto lerp8 = [](int a, int c, float t)
            { return int(float(a) + (float(c) - float(a)) * t + 0.5f); };
            // Anchor colours: hostile red, neutral yellow, ally green.
            int r, g, b;
            if (s < 0.0f)
            {
                const float t = s + 1.0f; // -1 → red, 0 → yellow
                r = lerp8(222, 232, t);
                g = lerp8(58, 200, t);
                b = lerp8(48, 70, t);
            }
            else
            {
                const float t = s; // 0 → yellow, +1 → green
                r = lerp8(232, 72, t);
                g = lerp8(200, 200, t);
                b = lerp8(70, 92, t);
            }
            return IM_COL32(r, g, b, 255);
        };
        for (std::size_t i = 0; i < blipCount; ++i)
        {
            const sub::MinimapBlip &b = blips[i];
            const float ox = -((b.x - playerX) / kHalfTiles) * radius;
            const float oy = -((b.y - playerY) / kHalfTiles) * radius;
            if (ox * ox + oy * oy > kBlipClip * kBlipClip)
                continue; // outside the visible disc (or beyond the local window)
            const ImVec2 dp(center.x + ox, center.y + oy);
            // Pixel-style blip: one flat colour, no outline — a small crisp
            // square matching the game's pixel-art look (and smaller than the
            // old soft dot). The colour still comes from the shared stance axis,
            // so hostiles read red and allies green; only the mark is stylised.
            // Half-extent scales gently with the disc but stays small — a crisp
            // ~1px dot on a normal window, never a blob when a city fills the map.
            const float kBlip = std::max(1.0f, radius * 0.001f);
            dl->AddRectFilled(ImVec2(dp.x - kBlip, dp.y - kBlip),
                              ImVec2(dp.x + kBlip, dp.y + kBlip),
                              stance_color(b.stance));
        }

        // Player marker — triangle pointing where the camera faces. The map
        // is north-up and mirrored in X (above) so its handedness matches the
        // first-person view. Camera heading (cos yaw, sin yaw) in world (X,Z)
        // maps to a mirrored screen offset of (-cos yaw, -sin yaw).
        const float hx = -std::cos(cameraYaw), hy = -std::sin(cameraYaw);
        const float ms = std::clamp(radius * 0.05f, 6.0f, 8.0f); // heading arrow: gently scaled, bounded small
        auto rot = [&](float along, float side)
        {
            // along = toward heading, side = screen-perpendicular (right).
            return ImVec2(center.x + hx * along - hy * side,
                          center.y + hy * along + hx * side);
        };
        const ImVec2 p0 = rot(ms, 0.0f);
        const ImVec2 p1 = rot(-ms * 0.7f, -ms * 0.6f);
        const ImVec2 p2 = rot(-ms * 0.7f, ms * 0.6f);
        dl->AddTriangleFilled(p0, p1, p2, IM_COL32(255, 240, 100, 255));
        dl->AddTriangle(p0, p1, p2, IM_COL32(20, 20, 20, 230), 1.5f);

        (void)viewportH;
    }

    void draw_subworld_map_overlay(const sub::SeamlessSubworldManager &mgr,
                                   float playerX, float playerY,
                                   float cameraYaw,
                                   bool *open,
                                   float scale)
    {
        if (!open || !*open)
            return;
        ensure_sub_full_map(mgr);
        auto &mm = sub_full_map_cache();
        if (!mm.tex)
            return;

        ImGui::SetNextWindowSize(ImVec2(680 * scale, 760 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Subworld Map", open))
        {
            ImGui::SetWindowFontScale(scale);
            ImGui::Text("Centre cell: (%d, %d)", mgr.center_cx(), mgr.center_cy());
            ImGui::Text("Player (subworld): %.1f, %.1f", playerX, playerY);
            ImGui::Separator();

            // Full-page view: square, axis-aligned, NO rotation, NO circular
            // mask. Shows the entire 3×3 seamless subworld grid. Player marker
            // shows world heading via a small wedge.
            const float avail = ImGui::GetContentRegionAvail().x;
            const float disp = std::min(avail, 620.0f);
            const ImVec2 size(disp, disp);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 pmin = origin;
            const ImVec2 pmax(origin.x + size.x, origin.y + size.y);

            ImDrawList *dl = ImGui::GetWindowDrawList();
            // Mirror horizontally (U goes 1→0) so the map handedness matches
            // the first-person view, same as the HUD compass.
            dl->AddImage(mm.tex,
                         pmin, pmax, ImVec2(1, 0), ImVec2(0, 1));
            dl->AddRect(pmin, pmax, IM_COL32(30, 30, 30, 230), 0.0f, 0, 2.0f);

            // 3×3 cell grid lines (axis-aligned).
            const ImU32 cellLine = IM_COL32(30, 30, 30, 140);
            for (int i = 1; i < 3; ++i)
            {
                const float t = float(i) / 3.0f;
                const float x = pmin.x + t * size.x;
                const float y = pmin.y + t * size.y;
                dl->AddLine(ImVec2(x, pmin.y), ImVec2(x, pmax.y), cellLine, 1.0f);
                dl->AddLine(ImVec2(pmin.x, y), ImVec2(pmax.x, y), cellLine, 1.0f);
            }

            // Player marker: dot + heading wedge. Map is mirrored in X (above)
            // so the marker X and heading X mirror too.
            const ImVec2 puv = sub_world_to_uv(playerX, playerY);
            const ImVec2 pp(pmin.x + (1.0f - puv.x) * size.x, pmin.y + puv.y * size.y);
            const float hx = -std::cos(cameraYaw), hy = -std::sin(cameraYaw);
            const float ms = 9.0f;
            auto rot = [&](float along, float side)
            {
                return ImVec2(pp.x + hx * along - hy * side,
                              pp.y + hy * along + hx * side);
            };
            const ImVec2 hp0 = rot(ms, 0.0f);
            const ImVec2 hp1 = rot(-ms * 0.7f, -ms * 0.6f);
            const ImVec2 hp2 = rot(-ms * 0.7f, ms * 0.6f);
            dl->AddTriangleFilled(hp0, hp1, hp2, IM_COL32(255, 240, 100, 255));
            dl->AddTriangle(hp0, hp1, hp2, IM_COL32(20, 20, 20, 230), 1.5f);
            dl->AddCircleFilled(pp, 2.5f, IM_COL32(20, 20, 20, 230), 12);
        }
        ImGui::End();
    }

} // namespace sm::ui
