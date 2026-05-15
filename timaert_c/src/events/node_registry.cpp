#include "events/node_registry.h"
#include "content/plot/encounters.h"
#include "core/rng.h"
#include "events/event_types.h"
#include "events/logic_nodes.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace sm {

namespace {

struct RandomEncounterState {
    Rng rng{0xEC0A57u};
    float steps = 0.0f;
};

void add_continue_choice(GameEvent& dialog, const char* label) {
    dialog.dialogChoices = std::make_shared<std::vector<DialogChoicePayload>>();
    dialog.dialogChoices->push_back(DialogChoicePayload{label ? label : "Continue", {}, {}});
    dialog.ix = static_cast<int>(dialog.dialogChoices->size());
}

const GameEvent* find_settlement_enter_event(const EventBus& bus) {
    for (const auto& ev : bus.last_tick_events()) {
        if (ev.tag == EventTag::SettlementVisit
            || ev.tag == EventTag::PlayerEnterSettlement) {
            return &ev;
        }
    }
    return nullptr;
}

float player_move_steps(const GameEvent& ev) {
    if (ev.a > 0u) {
        return static_cast<float>(ev.a) / 1000.0f;
    }
    if (ev.fx != 0.0f || ev.fy != 0.0f) {
        const float dx = static_cast<float>(ev.ix) - ev.fx;
        const float dy = static_cast<float>(ev.iy) - ev.fy;
        return std::sqrt(dx * dx + dy * dy);
    }
    return 0.0f;
}

LogicNode random_encounter_node() {
    auto state = std::make_shared<RandomEncounterState>();
    LogicNode n;
    n.id = "enc_random";
    n.label = "Random Encounter";
    ConditionSlot c;
    c.isEvent = true;
    c.tag = EventTag::PlayerMove;
    c.predicate = [state](const GameEvent& ev) {
        state->steps += std::max(0.0f, player_move_steps(ev));
        if (state->steps < 15.0f) return false;
        const float chance = 0.01f + std::min((state->steps - 15.0f) * 0.001f, 0.11f);
        if (state->rng.next_f01() >= chance) return false;
        state->steps = 0.0f;
        return true;
    };
    n.conditions.push_back(std::move(c));
    n.mask.push_back(1);
    n.next.push_back(n.id);
    n.tags.push_back("encounter");
    n.effect = [state](NodeContext& ctx) {
        const auto& table = content::encounters();
        if (table.empty()) return;
        const auto& def = table[std::size_t(state->rng.next_u32() % std::uint32_t(table.size()))];

        GameEvent dialog{EventTag::ShowDialog};
        dialog.s1 = def.title;
        dialog.s2 = def.body;
        dialog.dialogChoices = std::make_shared<std::vector<DialogChoicePayload>>();
        dialog.dialogChoices->reserve(def.choices.size());
        for (const auto& choice : def.choices) {
            DialogChoicePayload payload{};
            payload.label = choice.label;
            payload.effects = choice.effects;
            dialog.dialogChoices->push_back(std::move(payload));
        }
        dialog.ix = static_cast<int>(dialog.dialogChoices->size());
        ctx.bus->emit(dialog);
    };
    return n;
}

LogicNode level_up_dialog_node() {
    LogicNode n;
    n.id = "sys_level_up";
    n.label = "Level Up";
    ConditionSlot c;
    c.isEvent = true;
    c.tag = EventTag::PlayerLevelUp;
    n.conditions.push_back(std::move(c));
    n.mask.push_back(1);
    n.next.push_back(n.id);
    n.tags.push_back("system");
    n.effect = [](NodeContext& ctx) {
        int level = 0;
        for (const auto& ev : ctx.bus->last_tick_events()) {
            if (ev.tag == EventTag::PlayerLevelUp) {
                level = ev.ix;
                break;
            }
        }

        GameEvent dialog{EventTag::ShowDialog};
        dialog.s1 = "Level Up!";
        dialog.s2 = "You have reached level " + std::to_string(level)
                  + "! Your abilities grow stronger.";
        add_continue_choice(dialog, "Continue");
        ctx.bus->emit(dialog);
    };
    return n;
}

LogicNode settlement_dialog_node() {
    LogicNode n;
    n.id = "sys_settlement";
    n.label = "Settlement Greeting";
    ConditionSlot c;
    c.isEvent = false;
    c.check = [](const EventBus& bus, const PlayerState&) {
        return find_settlement_enter_event(bus) != nullptr;
    };
    n.conditions.push_back(std::move(c));
    n.mask.push_back(1);
    n.next.push_back(n.id);
    n.tags.push_back("system");
    n.effect = [](NodeContext& ctx) {
        std::string name = "Unknown";
        if (const GameEvent* ev = find_settlement_enter_event(*ctx.bus)) {
            if (!ev->s1.empty()) name = ev->s1;
        }

        GameEvent dialog{EventTag::ShowDialog};
        dialog.s1 = "Welcome to " + name;
        dialog.s2 = "The gates open before you. Merchants hawk their wares and guards patrol the walls.";
        add_continue_choice(dialog, "Enter");
        ctx.bus->emit(dialog);
    };
    return n;
}

} // namespace

void register_builtin_nodes(LogicNodeEngine& logic) {
    logic.add(random_encounter_node());
    logic.add(level_up_dialog_node());
    logic.add(settlement_dialog_node());
    logic.activate("enc_random");
    logic.activate("sys_level_up");
    logic.activate("sys_settlement");
}

} // namespace sm
