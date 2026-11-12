// The knowledge layer (macro/knowledge.h): terra incognita, the player's
// memory, and sight-as-light. Every assertion is a RELATION derived from the
// optical cost table (macro/optics.h) — never a pinned pixel count: canopy
// must shorten reach because it adds cost, a road must extend it because it
// spends less, a ridge must block because climbing pays per rise, and the
// same eye must see DOWN the same slope for free.
#include "check.h"
#include <memory>

#include "macro/knowledge.h"

#include <cstdint>
#include <vector>

namespace {

constexpr int W = 64, H = 64;
// The test's own eye budget, in open-ground cells. Any positive value works —
// the assertions below are relations against the cost table, not against a
// particular horizon; the app derives its real budget elsewhere.
constexpr float kBudget = 4.0f;

std::unique_ptr<sm::FeatureLayer> open_ground() {
    std::unique_ptr<sm::FeatureLayer> fOwn{new sm::FeatureLayer()};
    sm::FeatureLayer& f = *fOwn;
    f.resize(W, H);
    return fOwn;
}

// Суша под дверью фичи: стена ломтя 1 закрыла сырой set — фикстура ставит
// дорогу той же дверью, что мир (feature_put; воду та спрашивает у карты).
sm::TerrainData all_land() {
    sm::TerrainData td;
    td.width = W;
    td.height = H;
    td.rgba.assign(std::size_t(W) * std::size_t(H) * 4u, 0);
    td.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    for (std::size_t c = 0; c < std::size_t(W) * std::size_t(H); ++c)
        td.rgba[c * 4u + 0] = sm::field_word_of(140.0f / 255.0f);
    return td;
}

int count_state(const sm::KnowledgeLayer& k, std::uint8_t s) {
    int n = 0;
    for (std::uint8_t v : k.cells()) n += (v == s) ? 1 : 0;
    return n;
}

std::uint8_t at(const sm::KnowledgeLayer& k, int x, int y) { return k.at(x, y); }

// Клетка глаза — ИНДЕКСОМ: дверь зрения берёт адрес клетки, а не точку в ней
// (M-462: внутри клетки у мира позиции нет).
std::uint32_t eye(int x, int y) { return sm::cell_of(x, y, W); }

void test_open_sight_opens_a_disc_and_only_once() {
    std::unique_ptr<sm::KnowledgeLayer> kOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& k = *kOwn;
    sm::knowledge_reset(k, W, H);
    sm::SightRuntime rt;
    const std::unique_ptr<sm::FeatureLayer> featOwn = open_ground();
    const sm::FeatureLayer& feat = *featOwn;
    sm::OpticalWorld world{&feat, nullptr, nullptr};

    const int cx = 32, cy = 32;
    CHECK(sm::update_player_sight(k, rt, world, eye(cx, cy), kBudget),
          "the first sweep of a fresh world reports a change");
    CHECK(at(k, cx, cy) == sm::kKnowledgeVisible,
          "the player's own cell is Visible");
    // Straight open ground: a cell strictly inside the budget is Visible,
    // one strictly beyond it is still Unknown.
    const int inside = int(kBudget) - 1;
    const int beyond = int(kBudget) + 2;
    CHECK(at(k, cx + inside, cy) == sm::kKnowledgeVisible,
          "open ground inside the budget is Visible");
    CHECK(at(k, cx + beyond, cy) == sm::kKnowledgeUnknown,
          "open ground beyond the budget stays Unknown");
    CHECK(count_state(k, sm::kKnowledgeVisible) > 1,
          "the sweep opened more than the source cell (it MEASURED)");
    CHECK(count_state(k, sm::kKnowledgeExplored) == 0,
          "a player who never moved has no memory distinct from sight");

    // Same cell again: one integer compare, no change, no revision bump.
    // (Здесь проверялось «шевеление внутри клетки» дробной точкой — у этого
    // кейса больше нет предмета: дверь спрашивают клеткой, и точки внутри неё
    // для мира не существует. Закон остался: тот же адрес — обзора нет.)
    const std::uint32_t rev = k.revision;
    CHECK(!sm::update_player_sight(k, rt, world, eye(cx, cy), kBudget),
          "the same cell asked twice is not a crossing");
    CHECK(k.revision == rev, "no crossing, no revision bump");
    // Сосед — уже другой адрес: обзор обязан пройти заново. И адрес, свёрнутый
    // через шов тора, называет ТУ ЖЕ клетку, что и несвёрнутый.
    CHECK(sm::update_player_sight(k, rt, world, eye(cx + 1, cy), kBudget),
          "a step into the neighbour cell IS a crossing");
    CHECK(!sm::update_player_sight(k, rt, world, eye(cx + 1 + W, cy - H),
                                   kBudget),
          "the same cell named across the torus seam is not a crossing");
}

void test_canopy_shortens_reach() {
    const std::unique_ptr<sm::FeatureLayer> featOwn = open_ground();
    const sm::FeatureLayer& feat = *featOwn;

    std::unique_ptr<sm::KnowledgeLayer> openKOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& openK = *openKOwn;
    sm::knowledge_reset(openK, W, H);
    sm::SightRuntime openRt;
    sm::OpticalWorld openWorld{&feat, nullptr, nullptr};
    sm::update_player_sight(openK, openRt, openWorld, eye(32, 32), kBudget);

    // Full canopy everywhere: every step now costs the open baseline PLUS
    // kCanopyOpticalCost — reach must shrink, by construction of the law.
    const std::vector<float> canopy(std::size_t(W) * H, 1.0f);
    std::unique_ptr<sm::KnowledgeLayer> forestKOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& forestK = *forestKOwn;
    sm::knowledge_reset(forestK, W, H);
    sm::SightRuntime forestRt;
    sm::OpticalWorld forestWorld{&feat, nullptr, &canopy};
    sm::update_player_sight(forestK, forestRt, forestWorld, eye(32, 32),
                            kBudget);

    const int openSeen = count_state(openK, sm::kKnowledgeVisible);
    const int forestSeen = count_state(forestK, sm::kKnowledgeVisible);
    CHECK(openSeen > 0 && forestSeen > 0, "both sweeps measured");
    CHECK(forestSeen < openSeen,
          "canopy strictly shortens sight (it adds optical cost)");
    // A cell open ground can see is smothered by the forest: pick the open
    // ring edge — its optical cost under canopy exceeds the budget whenever
    // dist * (1 + kCanopyOpticalCost) > budget, true for every dist >
    // budget / (1 + kCanopyOpticalCost).
    const int edge = int(kBudget) - 1;
    CHECK(float(edge) * (1.0f + sm::kCanopyOpticalCost) > kBudget,
          "fixture: the probed cell must exceed the canopy budget");
    CHECK(at(openK, 32 + edge, 32) == sm::kKnowledgeVisible
              && at(forestK, 32 + edge, 32) == sm::kKnowledgeUnknown,
          "the forest hides a cell the open plain shows");
}

void test_road_carries_sight_farther() {
    const float roadCost = sm::feature_def(sm::FT_Road).opticalCost;
    // The farthest cell a road reaches but open ground does not:
    // steps * roadCost < budget while steps >= budget.
    const int steps = int((kBudget - 0.05f) / roadCost);
    CHECK(float(steps) >= kBudget && float(steps) * roadCost < kBudget,
          "fixture: the cost table must separate road from open at this range");

    const std::unique_ptr<sm::FeatureLayer> openFeatOwn = open_ground();
    sm::FeatureLayer& openFeat = *openFeatOwn;
    std::unique_ptr<sm::KnowledgeLayer> openKOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& openK = *openKOwn;
    sm::knowledge_reset(openK, W, H);
    sm::SightRuntime openRt;
    sm::OpticalWorld openWorld{&openFeat, nullptr, nullptr};
    sm::update_player_sight(openK, openRt, openWorld, eye(20, 20), kBudget);
    CHECK(at(openK, 20 + steps, 20) == sm::kKnowledgeUnknown,
          "open ground cannot see that far (negative control)");

    const std::unique_ptr<sm::FeatureLayer> roadFeatOwn = open_ground();
    sm::FeatureLayer& roadFeat = *roadFeatOwn;
    const sm::TerrainData roadTd = all_land();
    for (int x = 20; x <= 20 + steps; ++x)
        sm::feature_put(roadFeat, roadTd, x, 20, sm::FT_Road);
    std::unique_ptr<sm::KnowledgeLayer> roadKOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& roadK = *roadKOwn;
    sm::knowledge_reset(roadK, W, H);
    sm::SightRuntime roadRt;
    sm::OpticalWorld roadWorld{&roadFeat, nullptr, nullptr};
    sm::update_player_sight(roadK, roadRt, roadWorld, eye(20, 20), kBudget);
    CHECK(at(roadK, 20 + steps, 20) == sm::kKnowledgeVisible,
          "the same eye sees farther along a road (it spends less budget)");
}

void test_ridge_blocks_and_hilltop_overlooks() {
    const std::unique_ptr<sm::FeatureLayer> featOwn = open_ground();
    const sm::FeatureLayer& feat = *featOwn;
    // The probe sits strictly inside the open budget (the frontier prunes at
    // nd >= budget, so "exactly budget" is already outside) with the wall
    // column between it and the eye.
    const int cx = 32, cy = 32, wallX = cx + 2, probeX = cx + 3;

    // A wall column whose climb cost alone exceeds the whole budget.
    const float rise = (kBudget + 1.0f) / sm::kClimbOpticalCost;
    std::vector<float> ridge(std::size_t(W) * H, 0.0f);
    for (int y = 0; y < H; ++y)
        ridge[std::size_t(y) * W + std::size_t(wallX)] = rise;

    // Negative control first, and asserted: on FLAT ground the probe is seen.
    std::unique_ptr<sm::KnowledgeLayer> flatKOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& flatK = *flatKOwn;
    sm::knowledge_reset(flatK, W, H);
    sm::SightRuntime flatRt;
    const std::vector<float> flat(std::size_t(W) * H, 0.0f);
    sm::OpticalWorld flatWorld{&feat, &flat, nullptr};
    sm::update_player_sight(flatK, flatRt, flatWorld, eye(cx, cy), kBudget);
    CHECK(at(flatK, probeX, cy) == sm::kKnowledgeVisible,
          "flat ground: the probe cell is inside the open budget");

    // The ridge wall: the valley eye cannot climb it, so the far side —
    // and the crest itself — stay Unknown.
    std::unique_ptr<sm::KnowledgeLayer> valleyKOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& valleyK = *valleyKOwn;
    sm::knowledge_reset(valleyK, W, H);
    sm::SightRuntime valleyRt;
    sm::OpticalWorld ridgeWorld{&feat, &ridge, nullptr};
    sm::update_player_sight(valleyK, valleyRt, ridgeWorld, eye(cx, cy),
                            kBudget);
    CHECK(at(valleyK, probeX, cy) == sm::kKnowledgeUnknown,
          "a ridge walls sight off the valley floor");
    CHECK(at(valleyK, wallX, cy) == sm::kKnowledgeUnknown,
          "the crest itself is unseen from below (climbing pays per rise)");

    // The same wall from ON TOP: descent is free, so the hilltop eye sees
    // the valley cell the valley eye could not answer with.
    std::unique_ptr<sm::KnowledgeLayer> crestKOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& crestK = *crestKOwn;
    sm::knowledge_reset(crestK, W, H);
    sm::SightRuntime crestRt;
    sm::update_player_sight(crestK, crestRt, ridgeWorld, eye(wallX, cy),
                            kBudget);
    CHECK(at(crestK, cx, cy) == sm::kKnowledgeVisible,
          "the hilltop overlooks the valley (downhill spends nothing extra)");
}

void test_move_decays_visible_to_explored() {
    std::unique_ptr<sm::KnowledgeLayer> kOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& k = *kOwn;
    sm::knowledge_reset(k, W, H);
    sm::SightRuntime rt;
    const std::unique_ptr<sm::FeatureLayer> featOwn = open_ground();
    const sm::FeatureLayer& feat = *featOwn;
    sm::OpticalWorld world{&feat, nullptr, nullptr};

    sm::update_player_sight(k, rt, world, eye(10, 10), kBudget);
    const std::uint32_t revAfterFirst = k.revision;
    CHECK(sm::update_player_sight(k, rt, world, eye(40, 10), kBudget),
          "a crossing reports a change");
    CHECK(k.revision > revAfterFirst, "a crossing bumps the revision");
    CHECK(at(k, 10, 10) == sm::kKnowledgeExplored,
          "yesterday's sight decays to memory, not to darkness");
    CHECK(at(k, 40, 10) == sm::kKnowledgeVisible,
          "today's sight is Visible at the new cell");
    CHECK(count_state(k, sm::kKnowledgeExplored) > 0
              && count_state(k, sm::kKnowledgeVisible) > 0,
          "both states coexist after a move (it MEASURED)");
}

void test_torus_wrap_and_fail_closed_door() {
    std::unique_ptr<sm::KnowledgeLayer> kOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& k = *kOwn;
    sm::knowledge_reset(k, W, H);
    sm::SightRuntime rt;
    const std::unique_ptr<sm::FeatureLayer> featOwn = open_ground();
    const sm::FeatureLayer& feat = *featOwn;
    sm::OpticalWorld world{&feat, nullptr, nullptr};

    sm::update_player_sight(k, rt, world, eye(0, 0), kBudget);
    CHECK(at(k, W - 1, H - 1) == sm::kKnowledgeVisible,
          "sight wraps the torus like every other layer");

    std::unique_ptr<sm::KnowledgeLayer> emptyOwn{new sm::KnowledgeLayer()};
    const sm::KnowledgeLayer& empty = *emptyOwn;
    CHECK(empty.at(5, 5) == sm::kKnowledgeUnknown,
          "an absent grid answers Unknown - fail closed, nothing shows");
}

} // namespace

// ── ДИАГОНАЛЬ В ЗРЕНИИ СТОИТ ПО ПАРЕ ИГРЫ (M-463) ───────────────────────
// Зрение и ход обязаны одинаково понимать, что такое «N клеток»: шаг взгляда
// по диагонали длиннее прямого в `kDiagonalStepLen@src/core/torus.h` — той
// же парой, какой платят ноги. Бюджет взят из самой пары: чуть меньше пяти
// диагональных шагов. При прежней цене √2 пятая диагональ была бы видна.
void test_sight_prices_a_diagonal_by_the_game_pair() {
    std::unique_ptr<sm::KnowledgeLayer> kOwn{new sm::KnowledgeLayer()};
    sm::KnowledgeLayer& k = *kOwn;
    sm::knowledge_reset(k, W, H);
    sm::SightRuntime rt;
    const std::unique_ptr<sm::FeatureLayer> featOwn = open_ground();
    const sm::FeatureLayer& feat = *featOwn;
    sm::OpticalWorld world{&feat, nullptr, nullptr};
    const float budget = 5.0f * sm::kDiagonalStepLen - 0.1f;
    sm::update_player_sight(k, rt, world, eye(32, 32), budget);
    CHECK(at(k, 32 + 7, 32) == sm::kKnowledgeVisible,
          "семь клеток прямо — внутри бюджета");
    CHECK(at(k, 32 + 4, 32 + 4) == sm::kKnowledgeVisible,
          "четыре диагонали стоят шести прямых — внутри");
    CHECK(at(k, 32 + 5, 32 + 5) == sm::kKnowledgeUnknown,
          "ПЯТАЯ ДИАГОНАЛЬ — СНАРУЖИ: взгляд платит за диагональ парой игры");
}

int main() {
    test_open_sight_opens_a_disc_and_only_once();
    test_sight_prices_a_diagonal_by_the_game_pair();
    test_canopy_shortens_reach();
    test_road_carries_sight_farther();
    test_ridge_blocks_and_hilltop_overlooks();
    test_move_decays_visible_to_explored();
    test_torus_wrap_and_fail_closed_door();
    return sm::test::report("knowledge_test");
}
