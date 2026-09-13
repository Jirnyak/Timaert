// THE PALISADE — the village's own wall, and it is not a curtain in brown.
//
// Until 2026-09-13 one primitive with a `WallStyle{height, towers}` dial raised
// the city's curtain, the upper quarter's enceinte AND the village's wall. The
// price was visible from the road: a hamlet of four hundred souls stood behind
// EIGHT METRES OF STONE, flanked by round stone towers and entered under a
// stone arch, because it was the city's wall with a number turned down. Owner's
// ruling that day: the city gets its own wall generator and the village gets a
// stockade of its own. Two modules that look alike in places are not a debt
// (AGENTS.md, data-oriented law 5); one module with dials for three different
// things is.
//
// So what a village actually raises is here, and nothing about it is derived
// from masonry:
//
//   · A LOG, not a course. The village fells the trees it stands among and
//     sets the trunks upright, shoulder to shoulder — hence round bodies at
//     one trunk-width spacing rather than long straight chords. Each trunk is
//     driven into its OWN patch of earth, which is why a stockade drapes over
//     broken ground without the gaps a stone chord leaves hanging at one end.
//   · A GATE OF TIMBER FRAMING, not an arch. Two heavier posts and a beam laid
//     across them — and the frame STANDS PROUD of the wall it pierces, because
//     the clear a gateway owes a rider (sub/height.h kGateClearM) is more than a
//     stockade is tall. That is not a compromise; it is why a real stockade
//     gate is the tallest thing in a village.
//   · A WATCH PLATFORM over that gate, and nowhere else. A village keeps no
//     garrison to man a circuit of towers; it keeps one pair of eyes, and it
//     keeps them where the road comes in.
#pragma once
#include "sub/gens/kit/outline.h"
#include "sub/map_data.h"

namespace sm::sub {

// Raise the stockade on `outline`. Returns how many gateways it framed,
// writing up to `maxGates` of them so the caller can route its field tracks to
// a real way out.
int stamp_palisade(SubworldMapData& out, const kit::Outline& outline,
                   kit::WallGate* gates, int maxGates);

} // namespace sm::sub
