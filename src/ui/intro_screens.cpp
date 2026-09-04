#include "ui/intro_screens.h"
#include "ui/ui_image.h"
#include "ui/ui_theme.h"
#include "content/plot/intro.h"   // intro_story(): the authored slide table
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>

namespace sm::ui {

namespace {

// ── UTF-8 code-point helpers for the typewriter ─────────────────────────
// Counting BYTES tears a multi-byte character in half mid-type; both are by
// code points (continuation bytes 10xxxxxx don't count), the reference
// intro's hard-won lesson.
std::size_t utf8_length(const char* s) {
    std::size_t n = 0;
    for (; *s; ++s)
        if ((*s & 0xC0) != 0x80) ++n;
    return n;
}

// Byte length of the first `cp` code points of `s`.
std::size_t utf8_prefix_bytes(const char* s, std::size_t cp) {
    const char* p = s;
    while (*p && cp > 0) {
        ++p;
        while ((*p & 0xC0) == 0x80) ++p;
        --cp;
    }
    return std::size_t(p - s);
}

// Typing speed. Slower than an action game's chatter: this is the saga voice.
constexpr float kIntroTypeCharsPerSec = 45.0f;

} // namespace

namespace {

// ── The studio splash ───────────────────────────────────────────────────
// A swarm of blood-dark pixels (the owner's logo2 reference) assembles into
// the pixel sword and the studio letters; arrived sword pixels cool from
// blood-red into steel and bronze, stray specks die out, a few blots stay.
// Blood then runs down the blade and drips from the point. The cursor can
// shove any particle (the Lionhead nod) — the spring always wins.

// The sword, point down: P pommel/guard (bronze), G grip (leather),
// B blade (steel), D fuller (darker steel).
constexpr int kSwordW = 11, kSwordH = 16;
constexpr const char* kSwordRows[kSwordH] = {
    "....PPP....",
    ".....G.....",
    ".....G.....",
    "PPPPPPPPPPP",
    "....BDB....",
    "....BDB....",
    "....BDB....",
    "....BDB....",
    "....BDB....",
    "....BDB....",
    "....BDB....",
    "....BDB....",
    "....BDB....",
    "....BDB....",
    ".....B.....",
    ".....B.....",
};

// The letters are 5×7 BITMAP GLYPHS blown up into big square cells — the
// reference intros' whole aesthetic (they layout words from the same 5×7
// grid); font text at any scale reads as a subtitle, not as an emblem.
struct SplashGlyph {
    char ch;
    const char* rows[7];
};

constexpr SplashGlyph kSplashGlyphs[] = {
    {'T', {"XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."}},
    {'E', {"XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX"}},
    {'N', {"X...X", "XX..X", "X.X.X", "X..XX", "X...X", "X...X", "X...X"}},
    {'V', {"X...X", "X...X", "X...X", "X...X", "X...X", ".X.X.", "..X.."}},
    {'I', {"XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "XXXXX"}},
    {'K', {"X...X", "X..X.", "X.X..", "XX...", "X.X..", "X..X.", "X...X"}},
    {'G', {".XXX.", "X...X", "X....", "X.XXX", "X...X", "X...X", ".XXX."}},
    {'A', {".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"}},
    {'M', {"X...X", "XX.XX", "X.X.X", "X.X.X", "X...X", "X...X", "X...X"}},
    {'S', {".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX."}},
};

constexpr const SplashGlyph* splash_glyph(char c) {
    for (const SplashGlyph& g : kSplashGlyphs)
        if (g.ch == c) return &g;
    return nullptr;
}

// One line, the sword standing between the words: TENEVIK ⚔ GAMES.
constexpr const char* kWordLeft = "TENEVIK";
constexpr const char* kWordRight = "GAMES";
constexpr float kWordLeftCells = 7.0f * 6.0f - 1.0f;    // letters are 5 cells + 1 gap
constexpr float kWordRightCells = 5.0f * 6.0f - 1.0f;

// Everything about WHERE things sit, derived from the viewport each frame.
struct SplashLayout {
    float cell;               // one letter cell (big, chunky)
    float spx;                // one sword pixel (a touch bigger than a cell)
    float leftX, rightX;      // word origins
    float lettersY;           // top of the letter grid
    float swordX, swordY;     // top-left of the sword bitmap
    float midY;               // the composition's centre line
    float fontSize;           // subtitle
};

SplashLayout splash_layout(const ImVec2& vp) {
    SplashLayout L{};
    // The emblem takes ~80% of the width: TENEVIK + gap + sword + gap + GAMES.
    L.cell = std::min(std::max(vp.x * 0.80f
                 / (kWordLeftCells + kWordRightCells + 8.0f + 11.0f * 1.35f),
                 6.0f), 26.0f);
    L.spx = L.cell * 1.35f;
    const float gap = 4.0f * L.cell;
    const float total = kWordLeftCells * L.cell + gap
        + float(kSwordW) * L.spx + gap + kWordRightCells * L.cell;
    const float x0 = (vp.x - total) * 0.5f;
    L.midY = vp.y * 0.42f;
    L.leftX = x0;
    L.swordX = x0 + kWordLeftCells * L.cell + gap;
    L.rightX = L.swordX + float(kSwordW) * L.spx + gap;
    L.lettersY = L.midY - 3.5f * L.cell;
    L.swordY = L.midY - float(kSwordH) * 0.5f * L.spx;
    L.fontSize = ImGui::GetFontSize()
        * std::min(std::max(vp.y / 700.0f, 1.0f), 2.0f);
    return L;
}

// A particle's home this frame. kind 3 (dying speck) has none.
// Letter cells: gx = global letter index (left word first), gy = cy*5+cx.
ImVec2 splash_home(const SplashParticle& p, const SplashLayout& L,
                   const ImVec2& vp) {
    switch (p.kind) {
        case 0: return ImVec2(L.swordX + float(p.gx) * L.spx,
                              L.swordY + float(p.gy) * L.spx);
        case 1: {
            const int li = int(p.gx);
            const int cx = int(p.gy) % 5, cy = int(p.gy) / 5;
            const int nLeft = 7;
            const float baseX = li < nLeft
                ? L.leftX + float(li) * 6.0f * L.cell
                : L.rightX + float(li - nLeft) * 6.0f * L.cell;
            return ImVec2(baseX + float(cx) * L.cell,
                          L.lettersY + float(cy) * L.cell);
        }
        default:
            return ImVec2(vp.x * 0.5f + float(p.gx) * 0.001f * vp.x * 0.34f,
                          L.midY + float(p.gy) * 0.001f * vp.y * 0.30f);
    }
}

void splash_init(SplashState& st, const ImVec2& vp) {
    std::uint32_t rng = 0x7EAE71C5u;
    auto frand = [&rng]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return float(rng & 0xFFFFFFu) / float(0x1000000);
    };
    auto scatter = [&](SplashParticle& p) {
        p.x = vp.x * (0.10f + 0.80f * frand());
        p.y = vp.y * (0.05f + 0.90f * frand());
        p.vx = (frand() - 0.5f) * 60.0f;
        p.vy = (frand() - 0.5f) * 60.0f;
    };
    st.count = 0;
    for (int r = 0; r < kSwordH; ++r) {
        for (int c = 0; c < kSwordW; ++c) {
            if (kSwordRows[r][c] == '.') continue;
            SplashParticle& p = st.p[st.count++];
            scatter(p);
            p.kind = 0; p.gx = std::int16_t(c); p.gy = std::int16_t(r);
            p.delay = 0.5f + 2.3f * frand();
        }
    }
    int letterIdx = 0;
    for (const char* word : {kWordLeft, kWordRight}) {
        for (int i = 0; word[i]; ++i, ++letterIdx) {
            const SplashGlyph* g = splash_glyph(word[i]);
            if (!g) continue;
            for (int cy = 0; cy < 7; ++cy) {
                for (int cx = 0; cx < 5; ++cx) {
                    if (g->rows[cy][cx] == '.') continue;
                    if (st.count >= SplashState::kMaxParticles) break;
                    SplashParticle& p = st.p[st.count++];
                    scatter(p);
                    p.kind = 1;
                    p.gx = std::int16_t(letterIdx);
                    p.gy = std::int16_t(cy * 5 + cx);
                    p.delay = 1.2f + 2.2f * frand();
                }
            }
        }
    }
    for (int i = 0; i < 10 && st.count < SplashState::kMaxParticles; ++i) {
        SplashParticle& p = st.p[st.count++];
        scatter(p);
        p.kind = 2;
        p.gx = std::int16_t((frand() * 2.0f - 1.0f) * 1000.0f);
        p.gy = std::int16_t((frand() * 2.0f - 1.0f) * 1000.0f);
        p.delay = 2.0f + 1.4f * frand();
    }
    while (st.count < SplashState::kMaxParticles) {
        SplashParticle& p = st.p[st.count++];
        scatter(p);
        p.kind = 3;
        p.delay = 1e9f;   // never springs; it dies out instead
    }
    st.inited = true;
}

float splash_rand01(std::uint32_t& rng) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return float(rng & 0xFFFFFFu) / float(0x1000000);
}

// Parchment with a per-cell shade wobble: the reference intros always read
// as SEPARATE squares — same-colour cells drawn flush melt into one shape.
constexpr ImU32 kParchShades[3] = {
    IM_COL32(230, 217, 176, 255),
    IM_COL32(208, 195, 156, 255),
    IM_COL32(244, 233, 196, 255),
};

// Any KEYBOARD (or gamepad) key, except Esc — the hint says "press any key"
// and the hint must not lie. Mouse buttons are deliberately NOT here: the
// mouse is the toy (shove, punch), and a toy that closed the screen the
// third time you poked it would be a trap — the B&W / reference-intro rule.
bool splash_any_key_pressed() {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = ImGuiKey(k);
        if (key == ImGuiKey_Escape) continue;
        if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) continue;
        if (ImGui::IsKeyPressed(key, false)) return true;
    }
    return false;
}

} // namespace

ShellResult draw_studio_splash(SplashState& st) {
    ShellResult r{};
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 vp = viewport_size();
    if (vp.x <= 0.0f || vp.y <= 0.0f) return r;
    if (!st.inited) splash_init(st, vp);

    const float dt = std::min(io.DeltaTime, 0.05f);
    st.t += dt;
    // Playing with the mouse holds the door open: the auto-exit clocks
    // IDLE time, not show time — poke the pixels as long as you like
    // (the B&W / reference-intro contract).
    const bool mousePlaying = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f
        || ImGui::IsMouseDown(ImGuiMouseButton_Left);
    st.idleT = mousePlaying ? 0.0f : st.idleT + dt;
    const bool punch = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const SplashLayout L = splash_layout(vp);
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), vp, IM_COL32(0, 0, 0, 255));

    // ── the toy's physics, ported VERBATIM from the reference intros
    // (starcluster shell.cpp / gigahrush intro_ui.cpp, same names). The law
    // that makes it feel right: a struck particle is a PROJECTILE first —
    // the homeward spring stays silent until friction eats the impulse
    // (kFlySpeed gate) — so a good swipe scatters pixels across half the
    // screen instead of jelly-wobbling them back. Friction is per-frame
    // 0.988 (gentle: they slide), the cursor hands over its OWN velocity
    // (kPushDrag), a held LMB presses harder, a click is an instant burst.
    constexpr float kFriction    = 0.988f;
    constexpr float kFlySpeed    = 850.0f;
    constexpr float kSpringK     = 26.0f;
    constexpr float kSpringDamp  = 9.0f;
    constexpr float kEdgeBounce  = 0.78f;
    constexpr float kMaxSpeed    = 3200.0f;
    constexpr float kPushRadius  = 110.0f;
    constexpr float kPushRadial  = 900.0f;
    constexpr float kPushDrag    = 4.5f;
    constexpr float kPushHeldGain= 2.1f;
    constexpr float kPunchRadius = 240.0f;
    constexpr float kPunchImpulse= 1900.0f;

    const float mvx = io.MouseDelta.x / std::max(dt, 1e-4f);
    const float mvy = io.MouseDelta.y / std::max(dt, 1e-4f);
    const float held = ImGui::IsMouseDown(ImGuiMouseButton_Left)
        ? kPushHeldGain : 1.0f;
    // On top of the reference: rapid clicking charges the burst (owner).
    st.punchCharge = std::max(0.0f, st.punchCharge - dt * 1.1f);
    const float punchKick = kPunchImpulse * (0.7f + 0.3f * (1.0f + st.punchCharge));
    if (punch) st.punchCharge = std::min(3.0f, st.punchCharge + 1.0f);

    auto clampSpeed = [&](float& vx, float& vy) {
        const float v2 = vx * vx + vy * vy;
        if (v2 > kMaxSpeed * kMaxSpeed) {
            const float k = kMaxSpeed / std::sqrt(v2);
            vx *= k; vy *= k;
        }
    };
    auto shove = [&](float px_, float py_, float& vx, float& vy) {
        const float mdx = px_ - io.MousePos.x, mdy = py_ - io.MousePos.y;
        const float md2 = mdx * mdx + mdy * mdy;
        if (md2 < kPushRadius * kPushRadius && md2 > 1e-4f) {
            const float md = std::sqrt(md2);
            const float kk = 1.0f - md / kPushRadius;
            vx += (mdx / md * kPushRadial * kk + mvx * kPushDrag * kk) * held * dt;
            vy += (mdy / md * kPushRadial * kk + mvy * kPushDrag * kk) * held * dt;
        }
        if (punch && md2 < kPunchRadius * kPunchRadius && md2 > 1e-4f) {
            const float md = std::sqrt(md2);
            const float kk = 1.0f - md / kPunchRadius;
            vx += mdx / md * punchKick * kk;   // instant, no dt: a burst
            vy += mdy / md * punchKick * kk;
        }
        clampSpeed(vx, vy);
    };
    // Spring with the projectile gate + a noisy, non-straight return; snaps
    // home when both slow and near.
    auto springHome = [&](float& x, float& y, float& vx, float& vy,
                          float hx, float hy, float k) {
        const float dx = hx - x, dy = hy - y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float sp = std::sqrt(vx * vx + vy * vy);
        const float pull = 1.0f - std::min(1.0f, sp / kFlySpeed);
        if (pull > 0.0f) {
            const float noise = std::min(1.0f, dist / 220.0f) * 260.0f;
            vx += ((dx * k - vx * kSpringDamp)
                   + (splash_rand01(st.dropRng) * 2.0f - 1.0f) * noise) * pull * dt;
            vy += ((dy * k - vy * kSpringDamp)
                   + (splash_rand01(st.dropRng) * 2.0f - 1.0f) * noise) * pull * dt;
        }
        if (dist < 1.2f && sp < 40.0f) {
            x = hx; y = hy; vx *= 0.25f; vy *= 0.25f;
        }
    };
    const float damp = std::pow(kFriction, dt * 60.0f);
    auto integrate = [&](float& x, float& y, float& vx, float& vy) {
        vx *= damp; vy *= damp;
        x += vx * dt; y += vy * dt;
        const float m = 4.0f;   // edge bounce: flung pixels come back
        if (x < -m)        { x = -m;        vx = -vx * kEdgeBounce; }
        if (y < -m)        { y = -m;        vy = -vy * kEdgeBounce; }
        if (x > vp.x + m)  { x = vp.x + m;  vx = -vx * kEdgeBounce; }
        if (y > vp.y + m)  { y = vp.y + m;  vy = -vy * kEdgeBounce; }
    };

    // ── move ──
    for (int i = 0; i < st.count; ++i) {
        SplashParticle& p = st.p[i];
        if (st.t >= p.delay) {
            const ImVec2 home = splash_home(p, L, vp);
            springHome(p.x, p.y, p.vx, p.vy, home.x, home.y, kSpringK);
        } else {
            p.vx += std::sin(st.t * 4.0f + float(i) * 1.7f) * 40.0f * dt;
            p.vy += std::cos(st.t * 3.1f + float(i) * 2.3f) * 40.0f * dt;
        }
        shove(p.x, p.y, p.vx, p.vy);
        integrate(p.x, p.y, p.vx, p.vy);
    }

    // ── draw particles ──
    // Every square is drawn INSET (a ~12% gutter each side): the reference
    // intros always read as distinct little squares, and flush same-colour
    // cells melt into one solid shape — the exact complaint against v2.
    ImFont* font = ImGui::GetFont();
    auto pixel = [dl](float x, float y, float side, ImU32 col) {
        const float in = side * 0.12f;
        dl->AddRectFilled(ImVec2(x + in, y + in),
                          ImVec2(x + side - in, y + side - in), col);
    };
    for (int i = 0; i < st.count; ++i) {
        const SplashParticle& p = st.p[i];
        const ImVec2 home = splash_home(p, L, vp);
        const float hd = std::abs(home.x - p.x) + std::abs(home.y - p.y);
        const bool arrived = st.t >= p.delay && hd < 3.0f;
        const ImU32 bloodShade = (i % 3 == 0) ? kPalBloodDeep
                               : (i % 3 == 1) ? kPalBlood : kPalBloodHot;
        // A particle IS its full-size square from birth (the reference
        // intros fly whole cells, not sparks) — blood-dark and flickering
        // in flight, its true colour once seated.
        const float side = p.kind == 0 ? L.spx
                         : p.kind == 1 ? L.cell
                         : L.cell * 0.4f;
        if (p.kind == 3) {
            // Stray speck: swarms, then dies out as the emblem forms.
            const float a = 1.0f - (st.t - 3.0f) / 1.5f;
            if (a <= 0.0f) continue;
            const float aa = std::min(a, 1.0f)
                * (0.55f + 0.45f * std::sin(st.t * 11.0f + float(i) * 3.0f));
            ImU32 col = (bloodShade & 0x00FFFFFFu)
                | (ImU32(std::max(aa, 0.0f) * 255.0f) << 24);
            pixel(p.x, p.y, side, col);
            continue;
        }
        if (!arrived) {
            const float aa = 0.55f + 0.45f * std::sin(st.t * 11.0f + float(i) * 3.0f);
            const ImU32 col = (bloodShade & 0x00FFFFFFu) | (ImU32(aa * 255.0f) << 24);
            pixel(p.x, p.y, side, col);
            continue;
        }
        switch (p.kind) {
            case 0: {
                // Cooled into its material.
                const char cell = kSwordRows[p.gy][p.gx];
                const ImU32 col = cell == 'P' ? kPalBronze
                                : cell == 'G' ? kPalLeather
                                : cell == 'D' ? kPalFuller : kPalSteel;
                pixel(p.x, p.y, L.spx, col);
                break;
            }
            case 1:
                pixel(p.x, p.y, L.cell, kParchShades[i % 3]);
                break;
            default:
                pixel(p.x, p.y, side, kPalBloodDeep);
                break;
        }
    }

    // ── blood on the blade (after the emblem stands) ──
    // Pixels all the way down, and PARTICLES all the way down: each runnel
    // segment is born as a particle with a home on the blade, so the cursor
    // smears the blood off the sword and the spring drags it back — frozen
    // paint was the one thing on this screen the toy could not touch.
    if (st.t > 4.6f) {
        const float bt = st.t - 4.6f;
        const float guardY = L.swordY + 4.0f * L.spx;
        const float tipY = L.swordY + float(kSwordH) * L.spx;
        const float fullLen = tipY - guardY;
        const float runSpeed = L.spx * 3.2f;                       // px/s, scales
        st.bloodLen[0] = std::min(fullLen, bt * runSpeed);         // centre runnel
        st.bloodLen[1] = std::min(fullLen * 0.45f, bt * runSpeed * 0.55f);
        st.bloodLen[2] = std::min(fullLen * 0.30f, bt * runSpeed * 0.40f);
        const int cols[3] = {5, 4, 6};
        for (int k = 0; k < 3; ++k) {
            const int want = int(st.bloodLen[k] / L.spx);
            while (st.segSpawned[k] < want) {
                SplashBloodSeg* freeSeg = nullptr;
                for (SplashBloodSeg& s : st.bloodSegs)
                    if (!s.live) { freeSeg = &s; break; }
                if (!freeSeg) break;
                freeSeg->live = true;
                freeSeg->col = std::int8_t(k);
                freeSeg->idx = std::int8_t(st.segSpawned[k]);
                freeSeg->x = L.swordX + float(cols[k]) * L.spx + L.spx * 0.15f;
                freeSeg->y = guardY + float(st.segSpawned[k]) * L.spx;
                freeSeg->vx = 0.0f;
                freeSeg->vy = 0.0f;
                ++st.segSpawned[k];
            }
        }
        for (SplashBloodSeg& s : st.bloodSegs) {
            if (!s.live) continue;
            const float hx = L.swordX + float(cols[s.col]) * L.spx + L.spx * 0.15f;
            const float hy = guardY + float(s.idx) * L.spx;
            // The same projectile-then-spring law as the emblem, clinging a
            // touch harder — struck blood SPLATTERS off the blade and only
            // then crawls back.
            springHome(s.x, s.y, s.vx, s.vy, hx, hy, kSpringK * 1.5f);
            shove(s.x, s.y, s.vx, s.vy);
            integrate(s.x, s.y, s.vx, s.vy);
            const int j = int(s.idx) + int(s.col);
            const ImU32 shade = (j % 3 == 0) ? kPalBloodDeep
                              : (j % 3 == 1) ? kPalBlood : kPalBloodHot;
            pixel(s.x, s.y, L.spx * 0.7f, shade);
        }
        // Drops leave the point on their own beat and fall as squares —
        // and the cursor can bat them aside mid-air.
        if (st.bloodLen[0] >= fullLen) {
            st.dropTimer -= dt;
            if (st.dropTimer <= 0.0f) {
                for (SplashDrop& d : st.drops) {
                    if (d.sz > 0.0f) continue;
                    d.sz = L.spx * (0.35f + 0.30f * splash_rand01(st.dropRng));
                    d.x = L.swordX + 5.5f * L.spx
                        + (splash_rand01(st.dropRng) - 0.5f) * L.spx * 0.8f
                        - d.sz * 0.5f;
                    d.y = tipY;
                    d.vx = 0.0f;
                    d.vy = 0.0f;
                    break;
                }
                st.dropTimer = 0.35f + 0.55f * splash_rand01(st.dropRng);
            }
        }
        for (SplashDrop& d : st.drops) {
            if (d.sz <= 0.0f) continue;
            d.vy += 800.0f * dt;
            shove(d.x, d.y, d.vx, d.vy);
            d.vx *= std::pow(kFriction, dt * 60.0f);
            d.x += d.vx * dt;
            d.y += d.vy * dt;
            if (d.y > vp.y || d.x < -40.0f || d.x > vp.x + 40.0f) {
                d.sz = 0.0f;
                continue;
            }
            pixel(d.x, d.y, d.sz, kPalBloodHot);
        }
    }

    // ── subtitle + hint (once the emblem stands) ──
    // The subtitle sits BELOW the sword's point — the sword out-tops the
    // letters by design, so anchoring under the letter grid would run the
    // line straight through the blade.
    if (st.t > 4.0f) {
        const float a = std::min((st.t - 4.0f) / 1.0f, 1.0f);
        const char* sub = "An experimental indie studio";
        const ImVec2 ts = font->CalcTextSizeA(L.fontSize, FLT_MAX, 0.0f, sub);
        const ImU32 col = (kPalParchDim & 0x00FFFFFFu)
            | (ImU32(a * 255.0f) << 24);
        dl->AddText(font, L.fontSize,
                    ImVec2((vp.x - ts.x) * 0.5f,
                           L.swordY + float(kSwordH) * L.spx + 2.5f * L.cell),
                    col, sub);
        if (std::fmod(st.t, 1.2) < 0.7) {
            const char* hint = "[ press any key ]";
            const ImVec2 hs = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2((vp.x - hs.x) * 0.5f, vp.y - 44.0f),
                        kPalParchDim, hint);
        }
    }

    // ── fade-out and the exits ──
    // The auto-exit clocks IDLE time (owner: while you poke the pixels,
    // nothing closes): walk away and the screen excuses itself after the
    // show has stood a while; play with the mouse and it stays forever.
    // Only the KEYBOARD moves things along — the mouse is the toy.
    constexpr float kOutStart = 15.0f, kOutEnd = 16.5f;
    if (st.t > 5.5f && st.idleT > kOutStart) {   // show played AND hands off
        const float a = std::min((st.idleT - kOutStart) / (kOutEnd - kOutStart), 1.0f);
        ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(0, 0), vp, IM_COL32(0, 0, 0, int(a * 255.0f)));
        if (st.idleT > kOutEnd) {
            r.splashDone = true;
            return r;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        r.splashDone = true;
        return r;
    }
    if (splash_any_key_pressed()) {
        if (st.t < 4.0f) {
            // First key: assemble NOW — every spring engages at once.
            st.t = 4.0f;
            for (int i = 0; i < st.count; ++i)
                if (st.p[i].kind != 3) st.p[i].delay = 0.0f;
        } else {
            r.splashDone = true;
        }
    }
    return r;
}

// The pre-world slideshow. Layout derives from DisplaySize on every frame —
// deliberately nothing to rebuild on resize. Drawn straight onto the
// background/foreground draw lists: this screen has no windows, so a click
// anywhere is the page-turn.
ShellResult draw_intro_slides(IntroSlidesState& st) {
    ShellResult r{};
    const content::StoryDef& story = content::intro_story();
    if (story.slideCount == 0) { r.introFinished = true; return r; }
    if (st.slide >= int(story.slideCount)) st.slide = int(story.slideCount) - 1;

    const ImVec2 vp = viewport_size();
    auto* bg = ImGui::GetBackgroundDrawList();
    auto* fg = ImGui::GetForegroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), vp, kPalNight);

    // ── the frame: upper screen, bronze border, art aspect-fit inside ──
    const float margin = std::max(24.0f, vp.x / 14.0f);
    const float artX = margin;
    const float artY = vp.y * 0.06f;
    const float artW = vp.x - margin * 2.0f;
    const float artH = std::max(140.0f, vp.y * 0.62f);
    bg->AddRectFilled(ImVec2(artX, artY), ImVec2(artX + artW, artY + artH),
                      IM_COL32(8, 7, 4, 255));
    bg->AddRect(ImVec2(artX, artY), ImVec2(artX + artW, artY + artH),
                kPalBronze, 0.0f, 0, 1.0f);

    const content::StorySlide& slide = story.slides[st.slide];
    if (const UiImage* img = ui_image_for(slide.image)) {
        const float innerW = artW - 8.0f;
        const float innerH = artH - 8.0f;
        const float scale = std::min(innerW / float(img->w), innerH / float(img->h));
        const ImVec2 size(float(img->w) * scale, float(img->h) * scale);
        const ImVec2 p0(artX + (artW - size.x) * 0.5f, artY + (artH - size.y) * 0.5f);
        bg->AddImage(img->tex, p0, ImVec2(p0.x + size.x, p0.y + size.y));
    } else {
        const char* pending = "PANEL PENDING";
        const ImVec2 ts = ImGui::CalcTextSize(pending);
        bg->AddText(ImVec2(artX + (artW - ts.x) * 0.5f, artY + (artH - ts.y) * 0.5f),
                    kPalParchDim, pending);
    }

    // ── the caption: typewriter under the frame, wrapped to the art width ──
    // (Any future localisation must translate BEFORE the prefix cut, never
    // the cut-off prefix — the other reference lesson.)
    const char* full = slide.narration ? slide.narration : "";
    const std::size_t fullCp = utf8_length(full);
    st.typedChars += ImGui::GetIO().DeltaTime * kIntroTypeCharsPerSec;
    const std::size_t shownCp = std::min(fullCp, std::size_t(st.typedChars));
    const bool typing = shownCp < fullCp;
    const float capY = artY + artH + 18.0f;
    ImFont* font = ImGui::GetFont();
    const float capSize = ImGui::GetFontSize() * 1.25f;
    fg->AddText(font, capSize, ImVec2(artX + 4.0f, capY), kPalParchment,
                full, full + utf8_prefix_bytes(full, shownCp), artW - 8.0f);

    // ── slide marks: one bronze dash per slide, the current one grass ──
    const float markW = 12.0f, markGap = 18.0f;
    const float marksX = (vp.x - float(story.slideCount) * markGap) * 0.5f;
    for (int i = 0; i < int(story.slideCount); ++i) {
        const ImU32 c = i == st.slide ? kPalGrass : kPalBronze;
        fg->AddRectFilled(ImVec2(marksX + float(i) * markGap, vp.y - 42.0f),
                          ImVec2(marksX + float(i) * markGap + markW, vp.y - 38.0f), c);
    }

    // ── hints: steady while typing, slow blink once the line is done ──
    const bool blinkOn = std::fmod(ImGui::GetTime(), 1.2) < 0.7;
    if (typing || blinkOn) {
        const char* hint = typing ? "Click to reveal" : "Click to continue";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        fg->AddText(ImVec2((vp.x - ts.x) * 0.5f, vp.y - 26.0f), kPalParchDim, hint);
    }
    fg->AddText(ImVec2(margin, vp.y - 26.0f), kPalParchDim, "Esc  skip");

    // ── input: first tap finishes the line, the second turns the page ──
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        r.introFinished = true;
        return r;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Space)) {
        if (typing) {
            st.typedChars = float(fullCp);
        } else if (st.slide + 1 < int(story.slideCount)) {
            ++st.slide;
            st.typedChars = 0.0f;
        } else {
            r.introFinished = true;
        }
    }
    return r;
}


} // namespace sm::ui
