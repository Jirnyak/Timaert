// Subworld save/regen cache — port of subworld/map-factory.ts.
// In-memory snapshot store keyed by (seed, mode); on revisit, the saved
// heightmap and structures are merged into a freshly generated map so
// player-induced changes (felled trees, abandoned houses) persist.
//
// Diff semantics by structure Kind, mirroring the TS factory:
//   saved.count <= fresh.count → keep saved positions, append the
//                                additional fresh structures.
//   saved.count >  fresh.count → keep matching prefix, mark the surplus
//                                as Withered (Tree) or Abandoned (other).
#pragma once
#include <cstdint>
#include <cmath>
#include <memory>
#include "sub/map_data.h"

namespace sm::sub {

struct SavedSubworld {
    std::uint32_t seed = 0;
    SubworldMode  mode = SubworldMode::Open;
    std::vector<std::uint16_t> heightmap;   // quantised to u16
    std::vector<Structure>     structures;
};

// Compact "state" tag overlaid on Structure::height sign bit since
// Structure has no state field yet. Negative height => abandoned/withered.
inline bool structure_is_decayed(const Structure& s) { return s.height < 0.0f; }
inline void mark_structure_decayed(Structure& s)     { s.height = -std::fabs(s.height); }

// Snapshot the current subworld for later restoration.
SavedSubworld snapshot_subworld(std::uint32_t seed, SubworldMode mode,
                                const SubworldMapData& src);

// Merge a saved snapshot into a freshly generated map. Heightmap is
// restored verbatim; structures are diffed by Kind as documented above.
void restore_into(const SavedSubworld& saved, SubworldMapData& fresh);

// Per-session in-memory cache — never serialized: the subworld is a
// PROJECTION of the macro truth (CANON S21), and what must outlive a scene
// is written back up to the macro layer, not snapshotted down here.
void store_saved_subworld(const SavedSubworld& s);
void store_saved_subworld(SavedSubworld&& s);
std::shared_ptr<const SavedSubworld> find_saved_subworld_ref(std::uint32_t seed,
                                                             SubworldMode mode);
const SavedSubworld* find_saved_subworld(std::uint32_t seed, SubworldMode mode);
void clear_saved_subworlds();

} // namespace sm::sub
