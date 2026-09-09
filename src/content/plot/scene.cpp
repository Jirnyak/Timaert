#include "content/plot/scene.h"

namespace sm::content {
namespace {

// The ambush: SEVEN, staggered up both sides of the road so they arrive as
// a wave rather than a clump. They wait 262-430 tiles ahead — past the 200
// every other creature in the world detects at, so nothing jumps the player
// off the spawn and he gets his road and his forest first. Their own row
// (npc.h RoadAmbusher) is what sees him there and brings them.
constexpr ScenePlacement kPrologueAmbush[] = {
    {"road_ambusher", "Ambusher", "bandits", 3, -52.0f, -262.0f},
    {"road_ambusher", "Ambusher", "bandits", 3,  44.0f, -288.0f},
    {"road_ambusher", "Ambusher", "bandits", 3, -18.0f, -305.0f},
    {"road_ambusher", "Ambusher", "bandits", 3,  62.0f, -330.0f},
    {"road_ambusher", "Ambusher", "bandits", 3, -70.0f, -352.0f},
    {"road_ambusher", "Ambusher", "bandits", 3,  26.0f, -395.0f},
    {"road_ambusher", "Ambusher", "bandits", 3, -38.0f, -430.0f},
};

constexpr SceneDef kPrologueScene = {
    "prologue",
    "prologue_road",
    /*floorHeight*/0.55f,
    "Steel glints between the trees.",
    /*holdsMap*/true,
    kPrologueAmbush,
    sizeof(kPrologueAmbush) / sizeof(kPrologueAmbush[0]),
};

} // namespace

const SceneDef& prologue_scene() {
    return kPrologueScene;
}

} // namespace sm::content
