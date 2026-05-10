#include "content/quests/procedural.h"
#include "core/rng.h"
#include <cstddef>
#include <utility>

namespace sm {

static std::string make_quest_id(const Settlement& s, int day, int n) {
    return "q_" + std::to_string(s.id) + "_d" + std::to_string(day) + "_" + std::to_string(n);
}

std::vector<Quest> generate_quests_for_settlement(const Settlement& s,
                                                  const GameState& gs,
                                                  std::uint32_t worldSeed) {
    Rng r(worldSeed ^ std::uint32_t(s.id) ^ std::uint32_t(gs.worldTime.day));
    std::vector<Quest> out;
    int n = 1 + int(r.next_u32() % 3u);
    constexpr const char* kDeliveryItems[] = {"mat_wood", "mat_iron", "mat_herb"};
    for (int i = 0; i < n; ++i) {
        Quest q;
        q.id = make_quest_id(s, gs.worldTime.day, i);
        q.giverSettlementId = s.id;
        q.category = QuestCategory::Procedural;
        q.difficulty = 1 + int(r.next_u32() % 5u);
        int kind = int(r.next_u32() % 3u);
        if (kind == 0) {
            q.title = "Keep Watch";
            q.description = "Remain at the settlement through the next watch.";
            Objective o{}; o.kind = ObjectiveKind::WaitAt;
            o.ix = s.x;
            o.iy = s.y;
            o.radius = 5.0f;
            o.hoursRequired = 2 + int(r.next_u32() % 4u);
            q.objectives.push_back(o);
            Reward rw{}; rw.kind = RewardKind::Gold; rw.amount = 80 + q.difficulty * 30;
            q.rewards.push_back(rw);
        } else if (kind == 1) {
            q.title = "Deliver Materials";
            q.description = "Bring requested materials to the settlement stores.";
            Objective o{}; o.kind = ObjectiveKind::DeliverItems;
            o.itemId = kDeliveryItems[std::size_t(r.next_u32() % 3u)];
            o.quantity = 2 + int(r.next_u32() % 4u);
            o.targetSettlementId = s.id;
            q.objectives.push_back(o);
            Reward rw{}; rw.kind = RewardKind::Gold; rw.amount = 50 + q.difficulty * 20;
            q.rewards.push_back(rw);
        } else {
            q.title = "Scout the Frontier";
            q.description = "Visit a wild frontier cell.";
            Objective o{}; o.kind = ObjectiveKind::VisitCell;
            o.ix = (s.x + 40 + int(r.next_u32() % 60u)) % gs.mapW;
            o.iy = (s.y + 30 + int(r.next_u32() % 50u)) % gs.mapH;
            o.radius = 4.0f;
            q.objectives.push_back(o);
            Reward rw{}; rw.kind = RewardKind::Xp; rw.amount = 60 + q.difficulty * 25;
            q.rewards.push_back(rw);
        }
        out.push_back(std::move(q));
    }
    return out;
}

} // namespace sm
