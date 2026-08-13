#include "check.h"
#include "macro/audio.h"

#if defined(TIMAERT_HAS_SDL_MIXER)
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

#include <cstdio>

namespace {

int fail(const char* msg, const sm::AudioSystem& audio) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/audio_runtime_test.cpp", 0);
    return 1;
}

} // namespace

int main() {
#if !defined(TIMAERT_HAS_SDL_MIXER)
    std::printf("SKIP audio_runtime_test SDL_mixer unavailable\n");
    return 0;
#else
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    sm::AudioSystem audio;
    audio.set_music_volume(0.25f);
    audio.set_sfx_volume(0.50f);

    if (!audio.init()) return fail("init failed", audio);
    if (!audio.is_initialized()) return fail("init did not mark initialized", audio);
    if (!audio.music_loaded(sm::MusicId::Explore)) return fail("explore not loaded", audio);
    if (!audio.music_loaded(sm::MusicId::EmpireTheme)) return fail("empire not loaded", audio);
    if (!audio.music_loaded(sm::MusicId::Subworld)) return fail("subworld not loaded", audio);
    if (!audio.sfx_loaded(sm::SfxId::Witch)) return fail("witch sfx not loaded", audio);

    if (!audio.play_music(sm::MusicId::Explore, 0)) {
        return fail("explore play failed", audio);
    }
    if (!audio.music_playing()) return fail("music not playing after explore", audio);
    if (audio.current_music() != sm::MusicId::Explore) {
        return fail("explore current music not tracked", audio);
    }
    if (!audio.play_music(sm::MusicId::Subworld, 0)) {
        return fail("subworld play failed", audio);
    }
    if (audio.current_music() != sm::MusicId::Subworld) {
        return fail("subworld current music not tracked", audio);
    }
    if (!audio.play_sfx(sm::SfxId::Witch, -1)) {
        return fail("witch sfx play failed", audio);
    }

    audio.stop_music(0);
    if (audio.music_playing()) return fail("music still playing after stop", audio);
    if (audio.current_music() != sm::MusicId::Count) {
        return fail("stop did not clear current music", audio);
    }

    audio.shutdown();
    if (audio.is_initialized()) return fail("shutdown left initialized", audio);
    if (audio.music_loaded(sm::MusicId::Explore)) {
        return fail("shutdown left music loaded", audio);
    }
    if (audio.sfx_loaded(sm::SfxId::Witch)) {
        return fail("shutdown left sfx loaded", audio);
    }

    {
        sm::AudioSystem scoped;
        if (!scoped.init()) return fail("scoped init failed", scoped);
        if (!scoped.play_music(sm::MusicId::Explore, 0)) {
            return fail("scoped explore play failed", scoped);
        }
    }

    sm::AudioSystem afterScoped;
    if (!afterScoped.init()) return fail("post-destructor init failed", afterScoped);
    afterScoped.shutdown();

    std::printf("OK audio_runtime_test dummy_driver init decode play stop shutdown\n");
    CHECK(true, "every gate above held");
    return sm::test::report("audio_runtime_test");
#endif
}
