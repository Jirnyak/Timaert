// Celestial bodies — data-driven moons and constellations for the night sky.
//
// Like seasons.h, the whole model is a PURE function of the already-persisted
// world clock (macro/state.h WorldTime): a moon's phase is derived from the
// absolute `day`, so nothing new is serialized and kSaveVersion does not move
// ("derive, don't store"). The tables here are the same constexpr-array +
// inline-accessor idiom as seasons.h / landmark_registry.h / biomes.h: adding a
// moon or a constellation is ONE row, no engine change, no if-chain.
//
// Ownership seam: this header is pure gameplay DATA and pure lookups with zero
// dependencies beyond the standard library. The renderer (#include this header)
// reads the tables to draw the sky — moon discs at their current illuminated
// fraction and colour, constellation star-graphs at their fixed dome positions.
// This layer also owns each body's PLACE in the sky (sun_dir / moon_dir below):
// a moon's position and its phase derive from the one lag angle, so the disc
// you see, the moonlight on the terrain and the moon-path on the water agree by
// construction — nothing is pinned to -sunDir by decree anymore.
//
// Angle units are documented per field and chosen for hand-authorability
// (degrees), because constellations are authored by a human, not computed.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sm {

// ── Star size seam ──────────────────────────────────────────────────────────
// The CPU-side knob the star disc radius in shaders/sky.frag is multiplied by
// (< 1 shrinks every star uniformly). Defined here (data), not in the shader,
// so the size lives with the rest of the sky data and can be retuned without
// touching GLSL; sub/sky.h carries it through the SkyPush push-constant.
inline constexpr float kSkyStarSizeScale = 0.60f;   // address "stars too big"

// A star's on-screen size also scales with its brightness (below); this is the
// floor so even the dimmest star stays visible as at least a pixel.
inline constexpr float kSkyStarSizeMin = 0.25f;

// ── Moons ─────────────────────────────────────────────────────────────────
// Timaert has more than one moon; each is procedural — its phase advances on
// its own cycle so the moons are rarely full together, giving a varied night
// sky with no authored keyframes. A moon holds no state: phase is a pure
// function of the absolute day.
enum class MoonId : std::uint8_t { Pale = 0, Crimson, Count };

struct MoonDef {
    MoonId        id;
    const char*   name;
    float         baseSize;       // angular disc size, renderer-relative units
    std::uint32_t colorRGB;       // 0xRRGGBB tint of the lit disc
    int           cyclePeriodDays;// days for a full new→full→new lunar cycle
    int           phaseOffsetDays;// day-0 offset so moons desync from each other
    float         orbitTiltDeg;   // orbit-plane tilt off the sun's X-Y arc, so
                                  // two moons never ride the exact same line
};

// Two moons: a large slow pale one and a small fast crimson one. Their periods
// are coprime-ish so full moons rarely coincide (Pale full every 28d, Crimson
// every 11d → aligned only ~every 308 days). Add a row for a third moon.
inline constexpr MoonDef kMoons[std::size_t(MoonId::Count)] = {
    // id                name        baseSize  colorRGB    cycleDays  offset  tilt
    { MoonId::Pale,    "Selûne",      1.00f,   0xE8ECF5u,     28,        0,    9.0f },
    { MoonId::Crimson, "Vharûn",     0.55f,   0xD98A6Au,     11,        3,  -16.0f },
};

inline constexpr const MoonDef& moon_def(MoonId m) {
    return kMoons[std::size_t(m)];
}

inline constexpr float kCelTau      = 6.28318530718f;
inline constexpr float kCelDegToRad = kCelTau / 360.0f;

// Phase position in [0,1): 0 = new moon, 0.5 = full moon, →1 = back to new.
// The float-day form is the master formula: the sky moves smoothly through
// midnight, and a per-integer-day phase would jump the moon's position by
// 360°/period at every day flip (≈13° for the Pale moon — a visible pop).
// Negative/zero days wrap cleanly so callers never guard the argument.
inline float moon_phase01f(MoonId m, float dayf) {
    const MoonDef& d = moon_def(m);
    const float period = float(d.cyclePeriodDays > 0 ? d.cyclePeriodDays : 1);
    float t = std::fmod(dayf - 1.0f + float(d.phaseOffsetDays), period);
    if (t < 0.0f) t += period;
    return t / period;
}

// Whole-day phase (1-based, like season_at) — DELEGATES to the float form so
// the two can never drift: moon_phase01(m, d) == moon_phase01f(m, float(d)).
inline float moon_phase01(MoonId m, int day) {
    return moon_phase01f(m, float(day));
}

// Illuminated fraction of the disc in [0,1]: 0 at new moon (phase 0), 1 at full
// (phase 0.5), symmetric waxing/waning. The standard illuminated-fraction
// curve f = (1 - cos(2π·phase))/2. This is what the renderer scales the lit
// disc / moonlight contribution by, replacing the hardcoded always-full moon.
inline float moon_illumination01f(MoonId m, float dayf) {
    return 0.5f * (1.0f - std::cos(kCelTau * moon_phase01f(m, dayf)));
}
inline float moon_illumination01(MoonId m, int day) {
    return moon_illumination01f(m, float(day));
}

// True while the moon is growing (new → full); false while shrinking. Handy for
// a crescent's orientation. phase in (0,0.5) waxes, (0.5,1) wanes.
inline bool moon_is_waxing(MoonId m, int day) {
    return moon_phase01(m, day) < 0.5f;
}

// ── Sky position — procedural orbits ────────────────────────────────────────
// A moon's place in the sky derives from the SAME phase that lights it: the
// moon rides the sun's daily arc but LAGS the sun by its phase,
//
//     moonAngle = sunAngle − phase01 · 2π
//
// so a full moon (phase 0.5) is exactly anti-solar — it rises at sunset and
// stands highest at midnight — and a new moon travels WITH the sun, lost in
// its glare (and its unlit disc contributes nothing anyway). The visible
// disc, the moonlight that sculpts the terrain and the moon-path on the water
// all follow from this one derivation; the old "moonDir = -sunDir by decree"
// contract becomes an emergent property of the full moon instead of a
// hardcode. Each orbit additionally tilts off the sun's plane by its own
// orbitTiltDeg, swinging the arc into Z so two moons never stack on one line.

// A direction on the celestial dome (unit length). Deliberately its own tiny
// POD, not core/math's vec3 — this header's contract is zero deps.
struct SkyDir { float x, y, z; };

// The sun's daily arc in the X-Y plane: tod 0.25 = sunrise (+X), 0.5 = noon
// zenith, 0.75 = sunset (−X). This is THE formula sub/lighting.h and
// shaders/sky.frag each hardcoded; both consuming it from here makes "one
// celestial direction" a mechanism instead of a comment. tod is the fraction
// of the day in [0,1), midnight = 0.
inline SkyDir sun_dir(float tod) {
    const float a = (tod - 0.25f) * kCelTau;
    return { std::cos(a), std::sin(a), 0.0f };
}

// Where a moon stands at (day, tod). Phase is sampled at the fractional day
// (day + tod) so the lag — and therefore the position — glides through
// midnight instead of stepping.
inline SkyDir moon_dir(MoonId m, int day, float tod) {
    const float phase = moon_phase01f(m, float(day) + tod);
    const float a = (tod - 0.25f) * kCelTau - phase * kCelTau;
    const float tilt = moon_def(m).orbitTiltDeg * kCelDegToRad;
    return { std::cos(a),
             std::sin(a) * std::cos(tilt),
             std::sin(a) * std::sin(tilt) };
}

// The authored 0xRRGGBB tint unpacked to linear-ish [0,1] floats — the form
// every renderer-side consumer (push constants, light colour) actually wants.
inline void moon_color_rgb(MoonId m, float rgb[3]) {
    const std::uint32_t c = moon_def(m).colorRGB;
    rgb[0] = float((c >> 16) & 0xFFu) / 255.0f;
    rgb[1] = float((c >>  8) & 0xFFu) / 255.0f;
    rgb[2] = float( c        & 0xFFu) / 255.0f;
}

// ── Night light — contextual, not hardcoded ─────────────────────────────────
// Which moon lights the night is a QUERY, not a rule: the dominant moon is
// whichever is both lit and up, weighted illumination × horizon fade. On a
// night when every moon is new or below the horizon, strength01 is 0 and the
// world honestly goes dark (the ambient floor in sub/lighting.h keeps it from
// pure black). Consumers (directional night light, the water's moon-path, the
// sky's bloom) all read this one answer, so they can never disagree.
struct NightLight {
    SkyDir dir;        // toward the dominant moon; {0,1,0} when strength01 == 0
    float  rgb[3];     // that moon's tint
    float  strength01; // illumination × horizon fade, in [0,1]; 0 = no moon
    int    moonIndex;  // int(MoonId) of the dominant moon, -1 when none
};

inline NightLight night_light(int day, float tod) {
    NightLight best{};
    best.dir = { 0.0f, 1.0f, 0.0f };
    best.moonIndex = -1;
    for (int mi = 0; mi < int(MoonId::Count); ++mi) {
        const MoonId m = MoonId(mi);
        const SkyDir d = moon_dir(m, day, tod);
        // Same horizon-fade shape lighting.h applies to the sun (smoothstep
        // over elevation -0.05..0.30), kept inline so this header stays
        // zero-dep and the sky and the light share one fade.
        float e = (d.y + 0.05f) / 0.35f;
        e = e < 0.0f ? 0.0f : (e > 1.0f ? 1.0f : e);
        e = e * e * (3.0f - 2.0f * e);
        const float s = moon_illumination01f(m, float(day) + tod) * e;
        if (s > best.strength01) {
            best.dir = d;
            best.strength01 = s;
            best.moonIndex = mi;
            moon_color_rgb(m, best.rgb);
        }
    }
    return best;
}

// ── Constellations (star-graphs) ────────────────────────────────────────────
// Stars sit at FIXED positions on the celestial dome (they do not move relative
// to each other), so a constellation is a static graph: named stars with a dome
// position + brightness, plus edges (index pairs) that draw the figure. This is
// pure authored data — the renderer plots the stars and connects the edges.
struct StarDef {
    const char* name;
    float       az;         // azimuth on the dome, degrees [0,360)
    float       el;         // elevation above horizon, degrees [0,90]
    float       brightness; // [0,1], 1 = brightest; scales the drawn disc size
};

struct StarEdge { std::uint8_t a, b; };   // indices into the constellation's stars

struct ConstellationDef {
    const char*     name;
    const StarDef*  stars;
    int             starCount;
    const StarEdge* edges;
    int             edgeCount;
};

// ── Authored constellations. Each is its own constexpr star + edge array; the
// top-level kConstellations table points at them (address-of a constexpr array
// is a constant expression, same pattern the recon confirmed as house style).
// Positions are illustrative dome coordinates, evocative not astronomical.

// The Wain — a seven-star dipper/plough, the sky's anchor figure.
inline constexpr StarDef kWainStars[] = {
    { "Dubhe",   40.0f, 62.0f, 0.95f },
    { "Merak",   47.0f, 58.0f, 0.85f },
    { "Phecda",  54.0f, 55.0f, 0.80f },
    { "Megrez",  52.0f, 60.0f, 0.55f },
    { "Alioth",  60.0f, 63.0f, 0.90f },
    { "Mizar",   67.0f, 66.0f, 0.88f },
    { "Alkaid",  74.0f, 68.0f, 0.86f },
};
inline constexpr StarEdge kWainEdges[] = {
    {0,1},{1,2},{2,3},{3,0},   // the bowl
    {3,4},{4,5},{5,6},         // the handle
};

// The Hunter — a bright belt-and-shoulders figure low in the south.
inline constexpr StarDef kHunterStars[] = {
    { "Bael",    200.0f, 30.0f, 0.92f },  // shoulder
    { "Corin",   208.0f, 28.0f, 0.70f },  // shoulder
    { "Vesk",    204.0f, 22.0f, 1.00f },  // belt (brightest)
    { "Tarn",    201.0f, 22.0f, 0.75f },  // belt
    { "Odel",    207.0f, 22.0f, 0.75f },  // belt
    { "Isha",    203.0f, 14.0f, 0.80f },  // foot
    { "Rukh",    206.0f, 14.0f, 0.78f },  // foot
};
inline constexpr StarEdge kHunterEdges[] = {
    {0,3},{1,4},        // shoulders down to belt ends
    {3,2},{2,4},        // the belt
    {3,5},{4,6},        // belt down to feet
    {5,6},              // feet
};

// The Serpent — a short winding chain, a dim test of the star-graph model.
inline constexpr StarDef kSerpentStars[] = {
    { "Nix",   300.0f, 45.0f, 0.60f },
    { "Sarr",  306.0f, 48.0f, 0.65f },
    { "Ophi",  312.0f, 44.0f, 0.55f },
    { "Zeth",  318.0f, 49.0f, 0.50f },
};
inline constexpr StarEdge kSerpentEdges[] = {
    {0,1},{1,2},{2,3},
};

// Counts are derived with a helper so a hand-written number can never drift
// out of sync with the array (an over-count is an out-of-bounds read — UB the
// optimizer turns into a hang). Adding a star/edge to an array Just Works.
template <class T, std::size_t N>
inline constexpr int arr_count(const T (&)[N]) { return int(N); }

inline constexpr ConstellationDef kConstellations[] = {
    { "The Wain",    kWainStars,    arr_count(kWainStars),
                     kWainEdges,    arr_count(kWainEdges)    },
    { "The Hunter",  kHunterStars,  arr_count(kHunterStars),
                     kHunterEdges,  arr_count(kHunterEdges)  },
    { "The Serpent", kSerpentStars, arr_count(kSerpentStars),
                     kSerpentEdges, arr_count(kSerpentEdges) },
};

inline constexpr int kConstellationCount =
    int(sizeof(kConstellations) / sizeof(kConstellations[0]));

inline constexpr const ConstellationDef& constellation_def(int i) {
    return kConstellations[std::size_t(i)];
}

// Compile-time proof that every authored edge references a star that exists.
// A bad index (e.g. {0,9} in a 7-star figure) is an out-of-bounds read at draw
// time — UB the optimizer can turn into a hang — so reject it at BUILD time for
// every consumer of this header, not just when a test happens to run.
constexpr bool celestial_edges_all_valid() {
    for (int ci = 0; ci < kConstellationCount; ++ci) {
        const ConstellationDef& c = kConstellations[std::size_t(ci)];
        for (int e = 0; e < c.edgeCount; ++e) {
            if (c.edges[e].a >= c.starCount || c.edges[e].b >= c.starCount)
                return false;
            if (c.edges[e].a == c.edges[e].b) return false;  // self-loop
        }
    }
    return true;
}
static_assert(celestial_edges_all_valid(),
              "a constellation edge references a nonexistent star");

} // namespace sm
