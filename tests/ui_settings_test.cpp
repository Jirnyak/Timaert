// Unit tests for sm::ui::UiSettings — the universal UI settings registry (ONE
// table drives toggle + scale for every HUD element across the macroworld and
// the microworld). Pure persistence/state logic: spec-seeded defaults, the
// forgiving text-KV load/save round-trip, unknown-key / out-of-range / partial
// tolerance, non-scalable handling, and reset_defaults(). No window or ImGui
// context is created — draw_ui_settings_panel is compiled (its symbols link via
// the ImGui core TUs) but never called.
//
// Assertions go through tests/check.h — CHECK + sm::test::report.

#include "check.h"

#include "ui/ui_settings.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace sm::ui;

static bool approx(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// Overwrite `path` with raw bytes (for the forgiving-parser cases).
static void write_file(const std::string& path, const char* body) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fputs(body, f); std::fclose(f); }
}

// True iff two settings agree on every element's visibility AND scale.
static bool same_state(const UiSettings& a, const UiSettings& b) {
    for (std::size_t i = 0; i < kUiElementCount; ++i) {
        const auto id = static_cast<UiElementId>(i);
        if (a.visible(id) != b.visible(id)) return false;
        if (!approx(a.scale(id), b.scale(id))) return false;
    }
    return true;
}

int main() {
    const std::string tmp = "ui_settings_test_tmp.cfg";
    std::remove(tmp.c_str());

    // ── a fresh instance seeds every element from its spec defaults ──────────
    {
        UiSettings s;
        for (std::size_t i = 0; i < kUiElementCount; ++i) {
            const auto id = static_cast<UiElementId>(i);
            const UiElementSpec& sp = ui_spec(id);
            CHECK(s.visible(id) == sp.defaultVisible,
                  "fresh instance: visible == spec default");
            CHECK(approx(s.scale(id), sp.scaleDefault),
                  "fresh instance: scale == spec default");
        }
    }

    // ── reset_defaults() restores the full spec baseline after mutation ──────
    {
        UiSettings s;
        s.mut(UiElementId::SubMinimap).visible = false;
        s.mut(UiElementId::SubMinimap).scale   = 1.75f;
        s.mut(UiElementId::PlayerHud).visible  = false;
        s.reset_defaults();
        UiSettings fresh;
        CHECK(same_state(s, fresh),
              "reset_defaults() restores every element to spec defaults");
    }

    // ── a missing file is non-fatal: load returns false, state untouched ─────
    {
        std::remove(tmp.c_str());
        UiSettings s;
        s.mut(UiElementId::SubMinimap).visible = false;  // pre-set, non-default
        const bool ok = load_ui_settings(s, tmp);
        CHECK(!ok, "load returns false when the file is absent");
        CHECK(s.visible(UiElementId::SubMinimap) == false,
              "absent file leaves the caller's state untouched");
    }

    // ── save → load round-trips every element exactly ───────────────────────
    {
        UiSettings src;
        src.mut(UiElementId::SubMinimap).visible = false;
        src.mut(UiElementId::SubMinimap).scale   = 1.50f;  // within [0.60, 2.00]
        src.mut(UiElementId::PlayerHud).scale    = 0.75f;  // within [0.60, 1.80]
        src.mut(UiElementId::PanelCodex).visible = false;
        CHECK(save_ui_settings(src, tmp), "save returns true on success");

        UiSettings dst;  // starts at defaults; load must overwrite from file
        CHECK(load_ui_settings(dst, tmp), "load returns true when the file exists");
        CHECK(same_state(src, dst), "save/load round-trips every element");
    }

    // ── forgiving parser: comments, blanks, unknown keys, partial lines ──────
    {
        write_file(tmp,
            "# timaert ui prefs v1\n"
            "\n"
            "   # indented comment line\n"
            "totally.unknown.key 0 1.23\n"   // unknown key -> ignored, no crash
            "sub.minimap 0 1.25\n"           // known: off + scale 1.25
            "hud.player 0\n"                 // vis only -> scale keeps default
            "loose-token-no-fields\n");      // key-only unknown -> ignored
        UiSettings s;
        CHECK(load_ui_settings(s, tmp), "load reads a hand-authored file");
        CHECK(s.visible(UiElementId::SubMinimap) == false,
              "known key applies visibility");
        CHECK(approx(s.scale(UiElementId::SubMinimap), 1.25f),
              "known key applies scale");
        CHECK(s.visible(UiElementId::PlayerHud) == false,
              "vis-only line applies visibility");
        CHECK(approx(s.scale(UiElementId::PlayerHud), 1.0f),
              "vis-only line leaves scale at default");
        CHECK(s.visible(UiElementId::BottomToolbar) == true,
              "an unmentioned element keeps its default visibility");
        CHECK(approx(s.scale(UiElementId::BottomToolbar), 1.0f),
              "an unmentioned element keeps its default scale");
    }

    // ── out-of-range scales clamp into [scaleMin, scaleMax] ──────────────────
    {
        write_file(tmp,
            "sub.minimap 1 9.99\n"   // above max 2.00 -> clamps down
            "hud.player 1 0.01\n");  // below min 0.60 -> clamps up
        UiSettings s;
        load_ui_settings(s, tmp);
        CHECK(approx(s.scale(UiElementId::SubMinimap), 2.00f),
              "over-max scale clamps to scaleMax");
        CHECK(approx(s.scale(UiElementId::PlayerHud), 0.60f),
              "under-min scale clamps to scaleMin");
    }

    // ── a non-scalable element toggles, but ignores any scale in the file ────
    {
        write_file(tmp, "macro.overlay 0 1.75\n");  // MacroOverlay: scalable=false
        UiSettings s;
        load_ui_settings(s, tmp);
        CHECK(s.visible(UiElementId::MacroOverlay) == false,
              "non-scalable element still toggles visibility");
        CHECK(approx(s.scale(UiElementId::MacroOverlay),
                     ui_spec(UiElementId::MacroOverlay).scaleDefault),
              "non-scalable element ignores a file scale (stays at default)");
    }

    std::remove(tmp.c_str());
    return sm::test::report("ui_settings_test");
}
