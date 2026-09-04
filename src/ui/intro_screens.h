// The two PRE-WORLD cinematic screens: the studio splash and the intro
// slideshow. Neither touches the game — no GameState, no ECS, no world;
// they read only their own state struct and the authored slide table, and
// answer with a ShellResult flag the app's one transition dispatcher acts
// on. That is the whole contract, and it is why they live apart from
// screens.cpp: this file can be deleted and the game still boots.
//
// Both derive every coordinate from DisplaySize on the frame they draw, and
// cache nothing against the window size — the resize bug both reference
// intros carry (a resize event rebuilt or restarted the scene, so going
// fullscreen visibly jumped) is impossible here by construction.
#pragma once

#include "ui/screens.h"   // ShellResult — the shell's one answer type

#include <cstdint>

namespace sm::ui {

// ── The studio splash ───────────────────────────────────────────────────
// A swarm of blood-dark pixels assembles into the pixel sword and the studio
// letters, then blood runs down the blade and drips from the point. The
// cursor is a TOY (shove, punch — the reference intros' physics, verbatim);
// only the keyboard leaves.

// One splash particle. `home` is NOT stored: it is derived from the viewport
// every frame (sword-bitmap cell / letter-glyph cell / blot offset), so a
// resize merely moves the homes and the springs walk everyone over.
struct SplashParticle {
    float x = 0, y = 0, vx = 0, vy = 0;
    float delay = 0;              // seconds of swarming before the spring engages
    std::uint8_t kind = 0;        // 0 sword pixel, 1 letter cell, 2 blot, 3 dying speck
    std::int16_t gx = 0, gy = 0;  // sword: bitmap cell; letter: index+cell; blot: mils
};

// A falling blood pixel (square, like everything else here). sz == 0 = free.
struct SplashDrop {
    float x = 0, y = 0, vx = 0, vy = 0, sz = 0;
};

// One seated runnel pixel on the blade — a particle like everything else, so
// the cursor can smear the blood and the spring pulls it back home.
struct SplashBloodSeg {
    float x = 0, y = 0, vx = 0, vy = 0;
    std::int8_t col = 0;          // which runnel (0 centre, 1/2 flanks)
    std::int8_t idx = 0;          // segment index down the blade
    bool live = false;
};

struct SplashState {
    static constexpr int kMaxParticles = 384;
    static constexpr int kMaxDrops = 8;
    static constexpr int kMaxBloodSegs = 48;
    bool  inited = false;
    float t = 0.0f;               // seconds since the splash began
    float idleT = 0.0f;           // seconds since the mouse last played
    float punchCharge = 0.0f;     // rapid clicking builds a bigger burst
    int   count = 0;
    SplashParticle p[kMaxParticles]{};
    float bloodLen[3] = {0, 0, 0};   // runnels down the blade, px
    int   segSpawned[3] = {0, 0, 0}; // runnel pixels already born
    SplashBloodSeg bloodSegs[kMaxBloodSegs]{};
    SplashDrop drops[kMaxDrops]{};
    float dropTimer = 0.0f;          // until the next drop leaves the point
    std::uint32_t dropRng = 0x9E3779B9u;
};

// The studio splash. The mouse only ever plays with the pixels; the first
// key assembles the emblem at once, the next leaves, Esc always leaves, and
// the auto-exit clocks IDLE time — while you poke, nothing closes.
ShellResult draw_studio_splash(SplashState& st);

// ── The intro slideshow ─────────────────────────────────────────────────
// The slideshow's whole memory: which frame, and how far the typewriter got
// (fractional characters — it accrues io.DeltaTime, whole code points show).
struct IntroSlidesState {
    int   slide      = 0;
    float typedChars = 0.0f;
};

// The pre-world slideshow (content/plot/intro.h intro_story): frame in the
// upper screen, typewriter caption below, slide marks, Esc skips all. First
// click/key completes the typing, the second turns the slide; the last turn
// raises ShellResult::introFinished.
ShellResult draw_intro_slides(IntroSlidesState& st);

} // namespace sm::ui
