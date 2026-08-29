// Unit tests for sm::ui::Keymap — the universal rebindable keymap (ONE table
// drives every game key across the macroworld and the microworld). Pure
// state/persistence logic: spec-seeded defaults, the one-key-one-meaning-
// per-world steal rule, scope gating, the forgiving text-KV load/save
// round-trip and its tolerance cases, and reset_defaults(). No window or ImGui
// context is created — draw_keymap_panel is compiled but never called.
//
// Style mirrors tests/ui_settings_test.cpp: no framework, plain main().

#include "check.h"

#include "ui/keymap.h"

#include <cstdio>
#include <string>

using namespace sm::ui;

// Overwrite `path` with raw bytes (for the forgiving-parser cases).
static void write_file(const std::string& path, const char* body) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fputs(body, f); std::fclose(f); }
}

// True iff two keymaps agree on every action's binding.
static bool same_state(const Keymap& a, const Keymap& b) {
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const auto id = static_cast<ActionId>(i);
        if (a.get(id) != b.get(id)) return false;
    }
    return true;
}

int main() {
    const std::string tmp = "keymap_test_tmp.cfg";
    std::remove(tmp.c_str());

    // ── a fresh instance seeds every action from its spec default ────────────
    {
        Keymap m;
        for (std::size_t i = 0; i < kActionCount; ++i) {
            const auto id = static_cast<ActionId>(i);
            CHECK(m.get(id) == action_spec(id).def,
                   "fresh instance: binding == spec default");
        }
    }

    // ── scope_active: Both listens everywhere, Macro/Sub in their world ──────
    {
        CHECK(scope_active(UiScope::Both, false) && scope_active(UiScope::Both, true),
               "Both listens in both worlds");
        CHECK(scope_active(UiScope::Macro, false) && !scope_active(UiScope::Macro, true),
               "Macro listens on the map only");
        CHECK(!scope_active(UiScope::Sub, false) && scope_active(UiScope::Sub, true),
               "Sub listens underground only");
    }

    // ── the steal rule: one key, one meaning per world ───────────────────────
    {
        // Same world: rebinding Pause (Macro) onto Rest's Z robs Rest.
        Keymap m;
        m.set(ActionId::Pause, SDL_SCANCODE_Z);
        CHECK(m.get(ActionId::Pause) == SDL_SCANCODE_Z,
               "rebind applies to the target action");
        CHECK(m.get(ActionId::Rest) == SDL_SCANCODE_UNKNOWN,
               "same-world holder of the key is left unbound");
    }
    {
        // Different worlds never conflict: Jump (Sub) may share Pause's
        // (Macro) Space — that IS the shipping default — and binding a Sub
        // action onto a Macro key robs nobody.
        Keymap m;
        CHECK(m.get(ActionId::Pause) == SDL_SCANCODE_SPACE
               && m.get(ActionId::Jump) == SDL_SCANCODE_SPACE,
               "defaults already share Space across worlds");
        m.set(ActionId::Interact, SDL_SCANCODE_Z);   // Z = Rest (Macro)
        CHECK(m.get(ActionId::Rest) == SDL_SCANCODE_Z,
               "cross-world rebind leaves the other world's action alone");
    }
    {
        // A Both-scope action overlaps EVERY world: binding Codex onto A must
        // rob both PanLeft (Macro, A) and Attack (Sub, A).
        Keymap m;
        m.set(ActionId::Codex, SDL_SCANCODE_A);
        CHECK(m.get(ActionId::Codex) == SDL_SCANCODE_A, "Both-scope rebind applies");
        CHECK(m.get(ActionId::PanLeft) == SDL_SCANCODE_UNKNOWN,
               "Both-scope rebind robs the macro holder");
        CHECK(m.get(ActionId::Attack) == SDL_SCANCODE_UNKNOWN,
               "Both-scope rebind robs the sub holder");
    }
    {
        // Unbinding (scancode 0) robs nobody.
        Keymap m;
        m.set(ActionId::Map, SDL_SCANCODE_UNKNOWN);
        CHECK(m.get(ActionId::Map) == SDL_SCANCODE_UNKNOWN, "unbind applies");
        CHECK(m.get(ActionId::Quests) == SDL_SCANCODE_Q,
               "unbind steals from nobody");
    }

    // ── reset_defaults() restores the full spec baseline after mutation ──────
    {
        Keymap m;
        m.set(ActionId::Pause, SDL_SCANCODE_Z);
        m.set(ActionId::Codex, SDL_SCANCODE_A);
        m.reset_defaults();
        Keymap fresh;
        CHECK(same_state(m, fresh),
               "reset_defaults() restores every binding to spec default");
    }

    // ── a missing file is non-fatal: load returns false, state untouched ─────
    {
        std::remove(tmp.c_str());
        Keymap m;
        m.set(ActionId::Map, SDL_SCANCODE_N);   // pre-set, non-default
        const bool ok = load_keymap(m, tmp);
        CHECK(!ok, "load returns false when the file is absent");
        CHECK(m.get(ActionId::Map) == SDL_SCANCODE_N,
               "absent file leaves the caller's state untouched");
    }

    // ── save → load round-trips every binding exactly ────────────────────────
    {
        Keymap src;
        src.set(ActionId::Pause, SDL_SCANCODE_Z);      // robs Rest on the way
        src.set(ActionId::Attack, SDL_SCANCODE_F);
        src.set(ActionId::Map, SDL_SCANCODE_UNKNOWN);  // an unbound row survives
        CHECK(save_keymap(src, tmp), "save returns true on success");

        Keymap dst;  // starts at defaults; load must overwrite from file
        CHECK(load_keymap(dst, tmp), "load returns true when the file exists");
        CHECK(same_state(src, dst), "save/load round-trips every binding");
    }

    // ── forgiving parser: comments, blanks, unknown keys, junk values ────────
    {
        write_file(tmp,
            "# timaert keymap v1\n"
            "\n"
            "   # indented comment line\n"
            "totally.unknown.key 30\n"     // unknown action -> ignored, no crash
            "act.map 17\n"                 // known: SDL_SCANCODE_N
            "act.codex 99999\n"            // out-of-range scancode -> ignored
            "act.quests -3\n"              // negative -> ignored
            "act.rest\n"                   // value-less line -> ignored
            "loose-token-no-fields\n");    // key-only unknown -> ignored
        Keymap m;
        CHECK(load_keymap(m, tmp), "load reads a hand-authored file");
        CHECK(m.get(ActionId::Map) == SDL_SCANCODE_N, "known key applies");
        CHECK(m.get(ActionId::Codex) == action_spec(ActionId::Codex).def,
               "out-of-range scancode keeps the default");
        CHECK(m.get(ActionId::Quests) == action_spec(ActionId::Quests).def,
               "negative scancode keeps the default");
        CHECK(m.get(ActionId::Rest) == action_spec(ActionId::Rest).def,
               "value-less line keeps the default");
        CHECK(m.get(ActionId::Save) == action_spec(ActionId::Save).def,
               "an unmentioned action keeps its default");
    }

    // ── a hand-edited duplicate within one world: the LAST line wins ─────────
    {
        write_file(tmp,
            "act.panleft 4\n"    // SDL_SCANCODE_A (its own default)
            "act.panright 4\n"); // the same A -> robs PanLeft, keeps the key
        Keymap m;
        load_keymap(m, tmp);
        CHECK(m.get(ActionId::PanRight) == SDL_SCANCODE_A,
               "duplicate resolves to the last line");
        CHECK(m.get(ActionId::PanLeft) == SDL_SCANCODE_UNKNOWN,
               "the earlier holder is left visibly unbound");
    }

    std::remove(tmp.c_str());
    return sm::test::report("keymap_test");
}
