#include "sub/map_factory.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

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

std::unordered_map<Key, SavedSubworld, KeyHash>& cache() {
    static std::unordered_map<Key, SavedSubworld, KeyHash> g;
    return g;
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
        out.heightmap[i] = std::uint16_t(std::lround(v * 65535.0f));
    }
    out.structures = src.structures;
    return out;
}

void restore_into(const SavedSubworld& saved, SubworldMapData& fresh) {
    // Restore heightmap if sizes match.
    if (saved.heightmap.size() == fresh.heightmap.size()) {
        constexpr float kHmaxQuant = 2.5f;
        for (std::size_t i = 0; i < saved.heightmap.size(); ++i) {
            fresh.heightmap[i] = float(saved.heightmap[i]) / 65535.0f * kHmaxQuant;
        }
    }

    // Group structures by kind on both sides.
    constexpr int kKinds = 4; // Tree, Rock, House, Wall
    std::array<std::vector<const Structure*>, kKinds> savedByKind{};
    std::array<std::vector<const Structure*>, kKinds> freshByKind{};
    for (const auto& s : saved.structures) {
        if (s.kind < kKinds) savedByKind[s.kind].push_back(&s);
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
    cache()[Key{s.seed, s.mode}] = s;
}

const SavedSubworld* find_saved_subworld(std::uint32_t seed, SubworldMode mode) {
    auto& c = cache();
    auto it = c.find(Key{seed, mode});
    return it == c.end() ? nullptr : &it->second;
}

void clear_saved_subworlds() { cache().clear(); }

} // namespace sm::sub
