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

    // ── Weather seam (constants today, macro weather field tomorrow) ──
    float cloudiness01;        // 0 = clear .. 1 = overcast
    float windX, windZ;        // cloud drift, dome units / second
    float precip01;            // 0 = none .. 1 = downpour (rain/snow by season)
};

// How far the authored season tint (macro/seasons.h tintRGB) pulls the day
// sky from neutral. Deliberately subtle — a mood, not a filter.
inline constexpr float kSkySeasonTintStrength = 0.30f;

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

    // Weather defaults — today's constant look (the drift rate sky.frag
    // always used, a mild scattered-cloud cover). The macro weather field
    // replaces these reads, nothing else.
    c.cloudiness01 = 0.5f;
    c.windX = 0.008f;
    c.windZ = 0.0032f;
    c.precip01 = 0.0f;
    return c;
}

} // namespace sm::sub
