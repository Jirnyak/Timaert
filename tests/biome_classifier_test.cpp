// Pins the elevation-aware biome classifier `sm::biome_at`, the CPU mirror of
// the shader's bt_biome. Mountains are a biome classified by elevation (exactly
// like Water is classified by being below sea level), NOT a feature — this test
// guards the Water / Mountain / climate cascade and its precedence.
#include "check.h"

#include "macro/biomes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kSea = 0.40f;
constexpr float kMtn = sm::kMountainBiomeLevel; // 0.75f

// Below sea level is always Water, regardless of climate or how far below.
void test_water_is_classified_by_sea_level() {
    CHECK(sm::biome_at(0.0f, 0.0f, 0.00f, kSea, kMtn) == sm::Water,
           "deep water below sea level is Water");
    CHECK(sm::biome_at(1.0f, 1.0f, 0.39f, kSea, kMtn) == sm::Water,
           "just below sea level is Water even for hot/wet climate");
    // The boundary is inclusive-land: exactly at sea level is NOT water.
    CHECK(sm::biome_at(0.5f, 0.5f, 0.40f, kSea, kMtn) != sm::Water,
           "exactly at sea level is land, not Water");
}

// At or above the massif line is always Mountain, overriding the climate matrix.
void test_mountain_is_classified_by_elevation() {
    CHECK(sm::biome_at(0.0f, 0.0f, 0.75f, kSea, kMtn) == sm::Mountain,
           "exactly at mountain level is Mountain");
    CHECK(sm::biome_at(1.0f, 1.0f, 0.90f, kSea, kMtn) == sm::Mountain,
           "high elevation is Mountain regardless of hot/wet climate");
    CHECK(sm::biome_at(0.0f, 1.0f, 1.00f, kSea, kMtn) == sm::Mountain,
           "peak elevation is Mountain regardless of cold/wet climate");
    // Just below the line falls back to the climate matrix, not Mountain.
    CHECK(sm::biome_at(0.5f, 0.5f, 0.74f, kSea, kMtn) != sm::Mountain,
           "just below mountain level is a climate biome, not Mountain");
}

// Between sea level and the massif line, biome_at delegates verbatim to the
// 3x3 climate matrix — no elevation influence in that band.
void test_climate_band_delegates_to_matrix() {
    const float h = 0.55f; // land, below the mountain line
    CHECK(sm::biome_at(0.0f, 0.0f, h, kSea, kMtn) == sm::biome_from_climate(0.0f, 0.0f),
           "cold/dry matches climate matrix (Tundra)");
    CHECK(sm::biome_at(1.0f, 1.0f, h, kSea, kMtn) == sm::biome_from_climate(1.0f, 1.0f),
           "hot/wet matches climate matrix (Tropics)");
    CHECK(sm::biome_at(0.5f, 0.5f, h, kSea, kMtn) == sm::biome_from_climate(0.5f, 0.5f),
           "temperate/mid matches climate matrix (Meadow)");
    // Spot-check the concrete corners so a matrix regression is caught here too.
    CHECK(sm::biome_at(0.0f, 0.0f, h, kSea, kMtn) == sm::Tundra, "cold/dry is Tundra");
    CHECK(sm::biome_at(1.0f, 1.0f, h, kSea, kMtn) == sm::Tropics, "hot/wet is Tropics");
    CHECK(sm::biome_at(0.5f, 0.5f, h, kSea, kMtn) == sm::Meadow, "temperate/mid is Meadow");
}

// Water is checked before Mountain: a degenerate map where the mountain line
// sits below sea level must still yield Water below that sea level.
void test_water_precedence_over_mountain() {
    CHECK(sm::biome_at(0.5f, 0.5f, 0.30f, 0.50f, 0.20f) == sm::Water,
           "below sea level wins even when also above a lower mountain line");
    CHECK(sm::biome_at(0.5f, 0.5f, 0.60f, 0.50f, 0.20f) == sm::Mountain,
           "above sea level and above mountain line is Mountain");
}

// The authority's per-axis banding: int(x * 2 + 0.5), clamped to [0, 2].
int authority_axis(float x01) {
    int band = int(x01 * 2.0f + 0.5f);
    if (band > 2) band = 2;
    if (band < 0) band = 0;
    return band;
}

// C++ mirror of the GLSL expression in macro.frag bt_biome:
// int(clamp(floor(x * 2.0 + 0.5), 0.0, 2.0)).
int shader_axis(float x01) {
    float band = std::floor(x01 * 2.0f + 0.5f);
    if (band > 2.0f) band = 2.0f;
    if (band < 0.0f) band = 0.0f;
    return int(band);
}

// The 3x3 enum is row-major, so `Biome id == row * 3 + col` — the identity the
// river edge-detect byte path (map_generator.cpp) now relies on — and the
// shader's per-axis banding must agree with the CPU authority everywhere.
// Historic drift this locks out: the shader used floor(x * 2.99) buckets
// (thresholds at 0.334 / 0.669 instead of 0.25 / 0.75), so e.g. t = 0.30 was
// simulated as row 1 (Valley) but rendered as row 0 (Tundra).
void test_shader_mirror_matches_authority() {
    for (int i = 0; i <= 1000; ++i) {
        const float x = float(i) / 1000.0f;
        if (shader_axis(x) != authority_axis(x)) {
            CHECK(false, "shader banding diverges from biome_from_climate");
            return;
        }
    }
    for (int t = 0; t < 256; ++t) {
        for (int m = 0; m < 256; ++m) {
            const float t01 = float(t) / 255.0f;
            const float m01 = float(m) / 255.0f;
            const int id = authority_axis(t01) * 3 + authority_axis(m01);
            if (int(sm::biome_from_climate(t01, m01)) != id) {
                CHECK(false, "Biome enum id != row * 3 + col");
                return;
            }
        }
    }
    // Negative control: the retired floor(x * 2.99) shader banding really did
    // disagree with the authority (documents why bt_biome was rewritten).
    const float bad = 0.30f;
    CHECK(int(bad * 2.99f) != authority_axis(bad),
           "old floor(x * 2.99) banding should diverge at 0.30");
}

// The river edge-detect used min(1, t/128)*3 + min(2, m/86) over raw bytes —
// a 2x3 matrix whose Desert/Steppe/Tropics row was unreachable. Locks the
// replacement (a straight biome_from_climate call) and documents the old bug.
void test_river_pass_byte_path_matches_authority() {
    bool oldDiverges = false;
    bool oldReachesHotRow = false;
    for (int t = 0; t < 256; ++t) {
        for (int m = 0; m < 256; ++m) {
            const int now = int(sm::biome_from_climate(float(t) / 255.0f,
                                                       float(m) / 255.0f));
            const int old = std::min(1, t / 128) * 3 + std::min(2, m / 86);
            if (old != now) oldDiverges = true;
            if (old >= int(sm::Desert)) oldReachesHotRow = true;
        }
    }
    CHECK(oldDiverges, "old river byte formula should diverge somewhere");
    CHECK(!oldReachesHotRow,
           "old river byte formula could never classify Desert/Steppe/Tropics");
    // The authority does reach the hot row from bytes, e.g. max temperature.
    CHECK(sm::biome_from_climate(1.0f, 0.0f) == sm::Desert,
           "authority classifies hot/dry bytes as Desert");
}

} // namespace

int main() {
    test_water_is_classified_by_sea_level();
    test_mountain_is_classified_by_elevation();
    test_climate_band_delegates_to_matrix();
    test_water_precedence_over_mountain();
    test_shader_mirror_matches_authority();
    test_river_pass_byte_path_matches_authority();
    return sm::test::report("biome_classifier_test");
}
