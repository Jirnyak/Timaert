#include "macro/audio.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "audio_contract_test FAIL: %s\n", msg);
    return 1;
}

bool close_enough(float a, float b) {
    const float d = a > b ? a - b : b - a;
    return d < 0.0001f;
}

bool same_text(const char* a, const char* b) {
    if (!a || !b) return false;
    return std::strcmp(a, b) == 0;
}

bool contains_text(const char* text, const char* needle) {
    if (!text || !needle) return false;
    return std::strstr(text, needle) != nullptr;
}

bool asset_exists(const char* file) {
    if (!file || file[0] == '\0') return false;
    constexpr const char* kPrefixes[] = {
        "../public/assets/sound/",
        "public/assets/sound/",
        "assets/sound/",
        "../../public/assets/sound/",
    };

    char path[512];
    for (const char* prefix : kPrefixes) {
        const int n = std::snprintf(path, sizeof(path), "%s%s", prefix, file);
        if (n <= 0 || std::size_t(n) >= sizeof(path)) continue;
        std::FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        std::fclose(f);
        return true;
    }
    return false;
}

bool asset_has_min_bytes(const char* file, long minBytes) {
    if (!file || file[0] == '\0') return false;
    constexpr const char* kPrefixes[] = {
        "../public/assets/sound/",
        "public/assets/sound/",
        "assets/sound/",
        "../../public/assets/sound/",
    };

    char path[512];
    for (const char* prefix : kPrefixes) {
        const int n = std::snprintf(path, sizeof(path), "%s%s", prefix, file);
        if (n <= 0 || std::size_t(n) >= sizeof(path)) continue;
        std::FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            continue;
        }
        const long size = std::ftell(f);
        std::fclose(f);
        return size >= minBytes;
    }
    return false;
}

} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<sm::AudioSystem>);
    static_assert(!std::is_copy_assignable_v<sm::AudioSystem>);
    static_assert(!std::is_move_constructible_v<sm::AudioSystem>);
    static_assert(!std::is_move_assignable_v<sm::AudioSystem>);
    static_assert(static_cast<int>(sm::MusicId::Count) == 3);
    static_assert(static_cast<int>(sm::SfxId::Count) == 1);

    if (!same_text(sm::music_key(sm::MusicId::Explore), "explore")) {
        return fail("explore music key changed");
    }
    if (!same_text(sm::music_key(sm::MusicId::EmpireTheme), "empire_theme")) {
        return fail("empire music key changed");
    }
    if (!same_text(sm::music_key(sm::MusicId::Subworld), "subworld")) {
        return fail("subworld music key changed");
    }
    if (!same_text(sm::sfx_key(sm::SfxId::Witch), "witch")) {
        return fail("witch sfx key changed");
    }
    if (sm::music_key(sm::MusicId::Count) || sm::music_file(sm::MusicId::Count)) {
        return fail("invalid music id returned metadata");
    }
    if (sm::sfx_key(sm::SfxId::Count) || sm::sfx_file(sm::SfxId::Count)) {
        return fail("invalid sfx id returned metadata");
    }

    constexpr long kMinAudioAssetBytes = 64 * 1024;
    if (!same_text(sm::music_file(sm::MusicId::Explore), "15-dungeon-suno.mp3")
        || !asset_exists(sm::music_file(sm::MusicId::Explore))
        || !asset_has_min_bytes(sm::music_file(sm::MusicId::Explore),
                                kMinAudioAssetBytes)) {
        return fail("explore music asset missing");
    }
    if (!same_text(sm::music_file(sm::MusicId::EmpireTheme), "empire-theme.mp3")
        || !asset_exists(sm::music_file(sm::MusicId::EmpireTheme))
        || !asset_has_min_bytes(sm::music_file(sm::MusicId::EmpireTheme),
                                kMinAudioAssetBytes)) {
        return fail("empire music asset missing");
    }
    if (!same_text(sm::music_file(sm::MusicId::Subworld), "subworld.mp3")
        || !asset_exists(sm::music_file(sm::MusicId::Subworld))
        || !asset_has_min_bytes(sm::music_file(sm::MusicId::Subworld),
                                kMinAudioAssetBytes)) {
        return fail("subworld music asset missing");
    }
    if (!same_text(sm::sfx_file(sm::SfxId::Witch), "witch.mp3")
        || !asset_exists(sm::sfx_file(sm::SfxId::Witch))
        || !asset_has_min_bytes(sm::sfx_file(sm::SfxId::Witch),
                                kMinAudioAssetBytes)) {
        return fail("witch sfx asset missing");
    }

    sm::AudioSystem audio;

    if (audio.is_initialized()) return fail("fresh audio starts initialized");
    if (audio.music_playing()) return fail("fresh audio reports music playing");
    if (audio.current_music() != sm::MusicId::Count) {
        return fail("fresh audio has current music");
    }
    if (audio.play_music(sm::MusicId::Explore, 0)) {
        return fail("play_music succeeds before init");
    }
    if (!contains_text(audio.last_error(), "audio not initialized")) {
        return fail("play_music did not explain preinit failure");
    }
    if (audio.play_sfx(sm::SfxId::Witch, -1)) {
        return fail("play_sfx succeeds before init");
    }
    if (!contains_text(audio.last_error(), "audio not initialized")) {
        return fail("play_sfx did not explain preinit failure");
    }
    if (audio.play_music(sm::MusicId::Count, 0)) {
        return fail("invalid music id succeeded");
    }
    if (!contains_text(audio.last_error(), "invalid music id")) {
        return fail("invalid music id did not set error");
    }
    if (audio.play_sfx(sm::SfxId::Count, -1)) {
        return fail("invalid sfx id succeeded");
    }
    if (!contains_text(audio.last_error(), "invalid sfx id")) {
        return fail("invalid sfx id did not set error");
    }

    audio.set_master_volume(2.0f);
    audio.set_music_volume(-1.0f);
    audio.set_sfx_volume(0.25f);
    if (!close_enough(audio.master_volume(), 1.0f)) {
        return fail("master volume did not clamp high");
    }
    if (!close_enough(audio.music_volume(), 0.0f)) {
        return fail("music volume did not clamp low");
    }
    if (!close_enough(audio.sfx_volume(), 0.25f)) {
        return fail("sfx volume changed unexpectedly");
    }

    if (audio.muted()) return fail("fresh audio starts muted");
    if (!audio.toggle_muted()) return fail("toggle_muted did not mute");
    if (!audio.muted()) return fail("muted flag not persisted");
    audio.set_muted(false);
    if (audio.muted()) return fail("set_muted(false) failed");

    audio.stop_music(0);
    if (audio.music_playing()) {
        return fail("stop_music before init left music playing");
    }
    if (audio.current_music() != sm::MusicId::Count) {
        return fail("stop_music before init changed state incorrectly");
    }

    std::printf("OK audio_contract_test metadata assets volumes mute preinit contract\n");
    return 0;
}
