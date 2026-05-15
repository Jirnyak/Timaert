#include "content/quests/procedural.h"

#include "core/rng.h"
#include "core/torus.h"
#include "macro/economy.h"
#include "macro/npc.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace sm {
namespace {

struct QuestGenCtx {
    int id = -1;
    std::string idSegment;
    std::string name;
    int x = 0;
    int y = 0;
    bool isCity = false;
    SettlementMood mood = SettlementMood::Stable;
    int kingdomIdx = -1;
    const EconomyState* eco = nullptr;
    const GameState* gs = nullptr;
    Rng* rng = nullptr;
};

using GeneratorFn = bool (*)(const QuestGenCtx&, Quest&);

const char* material_id(ResourceId id) {
    switch (id) {
        case ResourceId::Grain:  return "mat_grain";
        case ResourceId::Wood:   return "mat_wood";
        case ResourceId::Iron:   return "mat_iron";
        case ResourceId::Clay:   return "mat_clay";
        case ResourceId::Silver: return "mat_silver";
        case ResourceId::Gems:   return "mat_gems";
        case ResourceId::Count:  break;
    }
    return "mat_wood";
}

std::string faction_of(const QuestGenCtx&) {
    return "empire";
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

void add_common(Quest& q, const QuestGenCtx& ctx, const char* prefix,
                QuestCategory category, int difficulty, int expireDelta) {
    q.id = std::string(prefix) + "_" + ctx.idSegment + "_"
        + std::to_string(ctx.gs->worldTime.day);
    q.category = category;
    q.giverSettlementId = ctx.id;
    q.expireDay = ctx.gs->worldTime.day + expireDelta;
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
    if (!ctx.isCity || !ctx.eco) return false;

    ResourceId bestResource = ResourceId::Grain;
    float bestPrice = 0.0f;
    for (std::size_t i = 0; i < kNumResources; ++i) {
        const float price = ctx.eco->resourcePrices[i];
        if (price > bestPrice) {
            bestPrice = price;
            bestResource = static_cast<ResourceId>(i);
        }
    }

    const int baseQty = 3 + int(ctx.rng->next_f01() * 8.0f);
    const float basePrice = kResourceBasePrice[std::size_t(bestResource)];
    const int gold = int(std::round(float(baseQty) * basePrice
        * (1.5f + ctx.rng->next_f01())));
    int difficulty = int(std::ceil(float(baseQty) / 2.0f));
    if (difficulty > 10) difficulty = 10;

    add_common(q, ctx, "q_proc_deliver", QuestCategory::Procedural,
               difficulty, 30);
    q.title = std::string("Supply ") + kResourceNames[std::size_t(bestResource)];
    q.description = ctx.name + " urgently needs " + std::to_string(baseQty)
        + " units of " + kResourceNames[std::size_t(bestResource)]
        + ". The local market pays well above standard rates.";

    Objective o{};
    o.kind = ObjectiveKind::DeliverItems;
    o.itemId = material_id(bestResource);
    o.quantity = baseQty;
    o.targetSettlementId = ctx.id;
    q.objectives.push_back(std::move(o));

    add_gold_xp_rewards(q, gold, 0.3f);
    add_reputation_reward(q, ctx, 5);
    return true;
}

bool gen_visit(const QuestGenCtx& ctx, Quest& q) {
    const GameState& gs = *ctx.gs;
    std::vector<const Settlement*> candidates;
    candidates.reserve(gs.settlements.size());
    for (const auto& settlement : gs.settlements) {
        if (settlement.id != ctx.id) candidates.push_back(&settlement);
    }
    if (candidates.empty()) return false;

    std::sort(candidates.begin(), candidates.end(),
        [&](const Settlement* a, const Settlement* b) {
            const float da = torus_dist(float(ctx.x), float(ctx.y),
                                        float(a->x), float(a->y),
                                        float(gs.mapW), float(gs.mapH));
            const float db = torus_dist(float(ctx.x), float(ctx.y),
                                        float(b->x), float(b->y),
                                        float(gs.mapW), float(gs.mapH));
            return db < da;
        });

    const int pickCount = int((candidates.size() + 1u) / 2u);
    const Settlement& target = *candidates[std::size_t(ctx.rng->next_int(0, pickCount))];
    const float dist = torus_dist(float(ctx.x), float(ctx.y),
                                  float(target.x), float(target.y),
                                  float(gs.mapW), float(gs.mapH));
    const float distFactor = 1.0f + dist / (float(gs.mapW) * 0.25f);
    const int gold = int(std::round(30.0f * distFactor
        + ctx.rng->next_f01() * 20.0f));
    int difficulty = int(std::ceil(distFactor * 2.0f));
    if (difficulty > 10) difficulty = 10;
    int expire = int(std::round(dist / 10.0f));
    if (expire < 14) expire = 14;

    q.id = "q_proc_visit_" + ctx.idSegment + "_"
        + std::to_string(target.id) + "_" + std::to_string(gs.worldTime.day);
    q.category = QuestCategory::Procedural;
    q.giverSettlementId = ctx.id;
    q.expireDay = gs.worldTime.day + expire;
    q.difficulty = difficulty;
    q.title = "Envoy to " + target.name;
    q.description = "Deliver a sealed letter to the magistrate of "
        + target.name + ". " + describe_destination(ctx, target.x, target.y);

    Objective o{};
    o.kind = ObjectiveKind::VisitCell;
    o.ix = target.x;
    o.iy = target.y;
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

    add_common(q, ctx, "q_proc_destroy", QuestCategory::Procedural,
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
    o.zoneRadius = 30.0f;
    q.objectives.push_back(o);

    add_gold_xp_rewards(q, gold, 0.6f);
    add_reputation_reward(q, ctx, 8);

    GameEvent spawn{EventTag::SpawnEntity};
    spawn.s1 = "bandit";
    spawn.ix = wrapi(zoneX, ctx.gs->mapW);
    spawn.iy = wrapi(zoneY, ctx.gs->mapH);
    spawn.a = std::uint32_t(level);
    q.onAccept.push_back(std::move(spawn));
    return true;
}

bool gen_protect(const QuestGenCtx& ctx, Quest& q) {
    if (ctx.isCity) return false;
    if (ctx.mood != SettlementMood::Tense
        && ctx.mood != SettlementMood::Unrest
        && ctx.mood != SettlementMood::Revolt
        && ctx.rng->next_f01() > 0.3f) {
        return false;
    }

    const int hours = 4 + int(ctx.rng->next_f01() * 8.0f);
    const int gold = int(std::round(40.0f + float(hours) * 8.0f
        + ctx.rng->next_f01() * 20.0f));
    int difficulty = int(std::ceil(float(hours) / 2.0f));
    if (difficulty > 10) difficulty = 10;

    add_common(q, ctx, "q_proc_protect", QuestCategory::Procedural,
               difficulty, 7);
    q.title = "Defend " + ctx.name;
    q.description = "Raiders threaten " + ctx.name + ". Stay and protect the villagers for "
        + std::to_string(hours) + " hours.";

    Objective o{};
    o.kind = ObjectiveKind::WaitAt;
    o.ix = ctx.x;
    o.iy = ctx.y;
    o.radius = 5.0f;
    o.hoursRequired = hours;
    q.objectives.push_back(o);

    add_gold_xp_rewards(q, gold, 0.4f);
    add_reputation_reward(q, ctx, 10);

    GameEvent spawn{EventTag::SpawnEntity};
    spawn.s1 = "bandit";
    spawn.ix = wrapi(ctx.x + int(std::round((ctx.rng->next_f01() - 0.5f) * 60.0f)),
                     ctx.gs->mapW);
    spawn.iy = wrapi(ctx.y + int(std::round((ctx.rng->next_f01() - 0.5f) * 60.0f)),
                     ctx.gs->mapH);
    spawn.a = std::uint32_t(2 + int(ctx.rng->next_f01() * 3.0f));
    q.onAccept.push_back(std::move(spawn));
    return true;
}

bool gen_fetch(const QuestGenCtx& ctx, Quest& q) {
    static constexpr const char* kItems[] = {"mat_herb", "mat_iron", "mat_wood"};
    const char* itemId = kItems[std::size_t(ctx.rng->next_int(0, 3))];
    const int quantity = 2 + int(ctx.rng->next_f01() * 5.0f);
    const int gold = int(std::round(float(quantity) * 12.0f
        + ctx.rng->next_f01() * 15.0f));
    int difficulty = int(std::ceil(float(quantity) / 2.0f));
    if (difficulty > 10) difficulty = 10;

    add_common(q, ctx, "q_proc_fetch", QuestCategory::Procedural,
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
    const int tx = wrapi(int(std::round(float(ctx.x) + std::cos(angle) * dist)),
                         ctx.gs->mapW);
    const int ty = wrapi(int(std::round(float(ctx.y) + std::sin(angle) * dist)),
                         ctx.gs->mapH);
    const float distFactor = 1.0f + dist / 50.0f;
    const int gold = int(std::round(25.0f * distFactor
        + ctx.rng->next_f01() * 15.0f));
    int difficulty = int(std::ceil(distFactor * 1.5f));
    if (difficulty > 10) difficulty = 10;

    add_common(q, ctx, "q_proc_scout", QuestCategory::Procedural,
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
    const int tx = wrapi(int(std::round(float(ctx.x) + std::cos(angle) * dist)),
                         ctx.gs->mapW);
    const int ty = wrapi(int(std::round(float(ctx.y) + std::sin(angle) * dist)),
                         ctx.gs->mapH);
    const float distFactor = 1.0f + dist / 50.0f;
    const int gold = int(std::round(60.0f * distFactor
        + ctx.rng->next_f01() * 40.0f));
    int difficulty = int(std::ceil(distFactor * 2.0f));
    if (difficulty > 10) difficulty = 10;
    int expire = int(std::round(dist / 8.0f));
    if (expire < 21) expire = 21;

    add_common(q, ctx, "q_proc_sanctuary", QuestCategory::Side,
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

std::vector<int> shuffled_order(Rng& rng) {
    std::vector<int> order = {0, 1, 2, 3, 4, 5, 6};
    for (int i = int(order.size()) - 1; i > 0; --i) {
        const int j = int(rng.next_f01() * float(i + 1));
        std::swap(order[std::size_t(i)], order[std::size_t(j)]);
    }
    return order;
}

std::vector<Quest> generate_for_context(QuestGenCtx& ctx) {
    static constexpr GeneratorFn kGenerators[] = {
        gen_delivery,
        gen_visit,
        gen_destroy,
        gen_protect,
        gen_fetch,
        gen_scout,
        gen_sanctuary,
    };

    const int maxQuests = ctx.isCity
        ? 2 + int(ctx.rng->next_f01() * 3.0f)
        : 1 + int(ctx.rng->next_f01() * 2.0f);
    std::vector<Quest> out;
    out.reserve(std::size_t(maxQuests));

    std::vector<int> order = shuffled_order(*ctx.rng);
    for (int idx : order) {
        if (int(out.size()) >= maxQuests) break;
        Quest q{};
        if (kGenerators[std::size_t(idx)](ctx, q)) {
            out.push_back(std::move(q));
        }
    }

    if (out.empty()) {
        Quest fallback{};
        if (gen_visit(ctx, fallback)) {
            out.push_back(std::move(fallback));
        }
    }
    return out;
}

} // namespace

std::vector<Quest> generate_quests_for_settlement(const Settlement& s,
                                                  const GameState& gs,
                                                  std::uint32_t worldSeed) {
    Rng rng(worldSeed ^ std::uint32_t(s.id) ^ std::uint32_t(gs.worldTime.day));
    QuestGenCtx ctx{};
    ctx.id = s.id;
    ctx.idSegment = std::to_string(s.id);
    ctx.name = s.name;
    ctx.x = s.x;
    ctx.y = s.y;
    ctx.isCity = true;
    ctx.mood = s.mood;
    ctx.kingdomIdx = s.kingdomIdx;
    ctx.eco = &s.eco;
    ctx.gs = &gs;
    ctx.rng = &rng;
    return generate_for_context(ctx);
}

std::vector<Quest> generate_quests_for_village(const Village& v,
                                               const GameState& gs,
                                               std::uint32_t worldSeed) {
    Rng rng(worldSeed ^ std::uint32_t(v.id + 0x6000) ^ std::uint32_t(gs.worldTime.day));
    QuestGenCtx ctx{};
    ctx.id = v.id;
    ctx.idSegment = "v" + std::to_string(v.id);
    ctx.name = v.name;
    ctx.x = v.x;
    ctx.y = v.y;
    ctx.isCity = false;
    ctx.mood = v.mood;
    ctx.kingdomIdx = v.kingdomIdx;
    ctx.eco = &v.eco;
    ctx.gs = &gs;
    ctx.rng = &rng;
    return generate_for_context(ctx);
}

} // namespace sm
