// Authored SCENES — who stands where when a pocket rises.
//
// A story is a run of slides (intro.h); a SCENE is a run of PLACEMENTS, and
// the same rule holds for both: a new scene is a new table, never a new kind
// of code. The demo's opening used to be a function in main.cpp with seven
// pairs of coordinates typed into it; this is that content, written as
// content, so the ruins and the closing hook are rows beside it rather than
// three more functions.
//
// Layering: this file names things, it does not include them. The pocket is
// named by its registry id (sub/dgn resolves it — content never includes
// sub/), the bodies by their rows of the one creature table, the faction by
// its registry id. Nothing here knows an ordinal.
#pragma once
#include <cstddef>

namespace sm::content {

// One body, one spot. The offset is from the arrival tile the pocket's own
// module seats the player on, in scene tiles: the author places the ambush
// relative to the man walking into it, not to a cell corner.
struct ScenePlacement {
    const char* npcId;       // a row of the one creature table (npc.h id)
    const char* displayName; // status-line label; the row still names the body
    const char* factionId;   // faction registry id; nullptr = the spawn default
    int         level;
    float       dx, dy;
};

struct SceneDef {
    const char* id;
    // WHICH pocket rises (sub/dgn/dispatch kind id: "prologue_road", …).
    const char* pocketId;
    // The pocket's floor in the normalised [0,1] height range the one
    // heightmap law reads.
    float       floorHeight;
    // What the session feed says as it opens; nullptr = it says nothing.
    const char* feedLine;
    // Whether the world's optical sweep is HELD while this scene plays — a
    // map filling in behind an opening scene is an immersion leak (owner,
    // 2026-09-09). Released by the story beat that ends the scene.
    bool        holdsMap;
    const ScenePlacement* placements;
    std::size_t placementCount;
};

// The demo's opening (release.md §3 scene 1): the forest road, and the seven
// who have been watching it. Their count is a LAW, not a difficulty knob —
// the pocket's only exit is the player's death, so an ambush he could win
// would strand him in the opening forever.
const SceneDef& prologue_scene();

} // namespace sm::content
