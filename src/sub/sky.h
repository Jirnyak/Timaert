// Subworld sky — the ONE door into the sky submodule.
//
// The sky is a pure-shader graphics submodule (shaders/sky.frag drawn as a
// fullscreen backdrop): it reads THIS context and writes pixels, nothing else.
// Turning it off is skipping one draw. Its only permitted influence on the
// rest of the world is lighting-shaped (the cloud-shadow factor, Inc D).
//
// SkyContext is everything the sky is allowed to know, gathered from the
// world by build_sky_context and copied verbatim into the SkyPush push
// constants by the renderer. All celestial truth (sun arc, procedural moon
// orbits, phases, tints) comes from macro/celestial.h — the sky never invents
// a position, so the disc you SEE, the light on the terrain (sub/lighting.h
// reads the same night_light) and the moon-path on the water agree by
// construction, not by decree.
//
// Weather seam: macro is the source of truth and the simulation; micro only
// renders. The weather fields below ship as constants today; when the macro
// weather field lands, build_sky_context starts reading them from the party's
// macro cell — this header's callers never learn where the numbers came from.
#pragma once

#include "core/time.h"
#include "macro/celestial.h"
#include "macro/seasons.h"

namespace sm::sub {

// Owner ruling: the sky carries 1–3 moons. The push-constant block and the
// shader loop are sized for this, so adding a moon within the cap stays "one
// row in kMoons"; a fourth moon is the compile error below, not a silent clip.
inline constexpr int kSkyMaxMoons = 3;
static_assert(int(MoonId::Count) <= kSkyMaxMoons,
              "the sky submodule carries at most 3 moons (owner ruling); "
              "grow kSkyMaxMoons + the SkyPush moon arrays together");

// One moon as the shader consumes it: where it stands, how large it draws,
// its authored tint, and how lit it is right now. The crescent SHAPE is not
// here — the shader derives the terminator from dir vs sunDir geometry, so
// the drawn phase can never contradict the illuminated fraction.
struct SkyMoonCtx {
    float dir[3];   // unit, toward the moon (macro/celestial.h moon_dir)
    float size;     // baseSize — angular scale relative to the reference disc
    float rgb[3];   // authored tint, [0,1]
    float illum;    // illuminated fraction [0,1] (bloom strength)
};

struct SkyContext {
    float      tod;            // fraction of the day [0,1), midnight = 0
    float      sunDir[3];      // unit, toward the sun (celestial sun_dir)
    int        moonCount;      // ≤ kSkyMaxMoons
    SkyMoonCtx moons[kSkyMaxMoons];
    float      starSizeScale;  // celestial star-size seam (≥ kSkyStarSizeMin)

    // Seasonal DAY-sky tint as a ready multiplier (already blended toward
    // white here — the strength is data, the shader just multiplies): winter
    // pales the sky, summer gilds it, autumn rusts it. Applied scaled by
    // dayF in sky.frag, so the night sky (moons, stars) stays honest.
    float seasonTint[3];

    // ── Weather seam (derived from the calendar today, macro field tomorrow)
    float cloudiness01;        // 0 = clear .. 1 = overcast
    float windX, windZ;        // cloud drift, dome units / second
    float precip01;            // 0 = none .. 1 = downpour
    int   precipKind;          // int(PrecipKind): what falls when it falls
    float storm01;             // 0 = calm .. 1 = full thunderstorm (rain only)
};

// How far the authored season tint (macro/seasons.h tintRGB) pulls the day
// sky from neutral. Deliberately subtle — a mood, not a filter.
inline constexpr float kSkySeasonTintStrength = 0.30f;

// ── Weather — DERIVED, not stored ───────────────────────────────────────────
// There is no macro weather field yet, so today's weather is a pure function
// of the calendar: a per-day hash decides whether a day is wet (chance =
// season data), WHAT falls (season kind weights — owner ruling: winter snow,
// summer rain, spring hail/snow/rain, autumn rain/snow), when during the day
// it falls (a smooth window — no popping), and whether a rain turns into a
// thunderstorm. Zero state, zero serialization ("derive, don't store"): the
// same day always has the same weather, and when the macro weather field
// lands its truth replaces this derivation inside build_sky_context — the
// renderer and shaders never learn the difference.
enum class PrecipKind : std::uint8_t { Rain = 0, Snow, Hail };

struct SeasonPrecipDef {
    float        chance01;       // fraction of the season's days that are wet
    float        stormChance01;  // of wet RAIN days, fraction that thunder
    std::uint8_t kindWeights[3]; // relative weights: Rain, Snow, Hail
};

// Indexed by macro/seasons.h Season. Adding a season there without a row
// here is the static_assert below, not a silent out-of-bounds read.
inline constexpr SeasonPrecipDef kSeasonPrecip[std::size_t(Season::Count)] = {
    // season    chance  storm   rain snow hail
    /*Spring*/ { 0.35f,  0.15f, { 5,   2,   2 } },
    /*Summer*/ { 0.25f,  0.35f, { 1,   0,   0 } },
    /*Autumn*/ { 0.45f,  0.10f, { 6,   1,   0 } },
    /*Winter*/ { 0.40f,  0.00f, { 0,   1,   0 } },
};
static_assert(sizeof(kSeasonPrecip) / sizeof(kSeasonPrecip[0])
                  == std::size_t(Season::Count),
              "every season needs a precipitation row");

// Small integer hash (splitmix-style avalanche) + a [0,1) mapping — the
// deterministic dice this header rolls the calendar's weather with.
inline std::uint32_t sky_hash_u32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16; return x;
}
inline float sky_hash01(std::uint32_t x) {
    return float(sky_hash_u32(x) >> 8) * (1.0f / 16777216.0f);
}

namespace skydetail {
inline float smooth01(float a, float b, float x) {
    float t = (x - a) / (b - a);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
}

// The weather of an instant. Pure and total: same (day, tod) → same answer.
// A wet day rains/snows through ONE smooth window (start + duration from the
// day's hash, eased edges), at a per-day peak intensity; the kind is constant
// for the whole day. storm01 follows the precipitation envelope so thunder
// lives inside the shower, not on a dry morning.
inline void weather_at(int day, float tod, float& outPrecip01,
                       PrecipKind& outKind, float& outStorm01) {
    outPrecip01 = 0.0f;
    outKind = PrecipKind::Rain;
    outStorm01 = 0.0f;
    const SeasonPrecipDef& def = kSeasonPrecip[std::size_t(season_at(day))];
    const std::uint32_t h = sky_hash_u32(std::uint32_t(day) * 2654435761u
                                         + 0x9E3779B9u);
    if (sky_hash01(h) >= def.chance01) return;   // a dry day

    const int total = int(def.kindWeights[0]) + int(def.kindWeights[1])
                    + int(def.kindWeights[2]);
    if (total <= 0) return;                      // season without precipitation
    int pick = int(sky_hash_u32(h + 1) % std::uint32_t(total));
    int kindIdx = 0;
    while (pick >= int(def.kindWeights[kindIdx])) {
        pick -= int(def.kindWeights[kindIdx]);
        ++kindIdx;
    }
    outKind = PrecipKind(kindIdx);

    const float start = sky_hash01(h + 2) * 0.72f;          // tod the shower starts
    const float dur   = 0.15f + sky_hash01(h + 3) * 0.35f;  // 3.6..12 game hours
    const float end   = start + dur > 1.0f ? 1.0f : start + dur;
    const float env = skydetail::smooth01(start, start + 0.08f, tod)
                    * (1.0f - skydetail::smooth01(end - 0.08f, end, tod));
    const float peak = 0.45f + sky_hash01(h + 4) * 0.55f;
    outPrecip01 = env * peak;

    if (outKind == PrecipKind::Rain
        && sky_hash01(h + 5) < def.stormChance01) {
        outStorm01 = outPrecip01;               // thunder rides the shower
    }
}

// Lightning flash envelope in [0,1] — a pure function of the RENDER clock
// (seconds), so it animates smoothly regardless of frame rate and replays
// identically in the capture harness. Interval-hashed: some ~6 s windows
// flash (a sharp attack + a fainter echo 0.12 s later — the classic double
// strike), most stay quiet; a stronger storm flashes more windows. The
// renderer adds the flash to the AMBIENT it hands every lit pass (the whole
// world blinks) and to the sky dome via its push lane.
inline float storm_flash01(float timeSec, float storm01) {
    if (storm01 <= 0.0f) return 0.0f;
    const float interval = 6.0f;
    const std::uint32_t idx = std::uint32_t(timeSec / interval);
    const std::uint32_t h = sky_hash_u32(idx * 0x9E3779B9u + 17u);
    if (sky_hash01(h) > 0.15f + 0.55f * storm01) return 0.0f;  // quiet window
    const float off = sky_hash01(h + 1) * (interval - 1.5f);
    const float tf = timeSec - float(idx) * interval - off;
    if (tf < 0.0f) return 0.0f;
    const float p1 = std::exp(-tf * 14.0f);
    const float t2 = tf - 0.12f;
    const float p2 = t2 > 0.0f ? std::exp(-t2 * 10.0f) * 0.6f : 0.0f;
    const float f = p1 + p2;
    return f > 1.0f ? 1.0f : f;
}

// ── Constellation stars — a tiny STATIC uniform buffer ─────────────────────
// The authored star-graphs (macro/celestial.h kConstellations) do not fit in
// push constants and never change at runtime, so they are uploaded ONCE at
// init into a small UBO the sky pipeline binds at set 0. std140: an array of
// vec4 has 16-byte stride, so the CPU struct below maps byte-for-byte.
// Edges are deliberately NOT uploaded (owner ruling: stars only — the figures
// read through brightness and placement, like a real sky; the edge tables
// stay authored for a future star-map UI).
inline constexpr int kSkyMaxConstellationStars = 32;
static_assert(celestial_total_stars() <= kSkyMaxConstellationStars,
              "more authored constellation stars than the sky UBO holds — "
              "grow kSkyMaxConstellationStars + the sky.frag mirror together");

struct SkyStarsUbo {
    float count[4];                                // x = star count, yzw pad
    float stars[kSkyMaxConstellationStars][4];     // xyz dome dir, w brightness
};
static_assert(sizeof(SkyStarsUbo) == 16 + 16 * kSkyMaxConstellationStars,
              "SkyStarsUbo must match the std140 UBO layout");

inline void fill_sky_stars(SkyStarsUbo& u) {
    int n = 0;
    for (int ci = 0; ci < kConstellationCount; ++ci) {
        const ConstellationDef& c = constellation_def(ci);
        for (int s = 0; s < c.starCount && n < kSkyMaxConstellationStars; ++s) {
            const SkyDir d = star_dome_dir(c.stars[s]);
            u.stars[n][0] = d.x;
            u.stars[n][1] = d.y;
            u.stars[n][2] = d.z;
            u.stars[n][3] = c.stars[s].brightness;
            ++n;
        }
    }
    u.count[0] = float(n);
    u.count[1] = u.count[2] = u.count[3] = 0.0f;
}

// The tod the whole subworld frame uses (renderer, lighting) — minute
// granularity off the one tick clock, same formula compute_sun always used.
inline float time_of_day01(const WorldTime& t) {
    return (float(t.hour()) + float(t.minute()) / 60.0f) / 24.0f;
}

inline SkyContext build_sky_context(const WorldTime& t) {
    SkyContext c{};
    c.tod = time_of_day01(t);
    const int day = t.day();

    const SkyDir s = sun_dir(c.tod);
    c.sunDir[0] = s.x; c.sunDir[1] = s.y; c.sunDir[2] = s.z;

    c.moonCount = int(MoonId::Count);
    for (int mi = 0; mi < c.moonCount; ++mi) {
        const MoonId m = MoonId(mi);
        const SkyDir d = moon_dir(m, day, c.tod);
        SkyMoonCtx& mc = c.moons[mi];
        mc.dir[0] = d.x; mc.dir[1] = d.y; mc.dir[2] = d.z;
        mc.size  = moon_def(m).baseSize;
        mc.illum = moon_illumination01f(m, float(day) + c.tod);
        moon_color_rgb(m, mc.rgb);
    }

    // Data layer owns the star-size values; the floor is applied HERE so the
    // shader can trust the one scale it receives.
    c.starSizeScale = kSkyStarSizeScale > kSkyStarSizeMin ? kSkyStarSizeScale
                                                          : kSkyStarSizeMin;

    // Season tint: the authored 0xRRGGBB from the season table, pre-blended
    // toward white at the data-owned strength.
    {
        const std::uint32_t t32 = season_def(season_at(day)).tintRGB;
        const float r = float((t32 >> 16) & 0xFFu) / 255.0f;
        const float g = float((t32 >>  8) & 0xFFu) / 255.0f;
        const float b = float( t32        & 0xFFu) / 255.0f;
        c.seasonTint[0] = 1.0f + (r - 1.0f) * kSkySeasonTintStrength;
        c.seasonTint[1] = 1.0f + (g - 1.0f) * kSkySeasonTintStrength;
        c.seasonTint[2] = 1.0f + (b - 1.0f) * kSkySeasonTintStrength;
    }

    // Weather — derived from the calendar (weather_at above); the macro
    // weather field replaces these reads, nothing else. A wet spell drags
    // the cloud cover up with it, so rain falls from a heavy sky and the
    // cloud-shadow system darkens the world for free.
    {
        float precip = 0.0f, storm = 0.0f;
        PrecipKind kind = PrecipKind::Rain;
        weather_at(day, c.tod, precip, kind, storm);
        c.precip01 = precip;
        c.precipKind = int(kind);
        c.storm01 = storm;
        const float cloud = 0.5f + 0.45f * precip;
        c.cloudiness01 = cloud > 1.0f ? 1.0f : cloud;
    }
    c.windX = 0.008f;
    c.windZ = 0.0032f;
    return c;
}

} // namespace sm::sub
