#include "macro/econ_day.h"
#include "sub/ability.h"   // THE ability door — a sip pays the one gate   // sp_price — THE one price of an action
#include "tables/seasons.h"    // kDaysPerSeason — the upkeep bill's horizon
#include "ui/overlays.h"
#include "macro/cell_facts.h"   // the Map preview's REAL cell context
#include "macro/player_entity.h"
#include "macro/squad.h"   // record_deed — THE deed door (S20.1)
#include "ui/trade_widgets.h"
#include "ui/screens.h"   // kTopStatusBarHeight — keep the minimap below the top bar
#include "macro/map_generator.h"
#include "tables/biomes.h"
#include "tables/codex.h"
#include "macro/landmark_registry.h"
#include "macro/landmark_iter.h"
#include "ui/squad_draw.h"
#include "tables/npc.h"
#include "tables/commodity.h"   // the raw rows the scrap variant byte names
#include "macro/economy.h"
#include "tables/faction.h"
#include "macro/anketa.h"
#include "content/spells/spell_book.h"
#include "content/plot/intro.h"
#include "events/event_bus.h"
#include "ui/ui_gpu.h"
#include "ui/ui_image.h"
#include "imgui.h"
#include "sub/base_generator.h"
#include "sub/material.h"
#include "sub/record.h"    // objects_find — мост арены (уедет фреймом, (vi));
                           // сам вопрос «руки игрока» — слот-дверями doors.h
#include "sub/gens/dispatch.h"
#include "sub/seamless_manager.h"
#include "sub/engine.h"
#include "sub/map_data.h"
#include "macro/store.h"
#include <stb_image.h>
#include <algorithm>
#include <bit>
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
            std::string line = "Event: ";
            line += dialog.s1.empty() ? "Event" : dialog.s1;
            line += " - ";
            line += choice.label.empty() ? "Continue" : choice.label;
            session_feed_push(gs.sessionFeed, line.c_str());
        }

        // Closing the last slide IS the story's completion: the StoryResult it
        // emits carries no values any more (the creation screen owns the
        // asking) — it marks the story beat, nothing else.
        void complete_story(StoryOverlayState &state, EventBus &bus)
        {
            if (!state.story)
                return;

            GameEvent result{EventTag::StoryResult};
            result.storyResult = std::make_shared<StoryResultPayload>();
            result.storyResult->sourceNodeId =
                state.story->sourceNodeId ? state.story->sourceNodeId : "";
            result.storyResult->storyId = state.story->id ? state.story->id : "";

            bus.emit(result);
            state.open = false;
            state.story = nullptr;
        }

        // The codex catalogue is a REGISTRY now (tables/codex.h): articles by
        // ordinal, categories a column, unlock state one bit per ordinal.
        // The tables that lived here moved there — the UI draws content, it
        // does not own it. `article` selection is the GLOBAL article index.
        bool codex_unlocked(const PlayerState &player, CodexArticleId id)
        {
            return (player.codexUnlockedBits & codex_bit(id)) != 0u;
        }

        int first_unlocked_article_in(const PlayerState &player,
                                      CodexCategoryId cat)
        {
            for (std::size_t i = 0; i < kCodexArticleCount; ++i)
            {
                if (kCodexArticles[i].category == cat &&
                    codex_unlocked(player, kCodexArticles[i].id))
                    return int(i);
            }
            return -1;
        }

        void ensure_codex_selection(const PlayerState &player,
                                    int &category,
                                    int &article)
        {
            if (category >= 0 && category < int(CodexCategoryId::Count) &&
                article >= 0 && article < int(kCodexArticleCount) &&
                int(kCodexArticles[std::size_t(article)].category) == category &&
                codex_unlocked(player, kCodexArticles[std::size_t(article)].id))
            {
                return;
            }

            for (int ci = 0; ci < int(CodexCategoryId::Count); ++ci)
            {
                const int ai =
                    first_unlocked_article_in(player, CodexCategoryId(ci));
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
        state.slideIndex = 0;
    }

    bool story_overlay_active(const StoryOverlayState &state)
    {
        return state.open && state.story != nullptr;
    }

    // (ПАНЕЛЬ ДИПЛОМАТИИ ВЫРЕЗАНА 2026-09-21, вердикт владельца: «вырезаем
    // панель дипломатии», «политики пока не будет… политику мы сделаем, но
    // после того как будет ядро играбельное». Она показывала список фракций и
    // репутацию игрока — то есть окно в систему, которой в демо нет. Матрица
    // отношений жива как БАЗА реестра интересов, а окно вернётся вместе с
    // политикой, одним законом над реестром.)

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

        // Старейший слот рода в области существ (обход 1023 → first =
        // старый порядок контейнера) — цена найма показывается по нему.
        const ItemRef* first_soldier_of_kind(const Inventory& inv,
                                             NPCType kind)
        {
            const std::uint16_t row = world_row_of_creature(kind);
            for (int i = kMaxInventorySlots - 1;
                 i >= inv.creature_first(); --i)
            {
                if (inv.slots[std::size_t(i)].def == row)
                    return &inv.slots[std::size_t(i)];
            }
            return nullptr;
        }

        // БЛАГОПОЛУЧИЕ — ЕДИНСТВЕННАЯ МЕРА ЖИЗНИ МЕСТА (владелец
        // 2026-09-19): реестр полос настроения вырезан, глаз читает то же
        // число, что и закон роста — долю оплаченных нужд, в процентах.
        int wellbeing_pct(const sm::MacroStore &st, std::uint16_t slot)
        {
            return int(st.wellbeing[slot].seasonWellbeing) * 100 / 255;
        }

        // THE ONE wrapper state of the subject counter (trade_widgets.h,
        // Инк 5): staged package + Amount + receipt, keyed by the subject's
        // ORDINAL. Счётчиков было ДВА — один «для места», другой «для
        // сквада», — и держала их не нужда, а ветвление панели надвое: одна
        // страница, одна прилавочная сессия (M-290).
        BarterWrapState g_subjectTrade;
        // The squad banner's Talk stub (the S27 hook): the line shown inline
        // under the banner, keyed by the subject so another squad's panel
        // never inherits a stranger's words.
        // Хэндл {slot,gen} вместо entt-энтити (шаг 1г): статик переживает
        // кадры и миры, и поколение честно мертвит его при переиспользовании
        // слота — биты энтити это только маскировали.
        MacroHandle  g_squadTalkFor{};
        const char*  g_squadTalkLine = nullptr;


        const char* npc_trait_label(std::uint8_t raw)
        {
            switch (static_cast<NPCTrait>(raw)) {
                case NPCTrait::Greedy:     return "Greedy";
                case NPCTrait::Honorable:  return "Honorable";
                case NPCTrait::Cowardly:   return "Cowardly";
                case NPCTrait::Brave:      return "Brave";
                case NPCTrait::Aggressive: return "Aggressive";
                case NPCTrait::Generous:   return "Generous";
                case NPCTrait::Suspicious: return "Suspicious";
                case NPCTrait::Curious:    return "Curious";
                default:                   return "?";
            }
        }

        // (Пара обёрток «цена по нраву купца» вырезана 2026-09-19 вместе с
        // реестром нрава: у обеих панелей — одна и та же цена, потому что
        // закон один. Обёртки ниже обслуживают и лавку места, и одинокого
        // торговца.)

        // A completed deal with a squad is a FACT of the world (S20.1) —
        // subject = ТОТ, НА КОМ ФЛАЖОК, object = the trader's squad by its
        // save-stable ordinal; filed through THE one deed door.
        //
        // ВСЕЛЕНИЕ НЕ БОЛЕЕ ЧЕМ ПЕРЕНОС ФЛАЖКА «ЗА КОГО ИГРАЕМ» (вердикт
        // владельца 2026-10-12, дословно: «да уничтожить все спец механики
        // вселения было решено что вселение не более чем перенос флажка за
        // кого играем (это шире то есть если будет потом в игре несколько
        // сквадов у игрока он будет перекоючаться между ними тоже переносом
        // флажка)»). Значит сделку подшивает ДЕЛАТЕЛЬ: торгуешь в теле лорда
        // — сделка ЛОРДА. Здесь стоял ординал ДОМА, то есть у факта было два
        // хозяина: клетку он брал у флажка (строкой ниже), а субъекта — у
        // оригинала, и вселённая сделка числилась за оставленной оболочкой.
        void record_subject_deal_fact(GameState &gs, MacroStore &st,
                                      std::uint32_t subjectOrdinal,
                                      int x, int y, int gave, int took)
        {
            WorldFact f{};
            f.day = gs.worldTime.day();
            f.kind = std::uint16_t(FactKind::Traded);
            f.subjectKind = std::uint8_t(FactSubject::Squad);
            // Подшивает ДЕЛАТЕЛЬ (владелец: «игрок это рпг анкета сквада…»):
            // торгуешь в теле лорда — сделка ЛОРДА. Здесь стоял ординал ДОМА,
            // и у факта было два хозяина: клетку он брал у флажка, а субъекта
            // у оригинала, и вселённая сделка числилась за оболочкой.
            f.subject = player_flag_ordinal(gs, st);
            f.objectKind = std::uint8_t(FactSubject::Squad);
            f.object = subjectOrdinal;
            f.x = std::int16_t(x);
            f.y = std::int16_t(y);
            f.amount = gave + took;
            record_deed(st, gs, f, MacroHandle{});
        }

        // (Колонка настроения в цене вырезана 2026-09-19 вместе с самим
        // настроением: у цены остались кривая дефицита и разница торговых
        // сил, третьего множителя не существует.)

        // ДВЕ СТОРОНЫ, ДВЕ АНКЕТЫ (CANON S25; починено 2026-09-21). Здесь
        // стояло `trade_price(база, харизма, ранг Торговли)` — то есть на
        // месте ЧУЖОЙ торговой силы стоял собственный скилл покупателя, а
        // контрагента сделка не спрашивала вовсе. Следствия оба видны
        // игроку: наценка выходила `(Cha − ранг Trade)/100`, значит прокачка
        // Торговли ДЕЛАЛА ЦЕНУ ХУЖЕ, а торг с нищим бродягой и со столицей
        // стоил одинаково. Обе стороны теперь называют своё производное
        // одной дверью trade_power_of.
        int trade_overlay_buy_price(int baseValue, int myTradePct,
                                    int theirTradePct)
        {
            return sm::trade_price(baseValue, myTradePct, theirTradePct,
                                   /*buying*/ true);
        }

        int trade_overlay_sell_price(int baseValue, int myTradePct,
                                     int theirTradePct)
        {
            return sm::trade_price(baseValue, myTradePct, theirTradePct,
                                   /*buying*/ false);
        }

        const char *objective_kind_label(ObjectiveKind k)
        {
            switch (k)
            {
            case ObjectiveKind::VisitCell:
                return "Visit";
            case ObjectiveKind::DeliverItems:
                return "Deliver";
            case ObjectiveKind::DestroyNpc:
                return "Destroy";
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

        // (No `item_type_label` and no `is_equipment_item`. Whether a row can
        // be WORN is the row's own `slotMask` — an item that names no body
        // part cannot be worn, whatever its type says — and the equipment tab
        // asks that instead of guessing from a category.)

        // (What a stack IS and DOES prints through the shared shopfront —
        // ui/trade_widgets.h draw_item_ref_name / draw_item_ref_bonuses —
        // one spelling for the bag, the body and the counter, instance
        // affixes included.)
        // (ПРЕВЬЮ ПЛАНИРОВКИ ГОРОДА УНИЧТОЖЕНО 2026-10-19 вместе со своей
        //  вкладкой (M-290, вердикт владельца «карта города это фигня»).
        //  Здесь жили: кеш-синглтон с ImGui-текстурой и своим ключом
        //  свежести, `settlement_preview_seed`, `preview_flock_` и
        //  `ensure_settlement_preview` — ~200 строк, которые на кадр панели
        //  собирали полный `CellContext` с кольцами соседей 3×3 и 5×5 и
        //  ПРОГОНЯЛИ ГЕНЕРАТОР СУБМИРА (`sub::dispatch_generate`), чтобы
        //  растеризовать 256×256 картинку. Читатель был ровно один — та
        //  вкладка. Словарь цвета тайлов `subworld_tile_color` ОСТАЛСЯ: его
        //  второй читатель — миникарта субмира ниже в этом же файле.)

        // THE tile → colour dictionary of every 2D subworld rendering in this
        // file (the settlement preview AND the subworld minimap — it lived as
        // two identical copies until 2026-08-29).
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

        // (Строковая перегрузка этой строки умерла вместе со своим последним
        // читателем и с тех пор только светила -Wunused-function: каждая
        // анкета панели печатает ЧИСЛО. Вернётся — вернётся вместе с
        // читателем.)
        void draw_info_overview_row(const char *label, int value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
            ImGui::TableNextColumn();
            ImGui::Text("%d", value);
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
            case ObjectiveKind::DeliverItems:
                ImGui::BulletText("%s Deliver %d x %s to settlement %d",
                                  done, o.quantity, o.itemId.c_str(), o.targetSettlementId);
                break;
            case ObjectiveKind::DestroyNpc:
                ImGui::BulletText("%s Defeat type %d: %d/%d",
                                  done, o.npcType, o.killed, o.count);
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
            case RewardKind::Reputation:
                ImGui::BulletText("%+d reputation: %s", r.delta, r.faction.c_str());
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

        // (No `draw_inventory_table`. The bag's table is written where the
        // tab is, because it now carries a BUTTON per row — a read-only
        // printer of the same rows beside it would be the second one to drift.)

        // (No attribute row table here either. The label and what a point buys
        // already stand in kAttributeDefs (macro/attributes.h); a panel that
        // restates a registry drifts from it the first time a number is tuned.)

        // (No reset/recompute helpers. Every «his sheet changed» moment —
        // level-up, point spend, learning — calls THE one door
        // refresh_player_body (macro/player_entity.h): ceilings follow the
        // effective sheet, each bar keeps its FRACTION — «доля у всех»,
        // owner 2026-09-10. The level-up full heal died with that verdict:
        // the lord's law is the only rescale, and a lord's level never
        // healed him.)

        // The panel's derived block after a spend — same door as its header.
        DerivedBonuses calculate_derived_effective(const GameState &gs,
                                                   MacroStore &st)
        {
            const CharacterSheet eff =
                player_effective_sheet(gs, st);
            return calculate_derived(eff.attributes, eff.skills);
        }

        // Width of a number field that never reflows: three digits plus the
        // spaces around them. Rank caps at 100 and attributes are far below
        // it, so this is the widest a value can render.
        float number_field_x()
        {
            return ImGui::CalcTextSize(" 999 ").x;
        }

        // Width of a "value [+]" column, MEASURED rather than guessed: the
        // number field plus the button. The panel scales its font
        // (SetWindowFontScale), so a hardcoded column width and a measured
        // button offset would part ways the moment the UI scale moved — this
        // way the column, the offset and the font always agree.
        float spend_column_width()
        {
            const ImGuiStyle &st = ImGui::GetStyle();
            return number_field_x() + ImGui::CalcTextSize("+").x
                   + st.FramePadding.x * 2.0f + st.ItemSpacing.x;
        }

        // Fixed x for a spend button that follows a "label value" line: the
        // WIDEST label of the block plus that number field. One column for
        // every row, so the [+] cannot jump when a value crosses 9 -> 10 or a
        // neighbouring row has a longer label. Measured per frame, so it
        // follows the UI font scale for free. Works for any row struct whose
        // first business is a `label`.
        template <typename Row>
        float widest_label_x(const Row *rows, std::size_t count)
        {
            float w = 0.0f;
            for (std::size_t i = 0; i < count; ++i)
                w = std::max(w, ImGui::CalcTextSize(rows[i].label).x);
            return w + number_field_x();
        }
    } // namespace

    void draw_character_panel(GameState &gs, MacroStore &st,
                              sub::SubObjects *sceneObjs, bool *open,
                              CharacterPanelTab *tab, float scale)
    {
        // What the last swallow did, kept between frames so the player can
        // read "+5 HP" instead of watching it flash past in one.
        static std::string lastUseMessage;
        if (!open || !*open)
            return;

        PlayerState &p = gs.player;
        // His bag and his men both live on his squad entity now; a world that
        // has none yet reads as an empty pack rather than a crash.
        Inventory *bagPtr = player_inventory(gs, st);
        Inventory bagFallback{};
        Inventory &playerBag = bagPtr ? *bagPtr : bagFallback;
        // His bars, through the one door (landing 4) — same fallback shape
        // as the bag: a world that has none yet shows zeros, not a crash.
        ecs::Pools *poolsPtr = player_pools(gs, st);
        ecs::Pools poolsFallback{};
        ecs::Pools &pools = poolsPtr ? *poolsPtr : poolsFallback;
        const Inventory *army = bagPtr;   // армия = область существ (M-71)
        const CharacterPanelTab current = tab ? *tab : CharacterPanelTab::Stats;
        // His BASE sheet, through the one door (посадка Б) — same fallback
        // shape as the bag and the bars: a world that has none yet shows a
        // blank build, not a crash. The spend buttons write THIS block.
        CharacterSheet *sheetPtr = player_sheet(gs, st);
        CharacterSheet sheetFallback{};
        CharacterSheet &sheet = sheetPtr ? *sheetPtr : sheetFallback;
        // The panel SHOWS the EFFECTIVE sheet (phase 4, owner: «финальное
        // после всего — и его везде использует»); the spend buttons below
        // still write the BASE one, which is the only thing they may touch.
        // Mutable: a spend refreshes it in place so the row shows the new
        // number this very frame, not the next.
        // СУБЪЕКТ ПАНЕЛИ — ФЛАЖОК, А НЕ РОДНОЙ СКВАД (M-301). Здесь стоял
        // `player_squad_handle`, тогда как лист, полосы и сумка этой же
        // панели читаются по ФЛАЖКУ (`player_sheet`/`player_pools`/
        // `player_inventory` → `flag_slot_or_none`). При вселении панель
        // показывала надетое и сумму ОДНОГО сквада поверх листа и сумки
        // ДРУГОГО, а двери трат писали в третий. Флажок и есть «кем я на
        // карте сейчас» — им отвечают все остальные половины анкеты, и
        // теперь им же отвечает эта.
        const MacroHandle panelSquadH = player_flag_handle(gs);
        const BonusTotals panelStanding = st.valid(panelSquadH)
            ? standing_bonuses_of(st, panelSquadH)
            : BonusTotals{};
        CharacterSheet effPanel = player_effective_sheet(gs, st);
        DerivedBonuses derived = calculate_derived(effPanel.attributes,
                                                   effPanel.skills,
                                                   panelStanding);
        // КИЛОГРАММЫ — ЭТО ПОКАЗ, И ЗДЕСЬ ФЛОАТ ЗАКОНЕН (ЗАКОН ТИПА п.5:
        // представление). Внутри мира величина целая в граммах от колонки
        // до цены перегруза; килограмм рождается ТОЛЬКО для глаза,
        // в этих двух строках и в `trade_widgets.h`.
        const float carryWeight =
            float(double(inventory_mass_g(playerBag)) / 1000.0);
        const float carryCap =
            float(double(get_carry_capacity(effPanel.attributes,
                                            effPanel.skills,
                                            panelStanding)) / 1000.0);
        const int armyTotal = army ? creature_count(*army) : 0;
        // Upkeep is maintenance, not a deal (2026-09-17): no CHA discount,
        // and the bill is the SEASON'S — paid a season ahead at the boundary
        // window, the same law every squad pays by.
        const int armyUpkeep = army
            ? calculate_squad_upkeep(*army) * kDaysPerSeason : 0;

        ImGui::SetNextWindowSize(ImVec2(760 * scale, 560 * scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Character", open))
        {
            ImGui::SetWindowFontScale(scale);
            ImGui::Text("%s  Level %d  Age %d days",
                        p.name.empty() ? "Wanderer" : p.name.c_str(),
                        sheet.levelData.level, p.ageDays);
            ImGui::SameLine();
            ImGui::TextDisabled("EXP %d / %d", sheet.levelData.exp,
                                exp_to_next_of(sheet.levelData));
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
                        ImGui::Text("HP %d / %d", pools.hp, pools.maxHp);
                        ImGui::Text("MP %d / %d", pools.mp, pools.maxMp);
                        ImGui::Text("SP %d / %d", pools.sp, pools.maxSp);
                        ImGui::Text("Value %d", container_worth(playerBag));
                        ImGui::Text("Attr pts %d", sheet.levelData.attributePoints);
                        ImGui::Text("Skill pts %d", sheet.levelData.skillPoints);
                        if (sheet.levelData.learnPicks > 0)
                            ImGui::Text("Learn picks %d", sheet.levelData.learnPicks);
                        if (sheet.levelData.exp >= exp_to_next_of(sheet.levelData))
                        {
                            if (ImGui::Button("Level Up"))
                            {
                                squad_level_up(st, panelSquadH);
                            }
                        }
                        ImGui::TableNextColumn();
                        // The [+] sits at ONE x for the whole block: measured
                        // from the widest label plus a three-digit number
                        // field, so it does not jump when a value grows a
                        // digit (9 -> 10) or a row has a longer label. Measured
                        // per frame, so it follows the UI font scale for free.
                        const float attrPlusX = widest_label_x(
                            kAttributeDefs,
                            sizeof(kAttributeDefs) / sizeof(kAttributeDefs[0]));
                        for (const AttributeDef &row : kAttributeDefs)
                        {
                            ImGui::PushID(row.label);
                            // EFFECTIVE number on display — what a worn ring
                            // adds is part of what he IS right now. The spend
                            // door below still writes the base sheet only.
                            ImGui::Text("%s %d", row.label,
                                        effPanel.attributes.of(row.id));
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", row.effect);
                            ImGui::SameLine(attrPlusX);
                            ImGui::BeginDisabled(sheet.levelData.attributePoints <= 0);
                            if (ImGui::SmallButton("+"))
                            {
                                if (squad_spend_attribute(st, panelSquadH, row.id))
                                {
                                    effPanel = player_effective_sheet(gs, st);
                                    derived = calculate_derived_effective(gs, st);
                                }
                            }
                            ImGui::EndDisabled();
                            ImGui::PopID();
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("Phys dmg +%d", derived.rawPhysDamage);
                        ImGui::Text("Spell dmg +%d", derived.rawSpellDamage);
                        ImGui::Text("EXP %d%%", derived.expMultPct);
                        ImGui::Text("Move %d%%", derived.moveSpeedPct);
                        ImGui::Text("Trade %d%%", derived.tradeDiscountPct);
                        ImGui::Text("Carry %.1f / %.0f kg", carryWeight, carryCap);
                        ImGui::EndTable();
                    }
                    ImGui::Spacing();
                    if (ImGui::BeginTable("skills_grid", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Skill");
                        ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed,
                                                spend_column_width());
                        ImGui::TableHeadersRow();
                        for (const SkillDef &row : kSkillDefs)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", row.label);
                            if (ImGui::IsItemHovered())
                            {
                                // The tooltip is ARITHMETIC off the row, not
                                // prose beside it: a retuned percent cannot
                                // leave a stale sentence behind.
                                ImGui::SetTooltip("%c%d%% %s",
                                                  row.buysCostDown ? '-' : '+',
                                                  int(row.pctPerRank), row.effect);
                            }
                            ImGui::TableNextColumn();
                            ImGui::PushID(row.label);
                            // The DOORS are gated by the BASE rank — the learn
                            // law lives on the base sheet, and a charm's +3 to
                            // an unlearned craft must not eat a learn pick's
                            // job. The DISPLAY is the effective rank — the
                            // number the formulas actually read.
                            const int rank = sheet.skills.of(row.id);
                            const int rankShown = effPanel.skills.of(row.id);
                            if (rank == 0)
                            {
                                // THE learn law: rank 0 IS ignorance. The row
                                // offers the learn door, not the spend door —
                                // creation picks feed it today, teachers and
                                // events feed the same door tomorrow. A worn
                                // bonus can still put a WORKING rank on an
                                // unlearned craft — print it, honestly.
                                if (rankShown > 0)
                                    ImGui::Text("%d", rankShown);
                                else
                                    ImGui::TextDisabled("—");
                                ImGui::SameLine(number_field_x());
                                ImGui::BeginDisabled(sheet.levelData.learnPicks <= 0);
                                if (ImGui::SmallButton("Learn"))
                                {
                                    if (squad_spend_learn(st, panelSquadH, row.id))
                                    {
                                        effPanel = player_effective_sheet(gs, st);
                                        derived = calculate_derived_effective(gs, st);
                                    }
                                }
                                ImGui::EndDisabled();
                            }
                            else
                            {
                                ImGui::Text("%d", rankShown);
                                // Same rule as the attribute rows: the [+] sits
                                // at a fixed number-field width, not right
                                // after the digits, so rank 10 does not push
                                // it sideways.
                                ImGui::SameLine(number_field_x());
                                ImGui::BeginDisabled(sheet.levelData.skillPoints <= 0);
                                if (ImGui::SmallButton("+"))
                                {
                                    if (squad_spend_skill(st, panelSquadH, row.id))
                                    {
                                        effPanel = player_effective_sheet(gs, st);
                                        derived = calculate_derived_effective(gs, st);
                                    }
                                }
                                ImGui::EndDisabled();
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    // (The perk grid died with the 2026-09-03 purge: six of
                    // its eight rows promised mechanics that did not exist,
                    // and the Talented branch was the hardcode the bonus
                    // registry forbids. Since v100 the third currency accrues
                    // honestly — the constellation graph it spends into is
                    // consciously absent for the demo, owner 2026-09-19.)
                    ImGui::Spacing();
                    ImGui::Text("Perks");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d points)", sheet.levelData.perkPoints);
                    ImGui::TextDisabled("Points accrue; the constellation graph arrives after the demo.");
                    ImGui::EndTabItem();
                }

                const bool inventoryOpen = ImGui::BeginTabItem("Inventory", nullptr,
                                                               selected_tab(current, CharacterPanelTab::Inventory));
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Inventory;
                if (inventoryOpen)
                {
                    ImGui::Text("Stacks: %d  Items: %d  Weight: %.1f / %.0f kg",
                                playerBag.used_slots(), playerBag.total(), carryWeight, carryCap);
                    // USE. `use_item` has existed and been tested since the
                    // port, with no way for a player to reach it: work_vector
                    // §5 lists "use_item наконец получает кнопку в UI" as part
                    // of the RPG layer, and a potion nobody can drink is a
                    // system that only the test suite plays.
                    //
                    // The row itself says whether it is drinkable — the door
                    // takes potions and food and refuses everything else — so
                    // the button appears exactly where the catalog says it
                    // should, and no category is restated here.
                    if (!lastUseMessage.empty())
                        ImGui::TextDisabled("%s", lastUseMessage.c_str());
                    // THE 32×32 GRID (владелец 2026-09-11): the bag draws
                    // the very shape the data always had — one widget, the
                    // same the two trade counters draw. Click selects; the
                    // detail line below names the stack and offers Use.
                    static int selectedSlot = -1;
                    // ПРОКРУТКА — ЧТОБЫ ХВОСТ СЕТКИ БЫЛ ДОСТИЖИМ (M-292,
                    // поймано кадром): контейнер 32×32, панель показывает
                    // около шестнадцати рядов, а область существ лежит вниз
                    // от слота 1023 — ровно в невидимых. Резерв выведен из
                    // того, что рисуется НИЖЕ: блок подробностей слота — это
                    // десять строк в худшем случае (имя, бонусы, описание на
                    // три строки, «Use», превью скрапа), и они обязаны
                    // остаться на экране, пока игрок листает сумку.
                    const float bagGridH =
                        -ImGui::GetTextLineHeightWithSpacing() * 10.0f;
                    ImGui::BeginChild("##charbag_scroll",
                                      ImVec2(0.0f, bagGridH));
                    const GridClick click =
                        draw_inventory_grid("##charbag", playerBag);
                    ImGui::EndChild();
                    if (click.slot >= 0)
                        selectedSlot = click.slot;
                    if (selectedSlot >= 0
                        && (playerBag.slots[std::size_t(selectedSlot)].empty()))
                        selectedSlot = -1;
                    ImGui::Separator();
                    if (selectedSlot >= 0)
                    {
                        const ItemRef &st =
                            playerBag.slots[std::size_t(selectedSlot)];
                        // ГЕЙТ — СТРОКА МИРА, А НЕ ПРЕДМЕТНАЯ ПОЛОВИНА
                        // (M-292): здесь стоял `if (const ItemDef *def =
                        // item_def_at(…))`, и выбранный слот существа не
                        // печатал НИЧЕГО — ни имени, ни счёта, ни цены, —
                        // хотя сетка выше его уже рисует. Личность стака
                        // общая для обеих половин; предметная строка нужна
                        // ниже только ГЛАГОЛАМ (описание, глоток), и они
                        // молчат на существе по своим же колонкам.
                        if (world_row_exists(st.def))
                        {
                            const ItemDef *def = item_def_at(int(st.def));
                            draw_item_ref_name(st);
                            ImGui::SameLine();
                            ImGui::Text("x%d   %d g", st.count, value_of(st));
                            draw_item_ref_bonuses(st);
                            if (def && def->description
                                && def->description[0])
                                ImGui::TextWrapped("%s", def->description);
                            if (def && archetype_consumable(item_archetype(*def))
                                && ImGui::SmallButton("Use"))
                            {
                                // ГЛОТОК — СПОСОБНОСТЬ (CANON S13, вердикт
                                // владельца 2026-09-19): рука должна быть
                                // свободна, и глоток занимает её на срок,
                                // который назвала СТРОКА зелья. Иначе в
                                // пошаговом режиме застывший мир позволял
                                // пить бесконечно — мир-то стоит.
                                ecs::Combat* gate = nullptr;
                                if (sub::SubObjects* objs = sceneObjs) {
                                    const int s = sm::sub::avatar_slot(*objs);
                                    if (s >= 0) {
                                        gate = sm::sub::slot_combat(*objs, s);
                                    }
                                }
                                if (!sm::sub::body_is_free(gate)) {
                                    lastUseMessage = "Still recovering.";
                                } else {
                                PlayerCombatSlice pc{
                                    pools.hp, pools.maxHp,
                                    pools.mp, pools.maxMp,
                                    pools.sp, pools.maxSp};
                                lastUseMessage =
                                    use_item(playerBag, def->id, pc);
                                pools.hp = pc.currentHp;
                                pools.mp = pc.currentMp;
                                pools.sp = pc.currentSp;
                                // Занять руки — через ТУ ЖЕ дверь, что удар
                                // и каст; лист берётся эффективный, так что
                                // хаста ускоряет глоток без кода здесь.
                                sm::sub::charge_ability(
                                    gate, def->useSeconds,
                                    effPanel.attributes, effPanel.skills,
                                    sm::SkillId::Armsmaster);
                                }
                            }
                            // SCRAP — the reverse ход of the one reaction
                            // (CANON «Крафт/Скрап»: «игрок разбирает
                            // осознанно»). Like Use above, the row itself
                            // says whether it applies: a composition on the
                            // catalog row puts the button here, terminals
                            // (raw matter, coins) simply never show it. The
                            // preview prints the door's own arithmetic —
                            // floor(count/2) per part, variant byte
                            // substituting part 0 — so the player reads the
                            // entropy price before paying it.
                            const auto parts = item_parts(int(st.def));
                            if (!parts.empty())
                            {
                                // The preview prints the door's own POOLED
                                // arithmetic — floor(n×count/(2×yield)) per
                                // part, the variant byte substituting part 0
                                // — so the player reads the entropy price of
                                // exactly the n he is about to pay.
                                const int rowYield = item_yield(int(st.def));
                                const auto returns_for = [&](int n) {
                                    std::string back;
                                    for (std::size_t pi = 0;
                                         pi < parts.size(); ++pi)
                                    {
                                        int mat = int(parts[pi].def);
                                        if (pi == 0 && st.material != 0
                                            && int(st.material)
                                                   <= kRawCommodityCount)
                                        {
                                            const int sub = item_index(
                                                kCommodities[st.material - 1]
                                                    .id);
                                            if (sub >= 0) mat = sub;
                                        }
                                        const int amt =
                                            n * int(parts[pi].count)
                                            / (2 * rowYield);
                                        if (amt <= 0) continue;
                                        const ItemDef *md = item_def_at(mat);
                                        if (!md) continue;
                                        if (!back.empty()) back += ", ";
                                        back += std::to_string(amt);
                                        back += "x ";
                                        back += md->name;
                                    }
                                    return back;
                                };
                                const std::string one = returns_for(1);
                                if (st.count > 1)
                                {
                                    const std::string all =
                                        returns_for(st.count);
                                    ImGui::TextDisabled(
                                        "Scrap 1 -> %s   |   all %d -> %s",
                                        one.empty() ? "slag" : one.c_str(),
                                        st.count,
                                        all.empty() ? "slag" : all.c_str());
                                }
                                else
                                {
                                    ImGui::TextDisabled(
                                        "Scrap -> %s",
                                        one.empty() ? "slag (the parts burn)"
                                                    : one.c_str());
                                }
                                const auto scrap = [&](int n) {
                                    const std::string back = returns_for(n);
                                    if (scrap_at(playerBag, selectedSlot, n))
                                    {
                                        lastUseMessage = "Scrapped ";
                                        lastUseMessage += def->name;
                                        lastUseMessage += back.empty()
                                            ? ": all of it burnt to slag."
                                            : (": +" + back + ".");
                                    }
                                    else
                                    {
                                        // The door is all-or-nothing: the
                                        // one honest refusal here is a bag
                                        // with no room for the returns.
                                        lastUseMessage =
                                            "No room for the scrap returns.";
                                    }
                                };
                                if (ImGui::SmallButton("Scrap 1")) scrap(1);
                                if (st.count > 1)
                                {
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Scrap all"))
                                        scrap(st.count);
                                }
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            playerBag.used_slots() == 0
                                ? "(empty)"
                                : "Click a stack to inspect it.");
                    }
                    ImGui::EndTabItem();
                }

                // CRAFT — the forward ход of the one reaction (CANON
                // «Крафт/Скрап»): full price in materials, a WHITE base out
                // (seed 0 — affixes are born in the world, never at a
                // bench). The list is the CATALOG's own answer: every row
                // with a composition, COIN INCLUDED (owner 2026-09-12:
                // «У НАС БАРТЕРНАЯ ЭКОНОМИКА» — no currency filter exists;
                // minting is value-neutral by the static_assert beside the
                // table, so the bench forges nothing a smith doesn't).
                const bool craftOpen = ImGui::BeginTabItem("Craft", nullptr,
                                                           selected_tab(current, CharacterPanelTab::Craft));
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Craft;
                if (craftOpen)
                {
                    static std::string lastCraftMessage;
                    ImGui::TextDisabled(
                        "Craft pays the full composition; scrap returns half"
                        " (the Inventory tab's button on any crafted thing).");
                    if (!lastCraftMessage.empty())
                        ImGui::TextDisabled("%s", lastCraftMessage.c_str());
                    if (ImGui::BeginTable("craft_grid", 3,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Item");
                        ImGui::TableSetupColumn("Materials");
                        ImGui::TableSetupColumn("");
                        ImGui::TableHeadersRow();
                        const auto catalog = item_catalog();
                        for (int ci = 0; ci < int(catalog.size()); ++ci)
                        {
                            const auto parts = item_parts(ci);
                            if (parts.empty())
                                continue;
                            const int yield = item_yield(ci);
                            const ItemDef &row = catalog[std::size_t(ci)];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            // One batch makes `yield` — the mint's 32 coins
                            // say so right in the row name.
                            if (yield > 1)
                                ImGui::Text("%dx %s", yield, row.name);
                            else
                                ImGui::Text("%s", row.name);
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("%d g   %.1f kg", row.value,
                                            double(row.massG) / 1000.0);
                                if (row.description && row.description[0])
                                    ImGui::TextWrapped("%s", row.description);
                                ImGui::EndTooltip();
                            }
                            ImGui::TableNextColumn();
                            bool can = true;
                            bool first = true;
                            for (const ItemPart &part : parts)
                            {
                                const ItemDef *md = item_def_at(int(part.def));
                                if (!md) continue;
                                const int have =
                                    playerBag.count_of(int(part.def));
                                const bool enough = have >= int(part.count);
                                can = can && enough;
                                if (!first) ImGui::SameLine();
                                first = false;
                                // Short of a material = the line says so in
                                // red; the button below stays honest for
                                // free (the door re-checks regardless).
                                if (enough)
                                    ImGui::Text("%dx %s (%d)",
                                                int(part.count), md->name,
                                                have);
                                else
                                    ImGui::TextColored(
                                        ImVec4(0.9f, 0.35f, 0.3f, 1.0f),
                                        "%dx %s (%d)", int(part.count),
                                        md->name, have);
                            }
                            // The SP price of ONE batch: a person-day is a
                            // FULL bar (the kGatherPerWorkerDay anchor's own
                            // derivation), so a batch costs bar / labour —
                            // and it is paid INTO THE NEGATIVE (owner
                            // 2026-09-12, «как в Elin», the march law): the
                            // button never greys on fatigue, forging on an
                            // empty bar simply leaves you exhausted for the
                            // exhaustion bite to find.
                            const int spCost =
                                sp_price(int(pools.maxSp), item_labour(ci));
                            ImGui::SameLine();
                            ImGui::TextDisabled("SP %d", spCost);
                            ImGui::TableNextColumn();
                            ImGui::PushID(ci);
                            ImGui::BeginDisabled(!can);
                            if (ImGui::SmallButton("Craft"))
                            {
                                if (craft_item(playerBag, ci, 1))
                                {
                                    // THE spend door (movement_cost.h): the
                                    // debit and the one QUADRATIC law of
                                    // zero in a single move — forging into
                                    // the negative bites HP right here, not
                                    // on some later step (S14.1: «любая
                                    // затрата ниже 0 бьёт»).
                                    apply_stamina_cost(pools, spCost);
                                    lastCraftMessage = "Crafted ";
                                    if (yield > 1)
                                    {
                                        lastCraftMessage +=
                                            std::to_string(yield);
                                        lastCraftMessage += "x ";
                                    }
                                    lastCraftMessage += row.name;
                                    lastCraftMessage += ".";
                                }
                                else
                                {
                                    // Materials were shown green, so the one
                                    // honest refusal left is a full bag.
                                    lastCraftMessage =
                                        "No room in the bag for it.";
                                }
                            }
                            ImGui::EndDisabled();
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                const bool armyOpen = ImGui::BeginTabItem("Army", nullptr,
                                                          selected_tab(current, CharacterPanelTab::Army));
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Army;
                if (armyOpen)
                {
                    ImGui::Text("Total: %d  Upkeep: %d g/season", armyTotal, armyUpkeep);
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
                            const int count =
                                army ? creature_count_of(*army, t) : 0;
                            if (count <= 0 && !npc_hireable(t))
                                continue;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", npc_def(t).label);
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", count);
                            ImGui::TableNextColumn();
                            ImGui::Text("%d g/season",
                                        npc_upkeep_base(t) * kDaysPerSeason);
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
                    // THE equipment tab, and it is real now. The stub it
                    // replaces said "Equip/unequip needs PlayerEquipment
                    // slots, compatibility rules, stat recomputation, and save
                    // serialization" — all four exist (macro/anatomy.h), so
                    // what was missing was only the hands to use them.
                    ecs::BodyEquipment *eqc = nullptr;
                    if (st.valid(panelSquadH))
                    {
                        // ФЛИП 1в: гир — колонка store у каждого сквада.
                        // Субъект — ФЛАЖОК, как и у сумки, в которую эта
                        // вкладка пишет указатели надетого (M-301): до правки
                        // гир брался у родного сквада, а сумка у флажка, то
                        // есть при вселении надетое индексировало ЧУЖОЙ
                        // инвентарь.
                        eqc = &st.gear[panelSquadH.slot];
                    }

                    if (!eqc)
                    {
                        ImGui::TextDisabled("No body to dress (no world yet).");
                    }
                    else
                    {
                        Gear &gear = eqc->gear;
                        const BonusTotals worn = worn_bonuses(gear, playerBag);
                        // Worn defence is eight pairs of columns now; the
                        // panel prints the physical face of it (worn rows are
                        // uniform until per-column authoring lands) — a full
                        // 8-column readout is the sheet door's business.
                        // His EFFECTIVE training, so the panel prints the very
                        // number the blow will meet (CANON S14: the armour
                        // skill's rank multiplies its own kind) — a readout
                        // computed without the sheet would quietly disagree
                        // with the law the moment a rank was spent.
                        const sm::DefenseSum wornDef = sm::worn_defense(
                            gear, playerBag, effPanel.skills,
                            sm::DamageType::Blunt);
                        ImGui::Text("Body — armour %d, block %d",
                                    wornDef.armor, wornDef.block);
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(
                                "Block comes off the blow flat and is always "
                                "up; armour is the PERCENT taken off what is "
                                "left (%d%% = nothing gets through). Negative "
                                "armour adds damage instead.",
                                sm::kArmorFull);
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%d of %d slots filled)",
                                            worn_cells(gear), gear.has.count());

                        // What it is worth, in the one currency: the same
                        // BonusTotals a perk and a spell speak.
                        bool anyBonus = false;
                        for (int i = 0; i < int(AttributeId::Count); ++i)
                        {
                            if (worn.attr[std::size_t(i)] == 0) continue;
                            if (!anyBonus) { ImGui::SameLine(); anyBonus = true; }
                            else ImGui::SameLine();
                            ImGui::TextDisabled("%+d %s",
                                                int(worn.attr[std::size_t(i)]),
                                                attribute_def(AttributeId(i)).label);
                        }

                        ImGui::Separator();
                        ImGui::TextUnformatted("Worn");
                        int wornRows = 0;
                        if (ImGui::BeginTable("worn", 4,
                                              ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                        {
                            ImGui::TableSetupColumn("Part", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                            ImGui::TableSetupColumn("Item");
                            ImGui::TableSetupColumn("Effect");
                            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableHeadersRow();
                            for (int cell = 0; cell < kEquipCells; ++cell)
                            {
                                const std::uint16_t v =
                                    gear.worn[std::size_t(cell)];
                                if (v >= kWornBlocked) continue;
                                const ItemRef &r = playerBag.slots[std::size_t(v)];
                                if (r.empty()) continue;   // протухший индекс
                                const ItemDef *def = item_def_at(int(r.def));
                                if (!def) continue;
                                ++wornRows;
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::TextDisabled("%s",
                                                    body_part_def(equip_cell_part(cell)).label);
                                ImGui::TableNextColumn();
                                draw_item_ref_name(r);
                                ImGui::TableNextColumn();
                                draw_item_ref_bonuses(r);
                                ImGui::TableNextColumn();
                                ImGui::PushID(cell);
                                if (ImGui::SmallButton("Take off"))
                                {
                                    // Вещь никуда не движется — она и так в
                                    // сумке; снятие лишь гасит указатель
                                    // (истина одна — M-183). Дверь уровня
                                    // сквада: производные анкеты пересчитаны
                                    // внутри неё (M-301).
                                    squad_unequip(st, panelSquadH, cell);
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        if (wornRows == 0)
                            ImGui::TextDisabled("(nothing worn)");

                        ImGui::Separator();
                        ImGui::TextUnformatted("In the bag");
                        int equipStacks = 0;
                        if (ImGui::BeginTable("equipment_candidates", 4,
                                              ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                        {
                            ImGui::TableSetupColumn("Item");
                            ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                            ImGui::TableSetupColumn("Effect");
                            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableHeadersRow();
                            for (int slot = 0; slot < kMaxInventorySlots; ++slot)
                            {
                                const ItemRef &stack = playerBag.slots[std::size_t(slot)];
                                if (stack.empty()) continue;
                                if (slot_is_worn(gear, slot))
                                    continue;   // уже на теле — строка выше
                                const ItemDef *def = item_def_at(int(stack.def));
                                if (!def || def->slotMask == 0)
                                    continue;   // the row itself says it is not worn
                                ++equipStacks;
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                draw_item_ref_name(stack);
                                ImGui::TextDisabled("%s", def->id);
                                ImGui::TableNextColumn();
                                ImGui::Text("x%d", stack.count);
                                ImGui::TableNextColumn();
                                draw_item_ref_bonuses(stack);
                                ImGui::TableNextColumn();
                                ImGui::PushID(slot);
                                if (ImGui::SmallButton("Wear"))
                                {
                                    // Тело ОТКАЗЫВАЕТ, если некуда; вещь в
                                    // любом исходе остаётся в сумке —
                                    // надевание ставит ИНДЕКС (M-183), и
                                    // консервация держится по построению.
                                    squad_equip(st, panelSquadH, slot);
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        if (equipStacks == 0)
                            ImGui::TextDisabled("(nothing wearable in the bag)");
                    }
                    ImGui::EndTabItem();
                }

                const bool spellsOpen = ImGui::BeginTabItem("Spells", nullptr,
                                                            selected_tab(current, CharacterPanelTab::Spells));
                if (tab && ImGui::IsItemClicked())
                    *tab = CharacterPanelTab::Spells;
                if (spellsOpen)
                {
                    // His book, through the one door (v89): the component on
                    // his squad — the panel is a reader like any other.
                    static SpellBook bookScratch{};
                    SpellBook* bookPtr = player_spellbook(gs, st);
                    SpellBook& book = bookPtr ? *bookPtr : bookScratch;
                    ImGui::Text("MP %d / %d", pools.mp, pools.maxMp);
                    if (spell_ordinal_ok(book.activeSpell))
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled(
                            "Active: %s",
                            kSpellDefs[book.activeSpell].name);
                    }
                    ImGui::Separator();

                    if (spellbook_learned_count(book) == 0)
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
                        ImGui::TableSetupColumn("Recovery");
                        ImGui::TableSetupColumn("Power");
                        ImGui::TableSetupColumn("State");
                        ImGui::TableSetupColumn("Active");
                        ImGui::TableHeadersRow();
                        for (int ord = 0; ord < kSpellCount; ++ord)
                        {
                            if (!spellbook_has_learned(book, ord)) continue;
                            const SpellDef *def = &kSpellDefs[ord];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            if (def)
                            {
                                const char *icon =
                                    (def->icon && def->icon[0] != '\0') ? def->icon : "?";
                                ImGui::Text("%s %s", icon, def->name);
                            }
                            if (def && ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                const char *icon =
                                    (def->icon && def->icon[0] != '\0') ? def->icon : "?";
                                ImGui::Text("%s %s", icon, def->name);
                                ImGui::TextDisabled("%s / Tier %d / %s",
                                                    spell_rarity_label(def->rarity),
                                                    def->tier,
                                                    spell_shape_label(def->shape));
                                // Casts are instant (verdict 2026-09-09) —
                                // the row's one tempo number is what the
                                // body owes AFTER, through the S14 door.
                                if (def->recovery > 0.0f)
                                    ImGui::TextDisabled("Recovery %.1fs",
                                                        def->recovery);
                                else
                                    ImGui::TextDisabled("No recovery");
                                if (def->description && def->description[0] != '\0')
                                {
                                    ImGui::Separator();
                                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
                                    ImGui::TextUnformatted(def->description);
                                    ImGui::PopTextWrapPos();
                                }
                                // What it DOES, read off the rows rather than
                                // off a vocabulary nobody consumed: stat
                                // effects from the one bonus registry, and the
                                // rule it switches, if any.
                                if (def->rule != SpellRuleId::None)
                                {
                                    ImGui::Separator();
                                    ImGui::TextDisabled("World: %s",
                                                        spell_rule_label(def->rule));
                                }
                                for (const Bonus &b : def->effects)
                                {
                                    if (b.row == 0 || b.value == 0) continue;
                                    ImGui::Separator();
                                    ImGui::TextDisabled("%+d %s", int(b.value),
                                                        bonus_def(BonusId(b.row)).label);
                                }
                                if (spell_flavor_count(def->pros) > 0)
                                {
                                    ImGui::Separator();
                                    ImGui::TextUnformatted("Pros");
                                    const int n = spell_flavor_count(def->pros);
                                    for (int i = 0; i < n; ++i)
                                        ImGui::BulletText("%s", def->pros[i]);
                                }
                                if (spell_flavor_count(def->cons) > 0)
                                {
                                    ImGui::Separator();
                                    ImGui::TextUnformatted("Cons");
                                    const int n = spell_flavor_count(def->cons);
                                    for (int i = 0; i < n; ++i)
                                        ImGui::BulletText("%s", def->cons[i]);
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
                                if (def->secondaryTag != def->tag)
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
                            // The row's authored recovery: live «busy» state
                            // is the BODY's one gate, not a book cell — and a
                            // world open on this panel is a paused world.
                            if (def && def->recovery > 0.0f)
                                ImGui::Text("%.1fs", def->recovery);
                            else
                                ImGui::TextDisabled("-");
                            ImGui::TableNextColumn();
                            if (def)
                            {
                                const int dmg = spell_damage(*def, sheet.attributes, sheet.skills);
                                const int heal = spell_heal(*def, sheet.attributes, sheet.skills);
                                const int rad = spell_radius(*def, sheet.attributes, sheet.skills);
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
                            if (def && def->sustained && spellbook_has_sustained(book, ord))
                            {
                                ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Sustained");
                            }
                            else
                            {
                                // Gate 0: the panel pauses the world, so the
                                // body's recovery is not racing this frame.
                                const CastCheck check = spellbook_can_cast_ex(
                                    book, pools, ord, true, 0u);
                                if (check.ok)
                                {
                                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "Ready");
                                }
                                else if (check.recoveryRemaining > 0.0f)
                                {
                                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                                                       "Recovery");
                                }
                                else
                                {
                                    ImGui::TextDisabled("%s", check.reason.c_str());
                                }
                            }
                            ImGui::TableNextColumn();
                            ImGui::PushID(ord);
                            if (book.activeSpell == ord)
                            {
                                ImGui::TextDisabled("Selected");
                            }
                            else if (ImGui::SmallButton("Set"))
                            {
                                spellbook_set_active(book, ord);
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

    // ── ОДНА СТРАНИЦА СУБЪЕКТА, ОДИН ХЭНДЛ, РАЗЛИЧИЯ — КОЛОНКАМИ ─────────
    // Здесь ветвились НАДВОЕ: ветка сквада (186 строк, 2 вкладки) против
    // ветки места (477 строк, 8 вкладок), — и ровно потому у приложения жило
    // ДВА носителя субъекта, а клик по ряду обязан был выбрать ветвь. Ветвей
    // нет, носитель один, и выбирать стало нечего (M-290).
    //
    // ЧЕМ ОТЛИЧАЕТСЯ ГОРОД ОТ ХОДОКА — ЭТО КОЛОНКИ, И ВСЕ ОНИ УЖЕ БЫЛИ:
    //   · какие ЦИФРЫ у баннера — `figuresFromPools@src/ui/squad_draw.h`
    //     («потолки ТЕЛА или ПАСТВА»), ключ архетипа, второй её читатель;
    //   · какие ГЛАГОЛЫ и ВКЛАДКИ — маска `actions_of@src/macro/squad.h`,
    //     и это УЖЕСТОЧЕНИЕ: ветка сквада маску не читала вовсе и рисовала
    //     Talk/Attack безусловно;
    //   · какое ИМЯ и ПОДПИСЬ — цепочка колонок «своё имя → ярлык строки
    //     рода → ярлык строки существа»; у подвижных родов ярлык пуст, и
    //     цепочка сама доходит до тела.
    // Ветки по роду не осталось ни одной: `squad_archetype` работает здесь
    // КЛЮЧОМ таблицы, как разрешает его шапка, а не гейтом прохода.
    //
    // КОНВЕРТ МИРА ИЗ СИГНАТУРЫ УШЁЛ ВМЕСТЕ С ЕДИНСТВЕННЫМ ЧИТАТЕЛЕМ: его
    // просила вкладка `Map`, собиравшая по нему факты клетки и кольца
    // соседей. Панель субъекта спрашивает КОЛОНКИ СВОЕГО СЛОТА, и больше
    // ничего — канал у неё ровно такой ширины.
    //
    // И ВКЛАДКИ ПЕРЕСТАЛИ БЫТЬ ПРИВИЛЕГИЕЙ МЕСТА. Garrison и Inventory были
    // безусловны у места и отсутствовали у ходока — по ЗАКОНУ КОНТЕЙНЕРА это
    // один и тот же контейнер (`st.inventory[slot].inv`), значит и вкладки
    // одни: у артели есть ростер и груз ровно так же, как у города.
    void draw_settlement(GameState &gs,
                         MacroStore &st,
                         MacroHandle subject,
                         const std::vector<Quest> &availableQuests,
                         std::vector<Quest> &activeQuests,
                         QuestEngine &questEngine,
                         EventBus &bus,
                         SettlementPanelTab *tab,
                         bool *open,
                         MacroHandle *attackRequest,
                         float scale)
    {
        if (!open || !*open)
            return;
        // The player's bag rides his squad entity (macro/player_entity.h).
        Inventory *bagPtr = player_inventory(gs, st);
        Inventory bagFallback{};
        Inventory &playerBag = bagPtr ? *bagPtr : bagFallback;

        // ОДИН ГЕЙТ ЖИВОСТИ НА ОБОИХ, И ОН FAIL-CLOSED. Прежде их было два:
        // ветка сквада закрывала панель, ветка места печатала «No settlement
        // selected (id=…)» — то есть протухший ординал ОСТАВЛЯЛ окно стоять.
        // Протухший субъект нечего показывать, чем бы он ни был.
        //
        // СМЕРТЬ СПРАШИВАЕТСЯ У `dead`, А НЕ У HP, И ЭТО НЕ ПРИДИРКА — ЭТОТ
        // ГЕЙТ УЖЕ ОДИН РАЗ УБИЛ ПАНЕЛЬ ЦЕЛИКОМ. Ветка сквада закрывалась по
        // `hp <= 0`, и при слиянии условие поехало на всех; у места же пулы
        // рождаются НУЛЯМИ («pools — НУЛЯМИ (CANON S6)»,
        // `birth_place@src/macro/place_birth.h`), потому что город не дерётся
        // телом — за него дерутся головы его контейнера (ЗАКОН АНКЕТЫ п.5).
        // То есть `hp == 0` у места есть ЗАКОННОЕ значение, а не труп, и
        // панель города перестала открываться вовсе. Поймал КАДР: 130 тестов
        // и 64 смоука остались зелёными (§8 п.6, пятая слепота — паритет
        // судит числа, а тут не сдвинулось ни одно). Дверь смерти одна —
        // `macro_dead@src/macro/store.h` над колонкой `dead`; HP есть ЦИФРА
        // тела, и спрашивать её о жизни значит предполагать род субъекта.
        const bool alive = st.valid(subject)
            && !macro_dead(st, subject)
            && st.playerFlag[subject.slot].home == 0;
        if (!alive)
        {
            *open = false;
            return;
        }

        const std::uint16_t slot = subject.slot;
        Inventory &sInv = st.inventory[slot].inv;
        const ecs::Pools &pools = st.pools[slot];
        const auto &kindCol = st.kind[slot];
        const SquadType sKind = SquadType(st.runtime[slot].squadType);
        const LandmarkDef &row = landmark_def(sKind);
        const NPCType bodyKind =
            kindCol.type < std::uint16_t(NPCType::Count)
                ? NPCType(std::uint8_t(kindCol.type)) : NPCType::Peasant;
        const auto &bodyDef = npc_def(bodyKind);
        // ПОДПИСЬ — ЯРЛЫК СТРОКИ РОДА, А У ПОДВИЖНЫХ ОН ПУСТ (`.label = ""`),
        // и тогда отвечает строка ТЕЛА. Это цепочка колонок, а не ветка: род
        // «артель» нарочно не имеет своего ярлыка, потому что артель видно по
        // её вожаку (СПРАЙТ-ЗАКОН тем же доводом рисует её его спрайтом).
        const bool hasRowLabel = !row.label.empty();
        const char *sLabel = hasRowLabel ? row.label.data() : bodyDef.label;
        const char *sName = st.name[slot].text[0] != '\0'
            ? st.name[slot].text : sLabel;
        const int sx = ecs::cell_x(st.cell[slot], gs.mapW);
        const int sy = ecs::cell_y(st.cell[slot], gs.mapW);
        const std::int16_t sFaction = std::int16_t(kindCol.factionIdx);
        const sm::FactionDef *fd =
            sm::faction_def_by_index(sm::faction_or_freefolk(sFaction));
        // ЦИФРЫ СУБЪЕКТА — КОЛОНКА ГРУППЫ, НЕ ВЕТКА ПО РОДУ. Она же решает,
        // законно ли вообще спрашивать паству: `souls_flock` читает
        // worked-слой КЛЕТКИ, и у ходока, стоящего на чужой клетке, ответ был
        // бы чужим числом. Колонка это и предотвращает.
        const bool bodyFigures =
            archetype_draw(squad_archetype(sKind)).figuresFromPools;
        const std::uint16_t acts = actions_of(st, subject);
        const CharacterSheet &sSheet = st.sheet[slot];
        const int sLevel = squad_level(st, slot);
        const auto *traits = &st.traits[slot];

        // Прилавок и реплика ключуются ОРДИНАЛОМ субъекта (M-220: уникален на
        // мир), поэтому соседняя панель не наследует чужих слов и чужого
        // отложенного свёртка.
        g_subjectTrade.sync_to(int(subject.id));
        if (g_squadTalkFor != subject)
        {
            g_squadTalkFor = MacroHandle{};
            g_squadTalkLine = nullptr;
        }
        const SettlementPanelTab current = tab ? *tab : SettlementPanelTab::Info;

        ImGui::SetNextWindowSize(ImVec2(760 * scale, 620 * scale),
                                 ImGuiCond_FirstUseEver);
        char title[96];
        std::snprintf(title, sizeof(title), "%s###Settlement", sName);
        if (ImGui::Begin(title, open))
        {
            ImGui::SetWindowFontScale(scale);
            // ── Banner ──
            ImGui::PushFont(nullptr);
            ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.50f, 1.0f), "%s", sName);
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", sLabel);
            ImGui::Text("Faction: %s", fd ? fd->name : "Unaligned");
            if (bodyFigures)
            {
                ImGui::Text("Lv.%d   HP %d/%d", sLevel,
                            int(pools.hp), int(pools.maxHp));
            }
            else
            {
                ImGui::Text("Population: %d", souls_flock(gs, st, slot));
                ImGui::Text("Wellbeing: %d%%", wellbeing_pct(st, slot));
                ImGui::Text("Starved last boundary: %d",
                            int(st.wellbeing[slot].starvedYesterday));
            }
            // ГЛАГОЛЫ БАННЕРА — ПО МАСКЕ, И ЭТО СТРОЖЕ, ЧЕМ БЫЛО: ветка
            // сквада рисовала обе кнопки безусловно, мимо колонки, которая
            // уже объявляла их строкой (`kLandmarks`). Место Talk и Attack
            // не объявляет — и не рисует, ровно как прежде.
            if (acts & kMapActTalk)
            {
                if (ImGui::Button("Talk"))
                {
                    g_squadTalkFor = subject;
                    g_squadTalkLine = bodyDef.talkCount > 0
                        ? bodyDef.talkLines[st.character[slot].visualSeed
                                            % std::uint32_t(bodyDef.talkCount)]
                        : "...";
                }
                if (acts & kMapActAttack) ImGui::SameLine();
            }
            if (acts & kMapActAttack)
            {
                if (ImGui::Button("Attack"))
                {
                    if (attackRequest) *attackRequest = subject;
                    *open = false;
                }
            }
            if (g_squadTalkFor == subject && g_squadTalkLine)
            {
                ImGui::TextWrapped("\"%s\"", g_squadTalkLine);
            }
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
                    if (ImGui::BeginTable("subject_info", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                        ImGui::TableSetupColumn("Value");
                        ImGui::TableHeadersRow();
                        if (bodyFigures)
                        {
                            draw_info_overview_row("Level", sLevel);
                            draw_info_overview_row("HP", int(pools.hp));
                        }
                        else
                        {
                            draw_info_overview_row("Population",
                                                   souls_flock(gs, st, slot));
                            draw_info_overview_row("Wellbeing %",
                                                   wellbeing_pct(st, slot));
                            draw_info_overview_row(
                                "Starved last boundary",
                                int(st.wellbeing[slot].starvedYesterday));
                        }
                        draw_info_overview_row("Faction index", int(sFaction));
                        draw_info_overview_row("Creatures",
                                               creature_count(sInv));
                        draw_info_overview_row("Inventory stacks",
                                               sInv.used_slots());
                        draw_info_overview_row("Inventory items", sInv.total());
                        draw_info_overview_row("Carries (value)",
                                               container_worth(sInv));
                        ImGui::EndTable();
                    }
                    if (traits && traits->count > 0)
                    {
                        ImGui::TextDisabled("Traits:");
                        for (std::uint8_t ti = 0;
                             ti < traits->count && ti < 2; ++ti)
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled(
                                "%s", npc_trait_label(traits->traits[ti]));
                        }
                    }
                    ImGui::Spacing();
                    if ((acts & kMapActTrade) && ImGui::Button("Trade") && tab)
                    {
                        *tab = SettlementPanelTab::Trade;
                    }
                    ImGui::EndTabItem();
                }

                // Trade
                if (acts & kMapActTrade)
                {
                const bool tradeOpen = ImGui::BeginTabItem("Trade", nullptr,
                                                           selected_tab(current, SettlementPanelTab::Trade));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Trade;
                if (tradeOpen)
                {
                    // ONE wrapper (Инк 5): the haggler, the state and the
                    // body come from trade_widgets.h — this site keeps only
                    // its price law and its fact.
                    const PlayerHaggler h = player_haggler(gs, st);
                    // ОБЕ ПОЛОВИНЫ (M-292): две цифры стоят рядом с «Their
                    // value», и смысл у них один — «сколько тут всего добра».
                    ImGui::Text("Player value: %d",
                                container_worth(playerBag));
                    if (!bodyFigures)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("Wellbeing: %d%%",
                                            wellbeing_pct(st, slot));
                    }
                    draw_trade_carry_line(h.sheet, playerBag, h.standing);
                    draw_counterparty_gold(sInv);
                    draw_trade_amount_input(&g_subjectTrade.amount);
                    // ОДИН ЗАКОН ЦЕНЫ НА ОБОИХ. У ветки сквада спрос был
                    // вписан НУЛЁМ (`stock_price(…, 0)`), у ветки места
                    // считался `season_demand_for` — два ответа на «сколько
                    // это здесь стоит». Остался второй: он спрашивает КОЛОНКИ
                    // субъекта (остаток долга нужд, домашние души, руки
                    // анкеты), и у ходока они свои, а не выдуманный ноль.
                    //
                    // АНКЕТА ЭТОГО СКВАДА, А НЕ СТОЛ ЕГО РОДА: таблица рода —
                    // только ШАБЛОН РОЖДЕНИЯ (`birth_place` ставит
                    // `st.sheet[slot] = landmark_sheet(kind)`), после него
                    // истина у анкеты — она качается (ЗАКОН АНКЕТЫ).
                    const auto buyUnit = [&](const ItemRef &ref, int n) {
                        return trade_overlay_buy_price(
                            stock_price(value_of(ref),
                                        sInv.count_of(int(ref.def)) - n,
                                        season_demand_for(
                                            int(ref.def),
                                            st.upkeep[slot].needDebt,
                                            souls_home(st, slot),
                                            sSheet.skills,
                                            &sInv)),
                            h.tradePct,
                            trade_power_of(sSheet));
                    };
                    const auto sellUnit = [&](const ItemRef &ref, int n) {
                        return trade_overlay_sell_price(
                            stock_price(value_of(ref),
                                        sInv.count_of(int(ref.def)) + n,
                                        season_demand_for(
                                            int(ref.def),
                                            st.upkeep[slot].needDebt,
                                            souls_home(st, slot),
                                            sSheet.skills,
                                            &sInv)),
                            h.tradePct,
                            trade_power_of(sSheet));
                    };
                    // ВТОРАЯ ПОЛОВИНА СДЕЛКИ — ДВЕРЬ ПАСТВЫ, И ТА ЖЕ, ЧТО У
                    // НАЙМА (M-292, ЗАКОН КОНТЕЙНЕРА п.3). Контейнер и число
                    // фичи суть ДВА носителя одной души, и писать их порознь
                    // запрещено: `barter_swap` пишет контейнер, значит число
                    // обязано догнать измеренный факт — иначе место
                    // продолжало бы кормить и облагать данью проданного,
                    // ровно тот дефект, который у найма уже чинили.
                    //
                    // Паства мерится ДО тела панели: сделка настаёт внутри
                    // него, в `onDeal`, и спрашивать «сколько было» после неё
                    // уже некого. Дельта ЗНАКОВАЯ — прилавок двусторонен, и
                    // отданная месту голова поднимает его число тем же
                    // измерением (ветки на это нет).
                    //
                    // Для контрагента-ХОДОКА дверь тоже верна: его дом —
                    // ребро Parent (M-245), и душа, проданная из
                    // корована, честно покидает паству того города. Это тот
                    // же адрес, которым ходит смерть
                    // (`kill_fallen_squad_creatures`).
                    const int folkBefore = count_human_souls(sInv);
                    draw_barter_body(
                        "Subject stock", g_subjectTrade,
                        playerBag, sInv,
                        buyUnit, sellUnit, [&](int gave, int took) {
                            flock_left_home(gs, st, slot, folkBefore);
                            record_subject_deal_fact(
                                gs, st, st.spawnId[slot].index, sx, sy,
                                gave, took);
                        });
                    ImGui::EndTabItem();
                }
                }   // acts & kMapActTrade

                // ── ДВЕ ВКЛАДКИ — ДВА ФИЛЬТРА ОДНОГО МАССИВА ─────────────
                // Вердикт владельца 2026-10-20, дословно: «мне важно чтобы
                // технически это было единая система но для интерфейсов и
                // взаиодействий — например загрузка гарнизона и тд или
                // тороговля — можно разбивать но чтобы было возможно всё всё
                // равно … можно разные двери но на верхних уровнях уже».
                //
                // Значит разбиение законно ТОЛЬКО как вид: обе вкладки идут по
                // ТЕМ ЖЕ 1024 слотам (`Inventory::slots`), различаются одним
                // предикатом рода строки и берут имя ОДНОЙ дверью
                // (`world_row_label@src/macro/world_row.h`). Второго хранилища
                // нет ни у одной, сословий внутри контейнера нет (ЗАКОН
                // КОНТЕЙНЕРА), и `?` на существе стал невыразим.
                //
                // Прежде `Garrison` обходил не слоты, а ПЕРЕЧЕНЬ РОДОВ, считая
                // каждый сканом контейнера (`creature_count_of` × число родов,
                // то есть родов × 1024 посещения слота на кадр); теперь это
                // один проход по 1024. Строки с нулём у хайрабл-родов ушли —
                // «кого можно нанять» отвечает вкладка Recruit, и это её
                // вопрос, а не ростера.
                const bool garrisonOpen = ImGui::BeginTabItem("Garrison", nullptr,
                                                              selected_tab(current, SettlementPanelTab::Garrison));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Garrison;
                if (garrisonOpen)
                {
                    const int units = creature_count(sInv);
                    ImGui::Text("Total: %d units", units);
                    ImGui::Spacing();
                    if (ImGui::BeginTable("garrison", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        for (const ItemRef &it : sInv.slots)
                        {
                            if (it.empty() || !world_row_is_creature(it.def))
                                continue;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", world_row_label(it.def));
                            ImGui::TableNextColumn();
                            ImGui::Text("x%d", it.count);
                        }
                        ImGui::EndTable();
                    }
                    if (units == 0)
                        ImGui::TextDisabled("(no creatures)");
                    ImGui::EndTabItem();
                }

                // Recruit — the Hire verb of the actions column.
                if (acts & kMapActHire)
                {
                const bool recruitOpen = ImGui::BeginTabItem("Recruit", nullptr,
                                                             selected_tab(current, SettlementPanelTab::Recruit));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Recruit;
                if (recruitOpen)
                {
                    // ЗДЕСЬ ИМЕННО ПЛАТЁЖНОЕ ЧИСЛО, И НАЗВАНО ОНО ТАК ЖЕ
                    // (M-292): оно ГЕЙТИТ кнопку ниже и уходит в `hire_npc`
                    // кошельком, поэтому показать здесь полную стоимость
                    // контейнера значило бы включить «Hire» на непокрытую
                    // сделку — существа в сумке игрока не монета.
                    ImGui::Text("Payable: %d", inventory_value(playerBag));
                    ImGui::Spacing();
                    for (int ti = 0; ti < npc_type_count(); ++ti)
                    {
                        const NPCType t = npc_type_at(ti);
                        if (!npc_hireable(t))
                            continue;
                        const ItemRef* offer = first_soldier_of_kind(sInv, t);
                        int cost = offer
                            ? hire_price_for(std::uint16_t(t), offer->level)
                            : npc_hire_price_base(t);
                        int avail = creature_count_of(sInv, t);
                        Inventory* playerArmy = &playerBag;
                        int owned = creature_count_of(playerBag, t);
                        ImGui::PushID(static_cast<int>(t));
                        bool can = avail > 0
                                   && inventory_value(playerBag) >= cost;
                        if (!can)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Hire"))
                        {
                            int purse = inventory_value(playerBag);
                            // Паства мерится ДО сделки: нанятый уходит в
                            // армию игрока НАВСЕГДА (у его сквада свой дом),
                            // то есть покидает паству этого места — ровно
                            // случай, для которого `leave_home_flock` и
                            // написан. Откат неудачной сделки ниже возвращает
                            // голову в состав, и замер до/после это учитывает.
                            const int folkBefore = count_human_souls(sInv);
                            const int paid = playerArmy
                                ? hire_npc(*playerArmy, sInv, t, purse)
                                : 0;
                            if (paid > 0)
                            {
                                // The fee is REAL coin into the subject's own
                                // store — the treasury IS the inventory
                                // (CANON S10), not a sink. A store that cannot
                                // take the whole fee refuses the deal: coin
                                // back, recruit back.
                                const int moved = transfer_value_dense(
                                    playerBag, sInv, paid);
                                if (moved != paid)
                                {
                                    if (moved > 0)
                                        transfer_value_dense(sInv,
                                                             playerBag, moved);
                                    SoldierRecord back{};
                                    if (creatures_pop_back(*playerArmy, back))
                                        creatures_push(sInv, back);
                                }
                            }
                            flock_left_home(gs, st, slot, folkBefore);
                        }
                        if (!can)
                            ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::Text("%-10s  cost %d g   avail %d   you have %d",
                                    npc_def(t).label, cost, avail, owned);
                        ImGui::PopID();
                    }
                    ImGui::Spacing();
                    ImGui::TextDisabled(
                        "Season upkeep: %d g",
                        calculate_squad_upkeep(playerBag) * kDaysPerSeason);
                    ImGui::EndTabItem();
                }
                }   // acts & kMapActHire

                // Inventory — ТОТ ЖЕ массив, обратный фильтр (см. Garrison).
                const bool inventoryOpen = ImGui::BeginTabItem("Inventory", nullptr,
                                                               selected_tab(current, SettlementPanelTab::Inventory));
                if (tab && ImGui::IsItemClicked())
                    *tab = SettlementPanelTab::Inventory;
                if (inventoryOpen)
                {
                    // «Пусто» — про ЭТОТ вид, а не про контейнер: склад из
                    // одних существ не пуст, но предметов в нём нет, и
                    // `used_slots() == 0` соврал бы в обе стороны.
                    int shown = 0;
                    if (ImGui::BeginTable("inv", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
                    {
                        for (const ItemRef &it : sInv.slots)
                        {
                            if (it.empty() || !world_row_is_item(it.def))
                                continue;
                            ++shown;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", world_row_label(it.def));
                            ImGui::TableNextColumn();
                            ImGui::Text("x%d", it.count);
                        }
                        ImGui::EndTable();
                    }
                    if (shown == 0)
                        ImGui::TextDisabled("(no items)");
                    ImGui::EndTabItem();
                }
                // (The History tab — a per-settlement population plot — died
                // 2026-09-18 with SettlementHistory itself, owner verdict №4:
                // one memory of a place is the chronicle, CANON S20.1.)
                // The inn is gone (owner, 2026-09-11: «отдых таверны
                // вырезать вообще»): a paid full restore beside the ONE
                // macro rest law was a second, cheaper law of recovery.
                // Quests — the contract board (actions column: Quests).
                if (acts & kMapActQuests)
                {
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
                        const bool known = questEngine.is_known(activeQuests, gs.player, q);
                        // Offers carry no ordinal yet — their provenance slot
                        // is the unique per-row key (one offer per slot/day).
                        ImGui::PushID(int(q.offerSlot));
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
                                questEngine.accept(activeQuests, q, gs, bus);
                            }
                            if (known)
                                ImGui::EndDisabled();
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                }   // acts & kMapActQuests
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
                        float scale,
                        const char *closeKeyName)
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
            // The button quotes the LIVE Quests binding, never a literal —
            // the same honesty rule as the toolbar tooltips (CANON S22).
            char closeLabel[32] = "Close";
            if (closeKeyName && closeKeyName[0])
                std::snprintf(closeLabel, sizeof(closeLabel), "Close [%s]",
                              closeKeyName);
            if (ImGui::Button(closeLabel))
            {
                *open = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Active: %zu  Done: %u  Failed: %u",
                                quests.size(), gs.player.completedQuestCount,
                                gs.player.failedQuestCount);
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
                    ImGui::PushID(int(q.ordinal));
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
            questEngine.abandon(
                quests, quests[std::size_t(abandonIndex)].ordinal, bus);
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
            ImGui::TextDisabled("Unlocked entries: %d",
                                std::popcount(gs.player.codexUnlockedBits));
            ImGui::Separator();

            ImGui::BeginChild("##codex_sidebar", ImVec2(250.0f, 0.0f), true);
            for (int ci = 0; ci < int(CodexCategoryId::Count); ++ci)
            {
                const CodexCategoryRow &cat =
                    kCodexCategories[std::size_t(ci)];
                const int firstArticle =
                    first_unlocked_article_in(gs.player, cat.id);
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
                    for (int ai = 0; ai < int(kCodexArticleCount); ++ai)
                    {
                        const CodexArticleRow &entry =
                            kCodexArticles[std::size_t(ai)];
                        if (entry.category != cat.id)
                            continue;
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
            if (selectedArticle >= 0 &&
                selectedArticle < int(kCodexArticleCount))
            {
                const CodexArticleRow &entry =
                    kCodexArticles[std::size_t(selectedArticle)];
                ImGui::TextColored(ImVec4(0.95f, 0.36f, 0.24f, 1.0f),
                                   "%s", entry.title);
                ImGui::TextDisabled(
                    "%s", kCodexCategories[std::size_t(entry.category)].title);
                ImGui::Separator();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(entry.body);
                ImGui::PopTextWrapPos();
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
                          const Inventory *bag,
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
                        if (goldCost > (bag ? inventory_value(*bag) : 0))
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
        if (story.slideCount == 0 || !story.slides)
        {
            complete_story(state, bus);
            return;
        }
        if (state.slideIndex >= story.slideCount)
            state.slideIndex = story.slideCount - 1;

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

        const content::StorySlide &slide = story.slides[state.slideIndex];
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", story.id);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu / %zu", state.slideIndex + 1, story.slideCount);
        ImGui::Separator();
        if (const UiImage *image = ui_image_for(slide.image))
        {
            const float imageW = ImGui::GetContentRegionAvail().x;
            draw_ui_image(*image, imageW, 360.0f, true);
            ImGui::Spacing();
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", slide.narration ? slide.narration : "");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const bool lastSlide = state.slideIndex + 1 >= story.slideCount;
        if (ImGui::Button(lastSlide ? "Begin" : "Continue", ImVec2(-FLT_MIN, 0.0f)) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            if (!lastSlide)
                ++state.slideIndex;
            else
                complete_story(state, bus);
        }
        ImGui::EndPopup();
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
            // Fraction of TILE_TREE_DECOR in this pixel's tile footprint.
            // Drives a continuous forest tint — trees are ~2% of tiles even
            // in a dense forest, so a discrete "majority tile" can never
            // show them; the fraction can.
            float treeFrac = 0.0f;
        };

        // Ground colour per biome for the 2D subworld maps — the map-palette
        // echo of mesh.frag's materialBase (same hues, map brightness). The
        // plain TILE_GRASS colour used to be ONE green for every biome, so
        // desert, taiga and snow all read as meadow on the map while the 3D
        // ground showed their real materials.
        ImU32 subworld_ground_color(sm::Biome b)
        {
            switch (b)
            {
            case sm::Tundra:   return IM_COL32(128, 133, 115, 255);
            case sm::Taiga:    return IM_COL32(64, 105, 78, 255);
            case sm::Snow:     return IM_COL32(205, 212, 208, 255);
            case sm::Valley:   return IM_COL32(140, 133, 82, 255);
            case sm::Swamp:    return IM_COL32(61, 92, 51, 255);
            case sm::Desert:   return IM_COL32(209, 184, 122, 255);
            case sm::Steppe:   return IM_COL32(173, 153, 82, 255);
            case sm::Tropics:  return IM_COL32(52, 108, 52, 255);
            case sm::Mountain: return IM_COL32(115, 110, 99, 255);
            case sm::Meadow:
            default:           return IM_COL32(90, 140, 70, 255);
            }
        }

        // (subworld_tile_color — THE tile → colour dictionary — is defined
        // once, beside the settlement preview above.)

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

            // BOTH map scales count the pixel's full tile footprint: the old
            // minimap path sampled only the centre tile, so whether a tree
            // (or road, or shore) showed was a per-pixel lottery — the owner
            // report "unclear what criteria draw trees on the minimap".
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
                out.treeFrac = float(counts[sub::TILE_TREE_DECOR])
                             / float(countTotal);
            }
            (void)detail;
            return out;
        }

        float sub_map_height_at(const std::vector<float> &heights, int x, int y)
        {
            const int src = sub::kFullSize;
            x = std::clamp(x, 0, src - 1);
            y = std::clamp(y, 0, src - 1);
            return heights[std::size_t(y) * src + x];
        }

        // `seaLevel` — ПЛОСКОСТЬ ТОГО МИРА, который рисуется, а не дефолтная.
        // Здесь стоял `constexpr kWaterLevel = WATER_LEVEL`, и на мире с
        // уровнем моря выше дефолта вся шкала глубины и высоты считалась от
        // чужой воды: дно красилось как отмель, суша — как затопленная.
        float sub_map_shade(const std::vector<float> &heights,
                            const SubMapSample &s,
                            bool detail,
                            float seaLevel)
        {
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
                const float depth = std::clamp((seaLevel - h) / seaLevel,
                                               0.0f, 1.0f);
                return std::clamp(0.96f - depth * 0.28f + (light - 1.0f) * 0.10f,
                                  0.62f, 1.06f);
            }

            const float elev = std::clamp((h - seaLevel) /
                                          (1.0f - seaLevel),
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
                case sub::Structure::Wattle:
                case sub::Structure::Palisade:
                    // A village's own wall: the same rampart mark as masonry,
                    // in timber's colour — from the air a stockade reads as a
                    // ring, which is the one thing the minimap owes it.
                    draw_map_disc(rgba, side, px, py,
                                  std::max(1, r), IM_COL32(118, 86, 52, 255), 0.95f);
                    break;
                case sub::Structure::Fence:
                    // Knee-high field wall: same stone family as Wall, drawn
                    // fainter so furlong grids read as texture, not ramparts.
                    draw_map_disc(rgba, side, px, py,
                                  std::max(1, r), IM_COL32(112, 108, 100, 255),
                                  0.60f);
                    break;
                case sub::Structure::Bridge:
                    // Stone masonry now (deck chords + piers), so the map
                    // paints it in the Wall's grey family, a shade lighter —
                    // a bridge should read as a crossing, not a rampart.
                    draw_map_rect(rgba, side, px, py,
                                  std::max(1, r), 1,
                                  IM_COL32(148, 148, 154, 255), 0.92f);
                    break;
                case sub::Structure::Rock:
                    draw_map_disc(rgba, side, px, py,
                                  std::max(1, r), IM_COL32(112, 112, 116, 255), 0.70f);
                    break;
                case sub::Structure::Door:
                case sub::Structure::SpireGate:
                case sub::Structure::SpireOrb:
                // The kerb is the bridge's own parapet rail — the deck above
                // already paints the crossing; a second mark would be noise.
                case sub::Structure::Kerb:
                case sub::Structure::Lantern:
                case sub::Structure::Hatch:
                case sub::Structure::Ladder:
                case sub::Structure::SpireHatch:
                case sub::Structure::Chest:
                case sub::Structure::CaveMouth:
                case sub::Structure::Well:
                case sub::Structure::Sign:
                case sub::Structure::Tree:
                case sub::Structure::Crop:
                case sub::Structure::Furnish:
                    // Everything here reads from the tile layer already
                    // (TREE_DECOR / TILE_FIELD / the painted footprints), and
                    // a town holds HUNDREDS of doors and lanterns: marking
                    // them turned the minimap into one yellow blanket. A dot
                    // per prop is only worth it when props are rare.
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
            // Плоскость окна — через ту же дверь, которой субмир её и получил
            // (CellContext::seaLevel); окно ОДНО, значит и плоскость одна.
            const float seaLevel =
                mgr.resolve_cell(mgr.center_cx(), mgr.center_cy()).seaLevel;
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
                    ImU32 base = subworld_tile_color(s.tile);
                    // Ground pixels take their colour from the SAME dithered
                    // 3×3 biome pick the 3D ground material uses — the map
                    // shows the exact world the renderer draws, transitions
                    // included (sub/material.h pick_ground_biome).
                    if (s.tile == sub::TILE_GRASS)
                    {
                        const int ccx = std::min(2, s.sx / sub::kCellSize);
                        const int ccy = std::min(2, s.sy / sub::kCellSize);
                        const int lx = s.sx - ccx * sub::kCellSize;
                        const int ly = s.sy - ccy * sub::kCellSize;
                        const long long ax0 =
                            (long long)(mgr.center_cx() - 1 + ccx)
                            * sub::kCellSize;
                        const long long ay0 =
                            (long long)(mgr.center_cy() - 1 + ccy)
                            * sub::kCellSize;
                        sm::Biome gb = sub::pick_ground_biome(
                            mgr.cell_ground_ring(ccy * 3 + ccx), lx, ly,
                            sub::kCellSize, ax0, ay0);
                        gb = sub::apply_mountain_treeline(
                            gb, s.height, ax0 + lx, ay0 + ly);
                        base = subworld_ground_color(gb);
                    }
                    // Continuous forest tint: blend toward the tree colour by
                    // the pixel's REAL tree fraction (full forest ≈ 2% of
                    // tiles ⇒ scale so it reads saturated). Density-honest —
                    // an опушка gradient shades in gradually, a lone tree is
                    // a faint fleck, and every mark corresponds to real
                    // Structure::Tree records (phantom decor is gone).
                    if (s.treeFrac > 0.0f && s.tile != sub::TILE_TREE_DECOR)
                    {
                        const float a =
                            std::min(1.0f, s.treeFrac / 0.02f) * 0.85f;
                        const ImU32 tc =
                            subworld_tile_color(sub::TILE_TREE_DECOR);
                        const auto mixc = [&](int shift) {
                            const float b0 = float((base >> shift) & 0xFF);
                            const float t0 = float((tc >> shift) & 0xFF);
                            return std::uint32_t(b0 + (t0 - b0) * a) & 0xFF;
                        };
                        base = IM_COL32(mixc(IM_COL32_R_SHIFT),
                                        mixc(IM_COL32_G_SHIFT),
                                        mixc(IM_COL32_B_SHIFT), 255);
                    }
                    const float shade =
                        sub_map_shade(heights, s, detail, seaLevel);
                    write_map_pixel(rgba, side, x, y, base, shade);
                }
            }
            if (detail)
            {
                overlay_subworld_roads(rgba, side, mgr);
                overlay_subworld_structures(rgba, side, mgr);
            }
            // In-place refresh (ui_gpu recreate): the map's dimensions never
            // change, so the periodic rebuild is one staged copy — not the
            // destroy/allocate/register cycle that stalled the frame every
            // two seconds (problems.md §20).
            mm.tex = recreate_ui_texture(mm.tex, side, side, rgba.data(),
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
