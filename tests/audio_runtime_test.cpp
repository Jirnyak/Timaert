// Locks the RUNTIME contract of the audio system (macro/audio.h) on a live
// device: init, the loaded set, what playback tracks, and that shutdown really
// lets go. The dummy SDL driver makes this runnable headless, so a regression
// in the mixer wiring is caught here and not in a player's ears.
//
// There is no SKIP path. SDL_mixer is a HARD dependency of this project —
// CMakeLists.txt fails the configure outright when it is missing ("SDL_mixer
// dependency missing: native audio requires SDL2_mixer"), so a build that
// reaches this file always has a mixer. The `#if !defined(TIMAERT_HAS_SDL_MIXER)
// -> printf("SKIP") -> return 0` branch that used to stand here was legacy of
// the deleted Emscripten target, and it was worse than dead: it was an exit
// that bypassed report() with ZERO checks — exactly the silently-green shape
// check.h exists to make impossible (§8 п.2).

#include "check.h"
#include "macro/audio.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

namespace {

// One live session, in the order the game itself walks it: bring the device up,
// play, swap the track, stop, shut down. Everything after a failed init is
// meaningless, so that one gate bails — the rest each break alone.
void test_a_live_device_plays_music_and_sfx() {
    using namespace sm;
    AudioSystem audio;
    audio.set_music_volume(0.25f);
    audio.set_sfx_volume(0.50f);

    CHECK_OR_RETURN(audio.init(), "the audio system comes up on the device");
    CHECK(audio.is_initialized(), "a successful init SAYS it is initialized");

    // The live set only (canon audit 2026-08-29): EmpireTheme and the Witch
    // sfx were loaded-but-never-played dead rows; the playback laws below are
    // what this test exists for.
    CHECK(audio.music_loaded(MusicId::Explore),
          "the explore theme is loaded — the macro map is never silent");
    CHECK(audio.music_loaded(MusicId::Subworld),
          "the subworld theme is loaded — the descent has its own music");

    CHECK(audio.play_music(MusicId::Explore, 0), "the explore theme plays");
    CHECK(audio.music_playing(), "and the system reports music as playing");
    CHECK(audio.current_music() == MusicId::Explore,
          "the system names the track it is actually playing");

    CHECK(audio.play_music(MusicId::Subworld, 0),
          "a second theme takes over without a stop in between");
    CHECK(audio.current_music() == MusicId::Subworld,
          "and the named track FOLLOWS the swap, it does not lag behind");

    // An out-of-range id must refuse loudly even on a live device.
    CHECK(!audio.play_sfx(SfxId::Count, -1),
          "an id past the end of the table plays nothing");

    // THE fallback law, proven live: no melee .wav ships today, yet every row
    // is loaded — the procedural default answered for the absent files — and
    // plays on the dummy device. This is the guarantee the melee tick leans
    // on: queue_sfx never queues a sound that cannot exist.
    CHECK(audio.sfx_loaded(SfxId::MeleeSwing),
          "every sfx row is loaded even with no file: the swing");
    CHECK(audio.sfx_loaded(SfxId::MeleeHit),
          "every sfx row is loaded even with no file: the hit");
    CHECK(audio.sfx_loaded(SfxId::MeleeBlocked),
          "every sfx row is loaded even with no file: the block");
    CHECK(audio.play_sfx(SfxId::MeleeSwing, -1),
          "the procedural default is a PLAYABLE sound: the swing");
    CHECK(audio.play_sfx(SfxId::MeleeHit, -1),
          "the procedural default is a playable sound: the hit");
    CHECK(audio.play_sfx(SfxId::MeleeBlocked, -1),
          "the procedural default is a playable sound: the block");

    audio.stop_music(0);
    CHECK(!audio.music_playing(), "a stopped track stops playing");
    CHECK(audio.current_music() == MusicId::Count,
          "and the system stops naming a current track");

    audio.shutdown();
    CHECK(!audio.is_initialized(), "shutdown un-initializes the system");
    CHECK(!audio.music_loaded(MusicId::Explore),
          "shutdown releases what it loaded — nothing is left holding the "
          "device");
}

// The destructor must free the device as thoroughly as shutdown() does, or the
// second system in a process comes up mute.
void test_a_destroyed_system_leaves_the_device_reusable() {
    using namespace sm;
    {
        AudioSystem scoped;
        CHECK_OR_RETURN(scoped.init(), "a second system opens the device");
        CHECK(scoped.play_music(MusicId::Explore, 0),
              "and plays through it");
    }   // destructor, no explicit shutdown

    AudioSystem afterScoped;
    CHECK(afterScoped.init(),
          "a system destroyed WITHOUT shutdown still frees the device for the "
          "next one");
    afterScoped.shutdown();
}

} // namespace

int main() {
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    test_a_live_device_plays_music_and_sfx();
    test_a_destroyed_system_leaves_the_device_reusable();
    return sm::test::report("audio_runtime_test");
}
