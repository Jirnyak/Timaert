// The knowledge layer (macro/knowledge.h): terra incognita, the player's
// memory, and sight-as-light. Every assertion is a RELATION derived from the
// optical cost table (macro/optics.h) — never a pinned pixel count: canopy
// must shorten reach because it adds cost, a road must extend it because it
// spends less, a ridge must block because climbing pays per rise, and the
// same eye must see DOWN the same slope for free.
#include "check.h"

#include "macro/knowledge.h"

#include <cstdint>
#include <vector>

namespace {

constexpr int W = 64, H = 64;
// The test's own eye budget, in open-ground cells. Any positive value works —
// the assertions below are relations against the cost table, not against a
// particular horizon; the app derives its real budget elsewhere.
constexpr float kBudget = 4.0f;

sm::FeatureLayer open_ground() {
    sm::FeatureLayer f;
    f.resize(W, H);
    return f;
}

int count_state(const sm::KnowledgeLayer& k, std::uint8_t s) {
    int n = 0;
    for (std::uint8_t v : k.data) n += (v == s) ? 1 : 0;
    return n;
}

std::uint8_t at(const sm::KnowledgeLayer& k, int x, int y) { return k.at(x, y); }

void test_open_sight_opens_a_disc_and_only_once() {
    sm::KnowledgeLayer k;
    sm::knowledge_reset(k, W, H);
    sm::SightRuntime rt;
    const sm::FeatureLayer feat = open_ground();
    sm::OpticalWorld world{&feat, nullptr, nullptr};

    const int cx = 32, cy = 32;
    CHECK(sm::update_player_sight(k, rt, world, float(cx) + 0.5f,
                                  float(cy) + 0.5f, kBudget),
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
    const std::uint32_t rev = k.revision;
    CHECK(!sm::update_player_sight(k, rt, world, float(cx) + 0.7f,
                                   float(cy) + 0.2f, kBudget),
          "a within-cell wiggle is not a crossing");
    CHECK(k.revision == rev, "no crossing, no revision bump");
}

void test_canopy_shortens_reach() {
    const sm::FeatureLayer feat = open_ground();

    sm::KnowledgeLayer openK;
    sm::knowledge_reset(openK, W, H);
    sm::SightRuntime openRt;
    sm::OpticalWorld openWorld{&feat, nullptr, nullptr};
    sm::update_player_sight(openK, openRt, openWorld, 32.5f, 32.5f, kBudget);

    // Full canopy everywhere: every step now costs the open baseline PLUS
    // kCanopyOpticalCost — reach must shrink, by construction of the law.
    const std::vector<float> canopy(std::size_t(W) * H, 1.0f);
    sm::KnowledgeLayer forestK;
    sm::knowledge_reset(forestK, W, H);
    sm::SightRuntime forestRt;
    sm::OpticalWorld forestWorld{&feat, nullptr, &canopy};
    sm::update_player_sight(forestK, forestRt, forestWorld, 32.5f, 32.5f,
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

    sm::FeatureLayer openFeat = open_ground();
    sm::KnowledgeLayer openK;
    sm::knowledge_reset(openK, W, H);
    sm::SightRuntime openRt;
    sm::OpticalWorld openWorld{&openFeat, nullptr, nullptr};
    sm::update_player_sight(openK, openRt, openWorld, 20.5f, 20.5f, kBudget);
    CHECK(at(openK, 20 + steps, 20) == sm::kKnowledgeUnknown,
          "open ground cannot see that far (negative control)");

    sm::FeatureLayer roadFeat = open_ground();
    for (int x = 20; x <= 20 + steps; ++x)
        roadFeat.set(x, 20, sm::FT_Road);
    sm::KnowledgeLayer roadK;
    sm::knowledge_reset(roadK, W, H);
    sm::SightRuntime roadRt;
    sm::OpticalWorld roadWorld{&roadFeat, nullptr, nullptr};
    sm::update_player_sight(roadK, roadRt, roadWorld, 20.5f, 20.5f, kBudget);
    CHECK(at(roadK, 20 + steps, 20) == sm::kKnowledgeVisible,
          "the same eye sees farther along a road (it spends less budget)");
}

void test_ridge_blocks_and_hilltop_overlooks() {
    const sm::FeatureLayer feat = open_ground();
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
    sm::KnowledgeLayer flatK;
    sm::knowledge_reset(flatK, W, H);
    sm::SightRuntime flatRt;
    const std::vector<float> flat(std::size_t(W) * H, 0.0f);
    sm::OpticalWorld flatWorld{&feat, &flat, nullptr};
    sm::update_player_sight(flatK, flatRt, flatWorld, float(cx) + 0.5f,
                            float(cy) + 0.5f, kBudget);
    CHECK(at(flatK, probeX, cy) == sm::kKnowledgeVisible,
          "flat ground: the probe cell is inside the open budget");

    // The ridge wall: the valley eye cannot climb it, so the far side —
    // and the crest itself — stay Unknown.
    sm::KnowledgeLayer valleyK;
    sm::knowledge_reset(valleyK, W, H);
    sm::SightRuntime valleyRt;
    sm::OpticalWorld ridgeWorld{&feat, &ridge, nullptr};
    sm::update_player_sight(valleyK, valleyRt, ridgeWorld, float(cx) + 0.5f,
                            float(cy) + 0.5f, kBudget);
    CHECK(at(valleyK, probeX, cy) == sm::kKnowledgeUnknown,
          "a ridge walls sight off the valley floor");
    CHECK(at(valleyK, wallX, cy) == sm::kKnowledgeUnknown,
          "the crest itself is unseen from below (climbing pays per rise)");

    // The same wall from ON TOP: descent is free, so the hilltop eye sees
    // the valley cell the valley eye could not answer with.
    sm::KnowledgeLayer crestK;
    sm::knowledge_reset(crestK, W, H);
    sm::SightRuntime crestRt;
    sm::update_player_sight(crestK, crestRt, ridgeWorld, float(wallX) + 0.5f,
                            float(cy) + 0.5f, kBudget);
    CHECK(at(crestK, cx, cy) == sm::kKnowledgeVisible,
          "the hilltop overlooks the valley (downhill spends nothing extra)");
}

void test_move_decays_visible_to_explored() {
    sm::KnowledgeLayer k;
    sm::knowledge_reset(k, W, H);
    sm::SightRuntime rt;
    const sm::FeatureLayer feat = open_ground();
    sm::OpticalWorld world{&feat, nullptr, nullptr};

    sm::update_player_sight(k, rt, world, 10.5f, 10.5f, kBudget);
    const std::uint32_t revAfterFirst = k.revision;
    CHECK(sm::update_player_sight(k, rt, world, 40.5f, 10.5f, kBudget),
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

void test_reveal_area_marks_memory_never_sight() {
    sm::KnowledgeLayer k;
    sm::knowledge_reset(k, W, H);
    sm::SightRuntime rt;
    const sm::FeatureLayer feat = open_ground();
    sm::OpticalWorld world{&feat, nullptr, nullptr};

    sm::update_player_sight(k, rt, world, 10.5f, 10.5f, kBudget);
    const int visibleBefore = count_state(k, sm::kKnowledgeVisible);

    sm::OpticalScratch scratch;
    sm::reveal_area(k, world, scratch, 50.5f, 50.5f, kBudget);
    CHECK(at(k, 50, 50) == sm::kKnowledgeExplored,
          "a revealed area reads as memory");
    CHECK(count_state(k, sm::kKnowledgeExplored) > 0, "the reveal MEASURED");
    CHECK(count_state(k, sm::kKnowledgeVisible) == visibleBefore,
          "a reveal never fabricates sight");

    // Revealing over the player's live sight must not downgrade it.
    sm::reveal_area(k, world, scratch, 10.5f, 10.5f, kBudget);
    CHECK(at(k, 10, 10) == sm::kKnowledgeVisible,
          "revealing what is already in sight leaves it in sight");
}

void test_torus_wrap_and_fail_closed_door() {
    sm::KnowledgeLayer k;
    sm::knowledge_reset(k, W, H);
    sm::SightRuntime rt;
    const sm::FeatureLayer feat = open_ground();
    sm::OpticalWorld world{&feat, nullptr, nullptr};

    sm::update_player_sight(k, rt, world, 0.5f, 0.5f, kBudget);
    CHECK(at(k, W - 1, H - 1) == sm::kKnowledgeVisible,
          "sight wraps the torus like every other layer");

    const sm::KnowledgeLayer empty;
    CHECK(empty.at(5, 5) == sm::kKnowledgeUnknown,
          "an absent grid answers Unknown - fail closed, nothing shows");
}

} // namespace

int main() {
    test_open_sight_opens_a_disc_and_only_once();
    test_canopy_shortens_reach();
    test_road_carries_sight_farther();
    test_ridge_blocks_and_hilltop_overlooks();
    test_move_decays_visible_to_explored();
    test_reveal_area_marks_memory_never_sight();
    test_torus_wrap_and_fail_closed_door();
    return sm::test::report("knowledge_test");
}
