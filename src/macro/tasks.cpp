// Конструктор задач — тела (шапка закона в tasks.h). Сканы здесь
// кап-зависимые, поэтому живут в .cpp (§5 п.13), и все они — путь
// ОТКРЫТИЯ ДОСКИ или КЛИКА, не тика: цена платится взглядом игрока.
#include "macro/tasks.h"

#include <cstring>
#include <limits>
#include <span>

#include "core/rng.h"
#include "core/torus.h"
#include "macro/chronicle.h"
#include "macro/deal.h"     // side_row — нужда места глазами сделки
#include "macro/econ_day.h"
#include "macro/economy.h"  // RowSide
#include "macro/factions.h"
#include "macro/features.h"
#include "macro/squad.h"
#include "macro/squad_index.h"
#include "macro/state.h"
#include "bus/store.h"
#include "tables/commodity.h"
#include "tables/items.h"
#include "bus/squad_row.h"   // колонки сквада — род 2 фрейма (ломоть 2б-I)

namespace sm {
namespace {

// Данные конструктора — у конструктора (ЗАКОН АГНОСТИЧНОСТИ: слой фич о
// задачах не знает; какие фичи «достопримечательность» — число редактора).
constexpr FeatureType kVisitWorthy[] = {FT_Ruin, FT_Spire};
// Радиусы — баланс (дневной марш округи), не по-двойка намеренно.
constexpr int kVisitScanRadius = 24;
constexpr int kKillScanRadius = 18;
// Сколько офферов кладёт один глагол — баланс доски.
constexpr int kOffersPerVerb[] = {2, 1, 1};
static_assert(std::size(kOffersPerVerb) == std::size(kTaskVerbs),
              "каждому глаголу — его счёт на доске");

// Обход цепочки сквада целиком: без горизонта по дням и без предела по числу.
// Предел обхода — сама цепочка, а она не длиннее лога.
constexpr std::int32_t kAllDays = std::numeric_limits<std::int32_t>::min();
constexpr int kWholeChain = std::numeric_limits<int>::max();

struct ContractScan {
    std::uint32_t playerId = 0;
    std::uint32_t placeId = 0;
    // Порядок внутри дня несёт seq, не день: взятие и сдача одного дня
    // днями неразличимы (поймано свидетелем — повторное взятие в день сдачи
    // читалось «уже сдан»). 0 = не видел (seq летописи начинается с 1).
    std::uint32_t takenSeq = 0;
    std::uint32_t doneSeq = 0;
    std::int32_t takenDay = 0;
    std::int32_t packed = 0;
    // Новейшее взятие этого сквада ГДЕ УГОДНО, не только у этого места.
    // Задача есть ОДНА цель (вердикт №2): взял контракт в другом городе —
    // прежний брошен, и говорит это ЛЕТОПИСЬ, а не тег цели.
    std::uint32_t newestTakenSeq = 0;
};

// Новейший QuestTaken против новейшего QuestDone этой пары (игрок, место).
// Цепочка по субъекту ходит НОВЕЙШИМИ ВПЕРЁД, поэтому первый встреченный род —
// его новейший экземпляр.
void scan_contract(void* user, const WorldFact& f) {
    auto& s = *static_cast<ContractScan*>(user);
    if (f.subjectKind != std::uint8_t(FactSubject::Squad)
        || f.subject != s.playerId) {
        return;
    }
    if (f.kind == std::uint16_t(FactKind::QuestTaken)
        && s.newestTakenSeq == 0u) {
        s.newestTakenSeq = f.seq;
    }
    if (f.objectKind != std::uint8_t(FactSubject::Rooted)
        || f.object != s.placeId) {
        return;
    }
    if (f.kind == std::uint16_t(FactKind::QuestTaken) && s.takenSeq == 0u) {
        s.takenSeq = f.seq;
        s.takenDay = f.day;
        s.packed = f.amount;
    }
    if (f.kind == std::uint16_t(FactKind::QuestDone) && s.doneSeq == 0u) {
        s.doneSeq = f.seq;
    }
}

struct KillScan {
    std::uint32_t playerId = 0;
    std::uint32_t targetId = 0;
    int count = 0;
};

void scan_kills(void* user, const WorldFact& f) {
    auto& s = *static_cast<KillScan*>(user);
    if (f.kind == std::uint16_t(FactKind::Killed)
        && f.subjectKind == std::uint8_t(FactSubject::Squad)
        && f.subject == s.playerId
        && f.objectKind == std::uint8_t(FactSubject::Squad)
        && f.object == s.targetId) {
        ++s.count;
    }
}

struct VisitScan {
    std::uint32_t playerId = 0;
    std::int16_t x = 0, y = 0;
    bool seen = false;
};

void scan_visits(void* user, const WorldFact& f) {
    auto& s = *static_cast<VisitScan*>(user);
    if (f.kind == std::uint16_t(FactKind::Explored)
        && f.subjectKind == std::uint8_t(FactSubject::Squad)
        && f.subject == s.playerId && f.x == s.x && f.y == s.y) {
        s.seen = true;
    }
}

// Опыт задачи — ЧИСТАЯ СТОИМОСТЬ (вердикт владельца 2026-11-02: «НИКОГДА
// МОНЕТЫ… агностичная система чистой стоимости в таблице… + опыт анкете +
// слава»): fetch платит стоимостью доставленного (колонка value строки,
// та же, какой платит весь мир), прочие — базой своей строки глагола.
std::uint32_t task_xp(std::uint8_t verb, std::uint16_t object,
                      std::uint16_t amount) {
    const TaskVerbDef& v = kTaskVerbs[verb];
    if (v.object == TaskObjectSpace::Need) {
        const ItemDef* d = item_def_at(int(object));
        const int value = d ? d->value : 0;
        return std::uint32_t(value) * std::uint32_t(amount);
    }
    return v.baseXp;
}

}  // namespace

int task_offers_for(const MacroStore& st, const GameState& gs,
                    const Session& ses,
                    const SquadIndex& idx,
                    MacroHandle place, TaskOffer* out, int cap) {
    const FeatureLayer& features = gs.features;
    if (!st.valid(place) || out == nullptr || cap <= 0) return 0;
    const std::uint16_t slot = place.slot;
    const std::uint32_t placeId = st.spawnId[slot].index;
    if (placeId == 0u) return 0;
    const int W = gs.mapW;
    const std::uint32_t here = st.cell[slot].idx;
    const int px = ecs::cell_x(st.cell[slot], W);
    const int py = ecs::cell_y(st.cell[slot], W);

    // Та же доска в тот же день у того же места: сид — мир × день × место.
    Rng rng(hash3(placeId, std::uint32_t(gs.worldTime.day()), gs.worldSeed));

    int n = 0;

    // ── ПРИНЕСИ: чего месту не хватает — сколько не оплачено по его счёту
    // сверх полки. Ответ тот же, каким нужду места читает сделка
    // (`side_row@src/macro/deal.h`: запас минус счёт); строку без счёта
    // (сырьё) доска не просит.
    {
        struct Need {
            std::uint16_t item;
            int lack;
        };
        Need needs[kCommodityCount];
        int needCount = 0;
        for (int c = 0; c < int(kCommodityCount); ++c) {
            const int item = commodity_item_index(c);
            if (item < 0 || item > 0xFFF) continue;  // упаковка 12 бит
            const ItemDef* d = item_def_at(item);
            if (!d || d->value <= 0) continue;
            const RowSide row = side_row(st, slot, item, d->value);
            const std::int64_t lack = row.net < 0 ? -row.net : 0;
            if (lack <= 0) continue;
            needs[needCount++] = {std::uint16_t(item),
                                  lack > 0xFFFF ? 0xFFFF : int(lack)};
        }
        for (int k = 0; k < kOffersPerVerb[0] && needCount > 0 && n < cap;
             ++k) {
            const int pick = rng.next_int(0, needCount);
            const Need nd = needs[pick];
            needs[pick] = needs[--needCount];  // без повтора той же строки
            out[n++] = TaskOffer{
                .verb = 0,
                .object = nd.item,
                .target = placeId,
                .cell = st.cell[slot].idx,
                .amount = std::uint16_t(nd.lack),
                .xp = task_xp(0, nd.item, std::uint16_t(nd.lack)),
            };
        }
    }

    // ── УБЕЙ: сквад, враждебный месту, из округи — КАРКАСОМ (ЗАКОН
    // КЛЕТОЧНОГО КАРКАСА), враждебность — матрицей фракций.
    {
        const MacroHandle flag = player_flag_handle(ses);
        const FactionSlot home = FactionSlot(st.kind[slot].factionIdx);
        struct Prey {
            std::uint32_t id;
            std::uint32_t cell;
        };
        Prey prey[16];
        int preyCount = 0;
        squads_in_frame(idx, st, px, py, kKillScanRadius,
                        [&](std::uint16_t s, int, int) {
                            if (preyCount >= 16) return;
                            if (s == slot) return;
                            // Невалидный флаг (мир без игрока) не смеет
                            // исключить чей-то законный слот.
                            if (st.valid(flag) && s == flag.slot) return;
                            if (st.dead[s] != 0) return;
                            const std::uint32_t id = st.spawnId[s].index;
                            if (id == 0u) return;
                            if (!factions_hostile(
                                    gs.factions, home,
                                    FactionSlot(st.kind[s].factionIdx))) {
                                return;
                            }
                            prey[preyCount++] = {id, st.cell[s].idx};
                        });
        for (int k = 0; k < kOffersPerVerb[1] && preyCount > 0 && n < cap;
             ++k) {
            const int pick = rng.next_int(0, preyCount);
            const Prey p = prey[pick];
            prey[pick] = prey[--preyCount];
            out[n++] = TaskOffer{
                .verb = 1,
                .object = 0,
                .target = p.id,
                .cell = p.cell,
                .amount = 1,
                .xp = task_xp(1, 0, 1),
            };
        }
    }

    // ── ПОСЕТИ: фича-достопримечательность из округи — чтение слоя фич.
    {
        struct Sight {
            std::uint16_t ft;
            std::uint32_t cell;
        };
        Sight sights[16];
        int sightCount = 0;
        for (int dy = -kVisitScanRadius;
             dy <= kVisitScanRadius && sightCount < 16; ++dy) {
            for (int dx = -kVisitScanRadius;
                 dx <= kVisitScanRadius && sightCount < 16; ++dx) {
                // Сосед — шаг ИНДЕКСА (`cell_step`, ЗАКОН АДРЕСА): здесь
                // стояли две свёртки осей и две сборки индекса обратно.
                const std::uint32_t n = cell_step(here, dx, dy, W);
                const FeatureType ft = features.at(n);
                for (const FeatureType want : kVisitWorthy) {
                    if (ft != want) continue;
                    sights[sightCount++] = {std::uint16_t(ft), n};
                    break;
                }
            }
        }
        for (int k = 0; k < kOffersPerVerb[2] && sightCount > 0 && n < cap;
             ++k) {
            const int pick = rng.next_int(0, sightCount);
            const Sight sg = sights[pick];
            sights[pick] = sights[--sightCount];
            out[n++] = TaskOffer{
                .verb = 2,
                .object = sg.ft,
                .target = 0,
                .cell = sg.cell,
                .amount = 1,
                .xp = task_xp(2, sg.ft, 1),
            };
        }
    }

    return n;
}

bool task_take(MacroStore& st, GameState& gs, MacroHandle player,
               MacroHandle place, const TaskOffer& offer) {
    if (!st.valid(player) || !st.valid(place)) return false;
    if (offer.verb >= std::size(kTaskVerbs)) return false;
    if (offer.object > 0xFFF || offer.amount == 0) return false;
    const std::uint32_t placeId = st.spawnId[place.slot].index;
    if (placeId == 0u) return false;

    const TaskVerbDef& v = kTaskVerbs[offer.verb];
    // Ординал цели: сквадные роды несут цель-сквад/место, Feature — род фичи.
    const std::uint32_t ord =
        v.goalKind == ecs::GoalKind::Feature ? offer.object : offer.target;
    ecs::goal_set(st.goal[player.slot], offer.cell,
                  ecs::goal_what(v.goalKind, ord), offer.amount, 0);

    WorldFact f{};
    f.day = gs.worldTime.day();
    f.kind = std::uint16_t(FactKind::QuestTaken);
    f.objectKind = std::uint8_t(FactSubject::Rooted);
    f.object = placeId;
    f.x = std::int16_t(ecs::cell_x(st.cell[place.slot], gs.mapW));
    f.y = std::int16_t(ecs::cell_y(st.cell[place.slot], gs.mapW));
    f.amount = task_pack(offer.verb, offer.object, offer.amount);
    return record_deed(st, gs, f, player) != 0u;
}

TaskActive task_active_at(const MacroStore& st, const GameState& gs,
                          ChronicleLinks& links,
                          MacroHandle player, MacroHandle place) {
    TaskActive t{};
    if (!st.valid(player) || !st.valid(place)) return t;
    ContractScan s{};
    s.playerId = st.spawnId[player.slot].index;
    s.placeId = st.spawnId[place.slot].index;
    if (s.playerId == 0u || s.placeId == 0u) return t;
    // Здесь спрашивали окрестность места без горизонта: длину обхода держал
    // только кап забывающего кольца. Контракты берёт СКВАД — спрашивается его
    // цепочка, и обход равен его истории.
    chronicle_by_subject(gs.chronicle, links, st, player, kAllDays,
                         kWholeChain, scan_contract, &s);
    if (s.takenSeq == 0u) return t;            // контрактов не брали
    if (s.doneSeq > s.takenSeq) return t;      // новейший уже сдан
    // ВЗЯТ БОЛЕЕ НОВЫЙ КОНТРАКТ В ДРУГОМ МЕСТЕ — ЭТОТ БРОШЕН (M-459). Прежде
    // это отвечал ТЕГ цели: «принести» ложилось родом места, «убить» — родом
    // сквада, и сверка рода ниже отличала одно от другого. Род цели у них
    // теперь один, а тег и раньше судил слабо — два контракта «убить» из
    // двух городов он не различал вовсе. Летопись различает всё: цель у
    // сквада одна, значит жив только новейший взятый.
    if (s.newestTakenSeq != s.takenSeq) return t;
    t.verb = task_verb(s.packed);
    if (t.verb >= std::size(kTaskVerbs)) return t;
    t.object = task_object(s.packed);
    t.amount = task_count(s.packed);
    t.takenDay = s.takenDay;

    // Цель игрока всё ещё ЭТОТ контракт? Задача есть одна цель (вердикт №2):
    // перезаписанная цель — брошенный контракт, не параллельный.
    const TaskVerbDef& v = kTaskVerbs[t.verb];
    const ecs::SquadGoal& g = st.goal[player.slot];
    if (ecs::goal_kind(g.what) != v.goalKind) return t;
    if (v.object == TaskObjectSpace::Need
        && ecs::goal_ordinal(g.what) != s.placeId) {
        return t;
    }
    if (v.object == TaskObjectSpace::Feature
        && ecs::goal_ordinal(g.what) != t.object) {
        return t;
    }

    const int today = gs.worldTime.day();
    t.expired = v.deadlineDays > 0
                && today - t.takenDay > int(v.deadlineDays);
    t.valid = true;
    return t;
}

bool task_judged_done(const MacroStore& st, const GameState& gs,
                      ChronicleLinks& links,
                      MacroHandle player, const TaskActive& t) {
    if (!t.valid || t.expired || !st.valid(player)) return false;
    const TaskVerbDef& v = kTaskVerbs[t.verb];
    const std::uint32_t playerId = st.spawnId[player.slot].index;
    switch (v.object) {
        case TaskObjectSpace::Need:
            return st.inventory[player.slot].inv.count_of(int(t.object))
                   >= int(t.amount);
        case TaskObjectSpace::Hostile: {
            KillScan s{};
            s.playerId = playerId;
            s.targetId = ecs::goal_ordinal(st.goal[player.slot].what);
            if (s.targetId == 0u) return false;
            // (Здесь шёл проход по ВСЕМУ миру с дня взятия под литералом
            //  `1 << 16`, побитово равным капу кольца, но имени его не
            //  называвшим. «Кого убил этот сквад» есть его цепочка.)
            chronicle_by_subject(gs.chronicle, links, st, player, t.takenDay,
                                 kWholeChain, scan_kills, &s);
            return s.count >= int(t.amount);
        }
        case TaskObjectSpace::Feature: {
            const ecs::SquadGoal& g = st.goal[player.slot];
            VisitScan s{};
            s.playerId = playerId;
            s.x = std::int16_t(cell_x(g.cell, gs.mapW));
            s.y = std::int16_t(cell_y(g.cell, gs.mapW));
            chronicle_by_subject(gs.chronicle, links, st, player, t.takenDay,
                                 kWholeChain, scan_visits, &s);
            return s.seen;
        }
    }
    return false;
}

bool task_turn_in(MacroStore& st, GameState& gs, ChronicleLinks& links,
                  MacroHandle player, MacroHandle place) {
    const TaskActive t = task_active_at(st, gs, links, player, place);
    if (!task_judged_done(st, gs, links, player, t)) return false;
    const TaskVerbDef& v = kTaskVerbs[t.verb];

    if (v.object == TaskObjectSpace::Need) {
        // Перенос = сделка с пустым встречным лотом; exact — полуплатёж есть
        // ложь (ЗАКОН ТРЁХ ДВЕРЕЙ: try_spend-семантика у сделки).
        DealLine give[1];
        give[0].want = int(t.amount);
        give[0].def = t.object;
        give[0].exact = 1;
        const DealFact df =
            deal(st, player.slot, place.slot, std::span<DealLine>(give),
                 std::span<DealLine>{});
        if (!df.settled || give[0].done < int(t.amount)) return false;
    }

    WorldFact f{};
    f.day = gs.worldTime.day();
    f.kind = std::uint16_t(FactKind::QuestDone);
    f.objectKind = std::uint8_t(FactSubject::Rooted);
    f.object = st.spawnId[place.slot].index;
    f.x = std::int16_t(ecs::cell_x(st.cell[place.slot], gs.mapW));
    f.y = std::int16_t(ecs::cell_y(st.cell[place.slot], gs.mapW));
    f.amount = task_pack(t.verb, t.object, t.amount);
    record_deed(st, gs, f, player);  // слава платится этой дверью сама

    squad_award_exp(st, player, int(task_xp(t.verb, t.object, t.amount)));

    // Задача исполнена — цель погашена; новую решит игрок (или доска).
    ecs::goal_set(st.goal[player.slot], ecs::kNoCell, 0u, 0, 0);
    return true;
}

void task_note_player_arrival(MacroStore& st, GameState& gs,
                              ChronicleLinks& links,
                              MacroHandle player, int x, int y) {
    if (!st.valid(player)) return;
    const ecs::SquadGoal& g = st.goal[player.slot];
    if (g.cell == ecs::kNoCell
        || ecs::goal_kind(g.what) != ecs::GoalKind::Feature) {
        return;
    }
    if (cell_of(x, y, gs.mapW) != g.cell) return;
    // Дедуп — память мира (прецедент SubZone): уже помнит — не пишем.
    VisitScan s{};
    s.playerId = st.spawnId[player.slot].index;
    s.x = std::int16_t(x);
    s.y = std::int16_t(y);
    chronicle_by_subject(gs.chronicle, links, st, player, kAllDays,
                         kWholeChain, scan_visits, &s);
    if (s.seen) return;
    WorldFact f{};
    f.day = gs.worldTime.day();
    f.kind = std::uint16_t(FactKind::Explored);
    f.x = std::int16_t(x);
    f.y = std::int16_t(y);
    f.amount = 1;
    record_deed(st, gs, f, player);
}

}  // namespace sm
