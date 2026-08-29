#include "sub/map_factory.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace sm::sub {

namespace {

struct Key {
    std::uint32_t seed;
    SubworldMode  mode;
    bool operator==(const Key& o) const noexcept {
        return seed == o.seed && mode == o.mode;
    }
};

struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
        return std::size_t(k.seed) * 131u + std::size_t(k.mode);
    }
};

std::unordered_map<Key, std::shared_ptr<const SavedSubworld>, KeyHash>& cache() {
    static std::unordered_map<Key, std::shared_ptr<const SavedSubworld>, KeyHash> g;
    return g;
}

std::mutex& cache_mutex() {
    static std::mutex m;
    return m;
}

} // namespace

SavedSubworld snapshot_subworld(std::uint32_t seed, SubworldMode mode,
                                const SubworldMapData& src) {
    SavedSubworld out;
    out.seed = seed;
    out.mode = mode;
    out.heightmap.resize(src.heightmap.size());
    // Heightmap range is [0, 2.5] now that mountain peaks may exceed 1.0
    // (apply_mountain_ridges allows peak = macroH + 1.0). Quantise into
    // 16-bit with that range so saved/restored peaks aren't truncated.
    constexpr float kHmaxQuant = 2.5f;
    for (std::size_t i = 0; i < src.heightmap.size(); ++i) {
        float v = std::clamp(src.heightmap[i] / kHmaxQuant, 0.0f, 1.0f);
        out.heightmap[i] = std::uint16_t(v * 65535.0f + 0.5f);
    }
    out.structures = src.structures;
    return out;
}

void restore_into(const SavedSubworld& saved, SubworldMapData& fresh) {
    // Restore heightmap if sizes match. A mismatch means the saved relief
    // CANNOT be applied — the fresh generation stands — and that refusal is
    // said out loud (CANON S26): a save that quietly loses the player's
    // remembered ground is a save nobody can debug.
    if (saved.heightmap.size() == fresh.heightmap.size()) {
        constexpr float kHmaxQuant = 2.5f;
        for (std::size_t i = 0; i < saved.heightmap.size(); ++i) {
            fresh.heightmap[i] = float(saved.heightmap[i]) / 65535.0f * kHmaxQuant;
        }
    } else {
        std::fprintf(stderr,
                     "[map_factory] saved heightmap size %zu != fresh %zu "
                     "(seed 0x%08X) — saved relief NOT restored\n",
                     saved.heightmap.size(), fresh.heightmap.size(),
                     saved.seed);
    }

    // Group structures by kind on both sides — EVERY kind the prop table
    // knows. This used to be a literal 5 (Tree/Rock/House/Wall/Bridge), so
    // every prop added after it — crops, field fences, furniture, and now
    // doors and lanterns — was silently DROPPED from a revisited cell: walk
    // out of a town and back and its doors were gone. A count that must
    // track a table is read from the table.
    constexpr int kKinds = Structure::kKindCount;
    std::array<std::vector<const Structure*>, kKinds> savedByKind{};
    std::array<std::vector<const Structure*>, kKinds> freshByKind{};
    for (const auto& s : saved.structures) {
        if (s.kind < kKinds) {
            savedByKind[s.kind].push_back(&s);
        } else {
            // A kind past the table is data the merge cannot place — refuse
            // OUT LOUD (CANON S26), never let a saved structure vanish
            // without a word.
            std::fprintf(stderr,
                         "[map_factory] saved structure kind %d out of range "
                         "(%d kinds, seed 0x%08X) — dropped from restore\n",
                         int(s.kind), kKinds, saved.seed);
        }
    }
    for (const auto& s : fresh.structures) {
        if (s.kind < kKinds) freshByKind[s.kind].push_back(&s);
    }

    std::vector<Structure> merged;
    merged.reserve(saved.structures.size() + fresh.structures.size());

    for (int k = 0; k < kKinds; ++k) {
        const auto& sv = savedByKind[k];
        const auto& fr = freshByKind[k];
        const std::size_t common = std::min(sv.size(), fr.size());
        for (std::size_t i = 0; i < common; ++i) merged.push_back(*sv[i]);
        if (sv.size() < fr.size()) {
            // Append additional fresh structures (new in this generation).
            for (std::size_t i = common; i < fr.size(); ++i) merged.push_back(*fr[i]);
        } else {
            // Mark the surplus saved structures as decayed.
            for (std::size_t i = common; i < sv.size(); ++i) {
                Structure s = *sv[i];
                mark_structure_decayed(s);
                merged.push_back(s);
            }
        }
    }

    fresh.structures = std::move(merged);
}

void store_saved_subworld(const SavedSubworld& s) {
    std::lock_guard<std::mutex> lock(cache_mutex());
    cache()[Key{s.seed, s.mode}] = std::make_shared<SavedSubworld>(s);
}

void store_saved_subworld(SavedSubworld&& s) {
    const Key k{s.seed, s.mode};
    std::lock_guard<std::mutex> lock(cache_mutex());
    cache()[k] = std::make_shared<SavedSubworld>(std::move(s));
}

std::shared_ptr<const SavedSubworld> find_saved_subworld_ref(std::uint32_t seed,
                                                             SubworldMode mode) {
    std::lock_guard<std::mutex> lock(cache_mutex());
    auto& c = cache();
    auto it = c.find(Key{seed, mode});
    return it == c.end() ? nullptr : it->second;
}

const SavedSubworld* find_saved_subworld(std::uint32_t seed, SubworldMode mode) {
    static thread_local std::shared_ptr<const SavedSubworld> pinned;
    pinned = find_saved_subworld_ref(seed, mode);
    return pinned.get();
}

void clear_saved_subworlds() {
    std::lock_guard<std::mutex> lock(cache_mutex());
    cache().clear();
}

} // namespace sm::sub
