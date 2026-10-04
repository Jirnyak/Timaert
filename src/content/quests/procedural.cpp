#include "content/quests/procedural.h"

#include "core/rng.h"
#include "core/torus.h"
#include "macro/agent_memory.h"
#include "tables/commodity.h"
#include "macro/anketa.h"
#include "macro/landmark_iter.h"   // for_each_place — места по слотам
#include "tables/npc.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace sm {

// Fisher-Yates shuffle of the 7 quest-generator slots. Kept at namespace scope
// (external linkage, not in the anonymous namespace below) so the out-of-bounds
// guard can be white-box tested; see tests/quest_lifecycle_test.cpp.
// Порядок бросков — по РАЗМЕРУ таблицы генераторов, а не по числу в коде:
// список {0..6} пережил вырезанный генератор и уводил индекс за край
// (bus error, поймано 2026-09-19). Счёт строк знает только таблица.
std::vector<int> shuffled_order(Rng& rng, int count) {
    std::vector<int> order(std::size_t(count < 0 ? 0 : count));
    for (int i = 0; i < int(order.size()); ++i) order[std::size_t(i)] = i;
    for (int i = int(order.size()) - 1; i > 0; --i) {
        // next_f01() is documented as [0,1), but float(0xFFFFFFFF)/2^32 rounds
        // up to exactly 1.0f, so int(f * (i + 1)) can equal i + 1 and index one
        // past the end. Clamp to keep the swap in range (no-op unless f == 1.0f).
        const int j = std::min(int(rng.next_f01() * float(i + 1)), i);
        std::swap(order[std::size_t(i)], order[std::size_t(j)]);
    }
    return order;
}

namespace {

struct QuestGenCtx {
    int id = -1;
    std::string name;
    int x = 0;
    int y = 0;
    bool isCity = false;
    int factionIdx = -1;
    const Inventory* store = nullptr;   // the place's universal inventory
    const MacroStore* st = nullptr;     // «какие ещё места есть в мире»
    const GameState* gs = nullptr;
    Rng* rng = nullptr;
};

// ИМЯ МЕСТА ДЛЯ ПОКАЗА — колонка `name` его тела, а пустая отвечает ярлыком
// строки реестра: безымянный есть честный случай именованного рода (тот же
// один закон, что у обхода мест), и шпиль с руиной не показывают пустоту.
std::string place_label(const MacroStore& st, std::uint16_t slot) {
    const char* t = st.name[slot].text;
    if (t[0] != '\0') return std::string(t);
    return std::string(
        landmark_def(SquadType(st.runtime[slot].squadType)).label.data());
}

using GeneratorFn = bool (*)(const QuestGenCtx&, Quest&);

// Whose standing does this quest move? The giver's own faction column — the
// same rule the spawners use (`kind.factionIdx` of its body, kingdoms cut
// 2026-09-11), so doing a job for Old Magica raises Old Magica, not the
// empire. An unowned settlement resolves to the free folk like everywhere
// else.
std::string faction_of(const QuestGenCtx& ctx) {
    if (!ctx.gs) return "empire";
    return faction_id_for_index(faction_or_freefolk(ctx.factionIdx));
}

std::string direction_name(float angle) {
    static constexpr const char* kNames[] = {
        "east", "northeast", "north", "northwest",
        "west", "southwest", "south", "southeast",
    };
    constexpr float kTau = 6.2831853071795864769f;
    int idx = int(std::round(((angle / kTau) * 8.0f) + 8.0f)) % 8;
    return kNames[std::size_t(idx)];
}

std::string describe_destination(const QuestGenCtx& ctx, int targetX, int targetY) {
    const GameState& gs = *ctx.gs;
    const float dist = torus_dist(float(ctx.x), float(ctx.y),
                                  float(targetX), float(targetY),
                                  float(gs.mapW), float(gs.mapH));
    int days = int(std::round(dist / 10.0f));
    if (days < 1) days = 1;
    const float angle = std::atan2(float(targetY - ctx.y), float(targetX - ctx.x));
    return "~" + std::to_string(days)
        + (days > 1 ? " days travel to the " : " day travel to the ")
        + direction_name(angle) + ".";
}

// Identity is NOT set here: the offer's provenance triple (giver, slot,
// bornDay) is stamped once in generate_for_context, and the ordinal is
// issued at accept (quest_types.h Quest).
void add_common(Quest& q, const QuestGenCtx& ctx,
                QuestCategory category, int difficulty, int expireDelta) {
    q.category = category;
    q.giverSettlementId = ctx.id;
    q.expireDay = ctx.gs->worldTime.day() + expireDelta;
    q.difficulty = difficulty;
}

void add_gold_xp_rewards(Quest& q, int gold, float xpMul) {
    Reward goldReward{};
    goldReward.kind = RewardKind::Gold;
    goldReward.amount = gold;
    q.rewards.push_back(goldReward);

    Reward xpReward{};
    xpReward.kind = RewardKind::Xp;
    xpReward.amount = int(std::round(float(gold) * xpMul));
    q.rewards.push_back(xpReward);
}

void add_reputation_reward(Quest& q, const QuestGenCtx& ctx, int delta) {
    Reward reward{};
    reward.kind = RewardKind::Reputation;
    reward.faction = faction_of(ctx);
    reward.delta = delta;
    q.rewards.push_back(std::move(reward));
}

bool gen_delivery(const QuestGenCtx& ctx, Quest& q) {
    if (!ctx.isCity || !ctx.store) return false;

    // The town asks for what it is SHORTEST of — the same stock classes a
    // caravan's memory grades markets by (one dictionary, one scarcity eye).
    int bestIdx = -1;
    int bestClass = 4;
    for (int i = 0; i < kCommodityCount; ++i) {
        // Город просит ТОВАР, а не сырьё: категория каталога и есть ответ
        // (items.h ItemType — ярус товарной таблицы умер 2026-09-18).
        const ItemDef* d = item_def(kCommodities[i].id);
        if (!d || d->type == ItemType::Material) continue;
        const int cls = stock_class(ctx.store->count(kCommodities[i].id));
        if (cls < bestClass) {
            bestClass = cls;
            bestIdx = i;
        }
    }
    if (bestIdx < 0 || bestClass >= 3) return false;   // a sated town asks nothing
    const char* itemId = kCommodities[bestIdx].id;
    const ItemDef* item = item_def(itemId);
    if (!item) return false;

    const int baseQty = 3 + int(ctx.rng->next_f01() * 8.0f);
    const int gold = int(std::round(float(baseQty) * float(item->value)
        * (1.5f + ctx.rng->next_f01())));
    int difficulty = int(std::ceil(float(baseQty) / 2.0f));
    if (difficulty > 10) difficulty = 10;

    add_common(q, ctx, QuestCategory::Procedural,
               difficulty, 30);
    q.title = std::string("Supply ") + item->name;
    q.description = ctx.name + " urgently needs " + std::to_string(baseQty)
        + " units of " + item->name
        + ". The local market pays well above standard rates.";

    Objective o{};
    o.kind = ObjectiveKind::DeliverItems;
    o.itemId = itemId;
    o.quantity = baseQty;
    o.targetSettlementId = ctx.id;
    q.objectives.push_back(std::move(o));

    add_gold_xp_rewards(q, gold, 0.3f);
    add_reputation_reward(q, ctx, 5);
    return true;
}

bool gen_visit(const QuestGenCtx& ctx, Quest& q) {
    const GameState& gs = *ctx.gs;
    if (!ctx.st) return false;
    const MacroStore& st = *ctx.st;
    const auto tx_of = [&](std::uint16_t s) {
        return ecs::cell_x(st.cell[s], gs.mapW);
    };
    const auto ty_of = [&](std::uint16_t s) {
        return ecs::cell_y(st.cell[s], gs.mapW);
    };
    std::vector<std::uint16_t> candidates;
    for_each_place(st, [&](std::uint16_t slot) {
        if (SquadType(st.runtime[slot].squadType) != SquadType::City)
            return;
        if (int(st.spawnId[slot].index) != ctx.id) candidates.push_back(slot);
    });
    if (candidates.empty()) return false;

    std::sort(candidates.begin(), candidates.end(),
        [&](std::uint16_t a, std::uint16_t b) {
            const float da = torus_dist(float(ctx.x), float(ctx.y),
                                        float(tx_of(a)), float(ty_of(a)),
                                        float(gs.mapW), float(gs.mapH));
            const float db = torus_dist(float(ctx.x), float(ctx.y),
                                        float(tx_of(b)), float(ty_of(b)),
                                        float(gs.mapW), float(gs.mapH));
            return db < da;
        });

    const int pickCount = int((candidates.size() + 1u) / 2u);
    const std::uint16_t target =
        candidates[std::size_t(ctx.rng->next_int(0, pickCount))];
    const int targetX = tx_of(target), targetY = ty_of(target);
    const float dist = torus_dist(float(ctx.x), float(ctx.y),
                                  float(targetX), float(targetY),
                                  float(gs.mapW), float(gs.mapH));
    const float distFactor = 1.0f + dist / (float(gs.mapW) * 0.25f);
    const int gold = int(std::round(30.0f * distFactor
        + ctx.rng->next_f01() * 20.0f));
    int difficulty = int(std::ceil(distFactor * 2.0f));
    if (difficulty > 10) difficulty = 10;
    int expire = int(std::round(dist / 10.0f));
    if (expire < 14) expire = 14;

    add_common(q, ctx, QuestCategory::Procedural, difficulty, expire);
    const std::string targetName = place_label(st, target);
    q.title = "Envoy to " + targetName;
    q.description = "Deliver a sealed letter to the magistrate of "
        + targetName + ". " + describe_destination(ctx, targetX, targetY);

    Objective o{};
    o.kind = ObjectiveKind::VisitCell;
    o.ix = targetX;
    o.iy = targetY;
    o.radius = 5.0f;
    q.objectives.push_back(o);
    add_gold_xp_rewards(q, gold, 0.5f);
    return true;
}

bool gen_destroy(const QuestGenCtx& ctx, Quest& q) {
    constexpr float kTau = 6.2831853071795864769f;
    const int count = 1 + int(ctx.rng->next_f01() * 3.0f);
    const int level = 1 + int(ctx.rng->next_f01() * 5.0f);
    const int gold = int(std::round(float(count * level * 15)
        + ctx.rng->next_f01() * 30.0f));
    int difficulty = count + level;
    if (difficulty > 10) difficulty = 10;

    const float angle = ctx.rng->next_f01() * kTau;
    const float dist = 20.0f + ctx.rng->next_f01() * 40.0f;
    const int zoneX = int(std::round(float(ctx.x) + std::cos(angle) * dist));
    const int zoneY = int(std::round(float(ctx.y) + std::sin(angle) * dist));

    add_common(q, ctx, QuestCategory::Procedural,
               difficulty, 20);
    q.title = "Clear the Road";
    q.description = "Bandits have been terrorising travellers near "
        + ctx.name + ". Eliminate " + std::to_string(count)
        + " of them. " + describe_destination(ctx, zoneX, zoneY);

    Objective o{};
    o.kind = ObjectiveKind::DestroyNpc;
    o.npcType = int(NPCType::Bandit);
    o.count = count;
    o.ix = zoneX;
    o.iy = zoneY;
    q.objectives.push_back(o);

    add_gold_xp_rewards(q, gold, 0.6f);
    add_reputation_reward(q, ctx, 8);

    GameEvent spawn{EventTag::SpawnEntity};
    spawn.s1 = "bandit";
    // Клетка мира сворачивается КАНОНИЧЕСКОЙ дверью (`cell_of`, core/torus.h):
    // сторона есть степень двойки, значит заворот — одна маска, а `wrapi`,
    // стоявший здесь, делил `std::int64_t %` по рантайм-делителю. Свернуть
    // КЛЕТКУ МИРА так названо дефектом дословно (AGENTS §3 ЗАКОН АДРЕСА п.4).
    const std::uint32_t zc = cell_of(zoneX, zoneY, ctx.gs->mapW);
    spawn.ix = cell_x(zc, ctx.gs->mapW);
    spawn.iy = cell_y(zc, ctx.gs->mapW);
    spawn.a = std::uint32_t(level);
    // One event = one body (the consumer's contract), so a kill-N contract
    // ships N spawn events. Copies, not fresh rolls: the spawner scatters
    // positions itself, and this keeps the generator's RNG stream unchanged.
    for (int i = 0; i < count; ++i) q.onAccept.push_back(spawn);
    return true;
}

// (gen_protect — квест «защити деревню» — ВЫРЕЗАН 2026-09-19 вместе с
// настроением, которое его поднимало: контент вернётся системно, когда у
// мира будет из чего его выводить, а не из мёртвой полосы настроения.)

bool gen_fetch(const QuestGenCtx& ctx, Quest& q) {
    static constexpr const char* kItems[] = {"mat_herb", "iron", "wood"};
    const char* itemId = kItems[std::size_t(ctx.rng->next_int(0, 3))];
    const int quantity = 2 + int(ctx.rng->next_f01() * 5.0f);
    const int gold = int(std::round(float(quantity) * 12.0f
        + ctx.rng->next_f01() * 15.0f));
    int difficulty = int(std::ceil(float(quantity) / 2.0f));
    if (difficulty > 10) difficulty = 10;

    add_common(q, ctx, QuestCategory::Procedural,
               difficulty, 14);
    q.title = "Gather Materials";
    q.description = ctx.name + " needs " + std::to_string(quantity) + " "
        + std::string(itemId).substr(4) + ". Gather them from the surrounding lands.";

    Objective o{};
    o.kind = ObjectiveKind::DeliverItems;
    o.itemId = itemId;
    o.quantity = quantity;
    o.targetSettlementId = ctx.id;
    q.objectives.push_back(std::move(o));
    add_gold_xp_rewards(q, gold, 0.3f);
    return true;
}

bool gen_scout(const QuestGenCtx& ctx, Quest& q) {
    constexpr float kTau = 6.2831853071795864769f;
    const float angle = ctx.rng->next_f01() * kTau;
    const float dist = 30.0f + ctx.rng->next_f01() * 50.0f;
    // Заворот клетки мира — дверью адреса, см. `gen_bandit_camp` выше.
    const std::uint32_t tc = cell_of(
        int(std::round(float(ctx.x) + std::cos(angle) * dist)),
        int(std::round(float(ctx.y) + std::sin(angle) * dist)), ctx.gs->mapW);
    const int tx = cell_x(tc, ctx.gs->mapW);
    const int ty = cell_y(tc, ctx.gs->mapW);
    const float distFactor = 1.0f + dist / 50.0f;
    const int gold = int(std::round(25.0f * distFactor
        + ctx.rng->next_f01() * 15.0f));
    int difficulty = int(std::ceil(distFactor * 1.5f));
    if (difficulty > 10) difficulty = 10;

    add_common(q, ctx, QuestCategory::Procedural,
               difficulty, 21);
    q.title = "Scout the Wilds";
    q.description = "Survey the area to the " + direction_name(angle)
        + " and report back to " + ctx.name + ". "
        + describe_destination(ctx, tx, ty);

    Objective outbound{};
    outbound.kind = ObjectiveKind::VisitCell;
    outbound.ix = tx;
    outbound.iy = ty;
    outbound.radius = 8.0f;
    q.objectives.push_back(outbound);

    Objective ret{};
    ret.kind = ObjectiveKind::VisitCell;
    ret.ix = ctx.x;
    ret.iy = ctx.y;
    ret.radius = 5.0f;
    q.objectives.push_back(ret);

    add_gold_xp_rewards(q, gold, 0.4f);
    return true;
}

bool gen_sanctuary(const QuestGenCtx& ctx, Quest& q) {
    if (ctx.isCity) return false;
    if (ctx.rng->next_f01() > 0.3f) return false;

    constexpr float kTau = 6.2831853071795864769f;
    const float angle = ctx.rng->next_f01() * kTau;
    const float dist = 40.0f + ctx.rng->next_f01() * 60.0f;
    // Заворот клетки мира — дверью адреса, см. `gen_bandit_camp` выше.
    const std::uint32_t tc = cell_of(
        int(std::round(float(ctx.x) + std::cos(angle) * dist)),
        int(std::round(float(ctx.y) + std::sin(angle) * dist)), ctx.gs->mapW);
    const int tx = cell_x(tc, ctx.gs->mapW);
    const int ty = cell_y(tc, ctx.gs->mapW);
    const float distFactor = 1.0f + dist / 50.0f;
    const int gold = int(std::round(60.0f * distFactor
        + ctx.rng->next_f01() * 40.0f));
    int difficulty = int(std::ceil(distFactor * 2.0f));
    if (difficulty > 10) difficulty = 10;
    int expire = int(std::round(dist / 8.0f));
    if (expire < 21) expire = 21;

    add_common(q, ctx, QuestCategory::Side,
               difficulty, expire);
    q.title = "Find the Sanctuary";
    q.description = "An elder speaks of an ancient sanctuary lost to time. "
        + describe_destination(ctx, tx, ty);

    Objective o{};
    o.kind = ObjectiveKind::VisitCell;
    o.ix = tx;
    o.iy = ty;
    o.radius = 6.0f;
    q.objectives.push_back(o);
    add_gold_xp_rewards(q, gold, 0.8f);
    add_reputation_reward(q, ctx, 12);
    return true;
}

std::vector<Quest> generate_for_context(QuestGenCtx& ctx) {
    static constexpr GeneratorFn kGenerators[] = {
        gen_delivery,
        gen_visit,
        gen_destroy,
        gen_fetch,
        gen_scout,
        gen_sanctuary,
    };
    // gen_visit's slot, for the empty-day fallback below — looked up from the
    // table so a reorder cannot silently desynchronise the provenance.
    constexpr auto slot_of = [](GeneratorFn fn) {
        for (std::size_t i = 0; i < std::size(kGenerators); ++i)
            if (kGenerators[i] == fn) return i;
        return std::size_t(0);
    };

    const int maxQuests = ctx.isCity
        ? 2 + int(ctx.rng->next_f01() * 3.0f)
        : 1 + int(ctx.rng->next_f01() * 2.0f);
    std::vector<Quest> out;
    out.reserve(std::size_t(maxQuests));

    // The offer's provenance triple, stamped in ONE place: each generator
    // fires at most once per settlement per day, so {giver, slot, bornDay}
    // names the offer uniquely (quest_types.h Quest) — it is what is_known
    // dedups by, and what the old id string used to encode.
    const auto stamp = [&](Quest& q, std::size_t slot) {
        q.giverSettlementId = ctx.id;
        q.offerSlot = std::uint8_t(slot);
        q.bornDay = ctx.gs->worldTime.day();
    };

    std::vector<int> order =
        shuffled_order(*ctx.rng, int(std::size(kGenerators)));
    for (int idx : order) {
        if (int(out.size()) >= maxQuests) break;
        Quest q{};
        if (kGenerators[std::size_t(idx)](ctx, q)) {
            stamp(q, std::size_t(idx));
            out.push_back(std::move(q));
        }
    }

    if (out.empty()) {
        Quest fallback{};
        if (gen_visit(ctx, fallback)) {
            stamp(fallback, slot_of(gen_visit));
            out.push_back(std::move(fallback));
        }
    }
    return out;
}

} // namespace

std::vector<Quest> generate_quests_for_settlement(std::uint16_t slot,
                                                  const MacroStore& st,
                                                  const GameState& gs,
                                                  std::uint32_t worldSeed) {
    const int id = int(st.spawnId[slot].index);
    Rng rng(worldSeed ^ std::uint32_t(id) ^ std::uint32_t(gs.worldTime.day()));
    QuestGenCtx ctx{};
    ctx.id = id;
    ctx.name = place_label(st, slot);
    ctx.x = ecs::cell_x(st.cell[slot], gs.mapW);
    ctx.y = ecs::cell_y(st.cell[slot], gs.mapW);
    ctx.isCity = true;
    ctx.factionIdx = int(std::int16_t(st.kind[slot].factionIdx));
    ctx.store = &st.inventory[slot].inv;
    ctx.st = &st;
    ctx.gs = &gs;
    ctx.rng = &rng;
    return generate_for_context(ctx);
}

std::vector<Quest> generate_quests_for_village(std::uint16_t slot,
                                               const MacroStore& st,
                                               const GameState& gs,
                                               std::uint32_t worldSeed) {
    const int id = int(st.spawnId[slot].index);
    Rng rng(worldSeed ^ std::uint32_t(id + 0x6000)
            ^ std::uint32_t(gs.worldTime.day()));
    QuestGenCtx ctx{};
    ctx.id = id;
    ctx.name = place_label(st, slot);
    ctx.x = ecs::cell_x(st.cell[slot], gs.mapW);
    ctx.y = ecs::cell_y(st.cell[slot], gs.mapW);
    ctx.isCity = false;
    ctx.factionIdx = int(std::int16_t(st.kind[slot].factionIdx));
    ctx.store = &st.inventory[slot].inv;
    ctx.st = &st;
    ctx.gs = &gs;
    ctx.rng = &rng;
    return generate_for_context(ctx);
}

} // namespace sm
