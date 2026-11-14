// ИИ-МОДУЛЬ СКВАДА РОДА Nomad — бродяга без дома: ходит от города к городу,
// не возвращаясь в только что покинутый. Строка `kAiModuleRows`; окно —
// `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <cstdint>

#include "core/subtract.h"   // sub_sat — угасание счёта ожидания

namespace sm {

namespace {

// ═════ МОДУЛЬ Nomad — БРОДЯГА БЕЗ ДОМА ════════════════════════════════════
// Дома нет — значит некуда возить: это не «фолбэк на всякий случай», а
// законный ответ мира. Бродяга ходит к городу мира (город или столица), стоит
// там и выбирает следующий, только не тот, откуда пришёл.
//
// СВОЙ ТИП СО СВОИМ МОДУЛЕМ (ряд M-499, раскладка (з), одобрена владельцем
// 2026-11-13; CANON «РАЗНООБРАЗИЕ ИГРЫ ЕСТЬ НОВЫЕ ТИПЫ СО СВОИМИ МОДУЛЯМИ»).
// Прежде это была думка `ai_nomad` в общем `npc_ai.cpp`, и звали её два
// чужих модуля глаголом пульта — общая думка под двумя модулями. Теперь
// корован и сборщик, оставшиеся без дома, САМИ меняют свой тип на этот —
// каждый своим кодом (`caravan.cpp`, `collector.cpp`), с зануленной памятью
// и брошенной целью. Назад бродяга не превращается ни в кого: дома нет.
//
// ЖДАТЬ МОДУЛЬ ВПРАВЕ ТОЛЬКО СВОЕЙ ПАМЯТЬЮ. Слово `kNomadMemWait` — сколько
// думок осталось до следующего выбора города; оно тает на думку, и думка, на
// которой оно дотаяло, решает. Истина — «ещё жду». (Прежде обе паузы —
// «постоял по прибытии» и «идти некуда» — писались в гейт восстановления, а
// заряжает его одна способность: вердикт владельца 2026-11-13, CANON S25
// «ВЗАИМОДЕЙСТВИЕ — НЕ СПОСОБНОСТЬ». Длина в думках та же: гейт таял на
// думку тем же квантом `kAiTicks`.)
bool nomad_waits_(ecs::MacroNpcRuntime& rt) {
    std::uint32_t& left = rt.mem[kNomadMemWait];
    left = sub_sat(left, std::uint32_t(1));   // угасание счёта ожидания
    return left != 0;
}

void nomad_think(AiSelf& s, const AiView& view, AiPult& pult) {
    ecs::SquadGoal& g = s.goal;
    auto& rt = s.rt;
    // Traveling: в пути решать нечего — везут ноги свипа, они же сделают
    // бродягу Idle по прибытии.
    if (rt.state != std::uint8_t(NS::Idle)) return;
    // «Не тот город, где был» — прежний ординал несёт сама цель (род Squad).
    // Ординал чужой погони городом не окажется: пространство ординалов одно,
    // и вопрос вида сверяет его с ординалами ГОРОДОВ. Ноль — «никто».
    const int lastRooted = ecs::goal_kind(g.what) == ecs::GoalKind::Squad
                              ? int(ecs::goal_ordinal(g.what))
                              : 0;
    // Дошёл до города — постоит (прибытие назвали ноги). Число думок —
    // баланс, не вывод; взято у снятой паузы гейтом той же длины.
    if (ecs::march_arrived(rt)) {
        rt.mem[kNomadMemWait] = std::uint32_t(10 + pult.rand(15));
        return;
    }
    // Слово тает на КАЖДОЙ думке без дела, и когда гейт восстановления
    // занят чужой дверью (ступень угрозы после автобоя), — так стоял и
    // прежний гейт: таял временем, что бы ни держало сквад.
    const bool waiting = nomad_waits_(rt);
    // Свой акт бродяга не делает, и гейт восстановления заряжают ему чужие
    // двери; пока он не пуст, бродяга стоит.
    if (waiting || squad_recovering(rt)) return;
    const int candidates = view.cities_except(lastRooted);
    if (candidates == 0) {
        // Идти некуда — переждать своим словом. Баланс, как и выше.
        rt.mem[kNomadMemWait] = 10;
        return;
    }
    const std::uint16_t city =
        view.city_except(lastRooted, pult.rand(candidates));
    if (city == kMacroNoSlot) return;
    ecs::goal_set(g, view.rooted_cell(city),
                  ecs::goal_what(ecs::GoalKind::Squad, view.rooted_id(city)),
                  0, std::uint8_t(NS::Idle));
    rt.state = std::uint8_t(NS::Traveling);
}

}  // namespace

// Переходник пятёрки — тот же, что у рейса сбыта (копия законна, AGENTS §4
// п.5): собирает СВОЮ СТРОКУ, ВИД и ПУЛЬТ. Без мира думка не идёт.
void ai_nomad_squad(MacroHandle e, ecs::MacroNpcRuntime& rt,
                    ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    // Преамбула рефлексов — та же и в том же порядке (M-307, см. ai_artel):
    // бродяга, бывший корованом или сборщиком, нюхал и пугался ими же.
    const ecs::NPCKind& kind = store_ctx(ctx).kind[e.slot];
    scent_squad_deposit(e, kind, ctx);
    if (squad_threat_step(e, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(e, kind, rt, pools, ctx)) return;
    AiSelf self{e, store_ctx(ctx).cell[e.slot], rt, pools,
                store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
    const AiView view(ctx);
    AiPult pult(ctx);
    nomad_think(self, view, pult);
}

}  // namespace sm
