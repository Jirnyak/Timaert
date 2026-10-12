#include "check.h"
#include "macro/audio.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace {

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
    // The LIVE set: two music rows, the melee feedback trio (owner
    // 2026-09-06) and the 2026-09-09 trio (cast / player-hurt / death, the
    // owner's SfxId queue) — every row is played by a real call site. SFX
    // rows follow THE fallback law: the FILE may be absent (a procedural
    // default is synthesized at init — audio.cpp synth_sfx_chunk), so this
    // test demands the metadata, never the bytes on disk.
    static_assert(static_cast<int>(sm::MusicId::Count) == 2);
    static_assert(static_cast<int>(sm::SfxId::Count) == 6);

    const bool ok1 =
        !(!same_text(sm::music_key(sm::MusicId::Explore), "explore"));
    CHECK(ok1, "explore music key changed");
    if (!ok1) return 1;
    const bool ok2 =
        !(!same_text(sm::music_key(sm::MusicId::Subworld), "subworld"));
    CHECK(ok2, "subworld music key changed");
    if (!ok2) return 1;
    const bool ok3 = !(!same_text(sm::sfx_key(sm::SfxId::MeleeSwing), "melee-swing")
                       || !same_text(sm::sfx_file(sm::SfxId::MeleeSwing), "melee-swing.wav"));
    CHECK(ok3, "melee-swing sfx row changed");
    if (!ok3) return 1;
    const bool ok4 = !(!same_text(sm::sfx_key(sm::SfxId::MeleeHit), "melee-hit")
                       || !same_text(sm::sfx_file(sm::SfxId::MeleeHit), "melee-hit.wav"));
    CHECK(ok4, "melee-hit sfx row changed");
    if (!ok4) return 1;
    const bool ok5 = !(!same_text(sm::sfx_key(sm::SfxId::MeleeBlocked), "melee-blocked")
                       || !same_text(sm::sfx_file(sm::SfxId::MeleeBlocked),
                                     "melee-blocked.wav"));
    CHECK(ok5, "melee-blocked sfx row changed");
    if (!ok5) return 1;
    const bool ok6 = !(!same_text(sm::sfx_key(sm::SfxId::SpellCast), "spell-cast")
                       || !same_text(sm::sfx_file(sm::SfxId::SpellCast), "spell-cast.wav"));
    CHECK(ok6, "spell-cast sfx row changed");
    if (!ok6) return 1;
    const bool ok7 = !(!same_text(sm::sfx_key(sm::SfxId::PlayerHurt), "player-hurt")
                       || !same_text(sm::sfx_file(sm::SfxId::PlayerHurt), "player-hurt.wav"));
    CHECK(ok7, "player-hurt sfx row changed");
    if (!ok7) return 1;
    const bool ok8 = !(!same_text(sm::sfx_key(sm::SfxId::Death), "death")
                       || !same_text(sm::sfx_file(sm::SfxId::Death), "death.wav"));
    CHECK(ok8, "death sfx row changed");
    if (!ok8) return 1;
    const bool ok9 =
        !(sm::music_key(sm::MusicId::Count) || sm::music_file(sm::MusicId::Count));
    CHECK(ok9, "invalid music id returned metadata");
    if (!ok9) return 1;
    const bool ok10 =
        !(sm::sfx_key(sm::SfxId::Count) || sm::sfx_file(sm::SfxId::Count));
    CHECK(ok10, "invalid sfx id returned metadata");
    if (!ok10) return 1;

    constexpr long kMinAudioAssetBytes = 64 * 1024;
    const bool ok11 = !(!same_text(sm::music_file(sm::MusicId::Explore), "15-dungeon-suno.mp3")
                        || !asset_exists(sm::music_file(sm::MusicId::Explore))
                        || !asset_has_min_bytes(sm::music_file(sm::MusicId::Explore),
                                                kMinAudioAssetBytes));
    CHECK(ok11, "explore music asset missing");
    if (!ok11) return 1;
    const bool ok12 = !(!same_text(sm::music_file(sm::MusicId::Subworld), "subworld.mp3")
                        || !asset_exists(sm::music_file(sm::MusicId::Subworld))
                        || !asset_has_min_bytes(sm::music_file(sm::MusicId::Subworld),
                                                kMinAudioAssetBytes));
    CHECK(ok12, "subworld music asset missing");
    if (!ok12) return 1;

    sm::AudioSystem audio;

    const bool ok13 = !(audio.is_initialized());
    CHECK(ok13, "fresh audio starts initialized");
    if (!ok13) return 1;
    const bool ok14 = !(audio.music_playing());
    CHECK(ok14, "fresh audio reports music playing");
    if (!ok14) return 1;
    const bool ok15 = !(audio.current_music() != sm::MusicId::Count);
    CHECK(ok15, "fresh audio has current music");
    if (!ok15) return 1;
    const bool ok16 = !(audio.play_music(sm::MusicId::Explore, 0));
    CHECK(ok16, "play_music succeeds before init");
    if (!ok16) return 1;
    const bool ok17 =
        !(!contains_text(audio.last_error(), "audio not initialized"));
    CHECK(ok17, "play_music did not explain preinit failure");
    if (!ok17) return 1;
    const bool ok18 = !(audio.play_sfx(sm::SfxId::MeleeSwing, -1));
    CHECK(ok18, "play_sfx succeeds before init");
    if (!ok18) return 1;
    const bool ok19 =
        !(!contains_text(audio.last_error(), "audio not initialized"));
    CHECK(ok19, "play_sfx did not explain preinit failure");
    if (!ok19) return 1;
    const bool ok20 = !(audio.play_music(sm::MusicId::Count, 0));
    CHECK(ok20, "invalid music id succeeded");
    if (!ok20) return 1;
    const bool ok21 = !(!contains_text(audio.last_error(), "invalid music id"));
    CHECK(ok21, "invalid music id did not set error");
    if (!ok21) return 1;
    const bool ok22 = !(audio.play_sfx(sm::SfxId::Count, -1));
    CHECK(ok22, "invalid sfx id succeeded");
    if (!ok22) return 1;
    const bool ok23 = !(!contains_text(audio.last_error(), "invalid sfx id"));
    CHECK(ok23, "invalid sfx id did not set error");
    if (!ok23) return 1;

    audio.set_master_volume(2.0f);
    audio.set_music_volume(-1.0f);
    audio.set_sfx_volume(0.25f);
    const bool ok24 = !(!close_enough(audio.master_volume(), 1.0f));
    CHECK(ok24, "master volume did not clamp high");
    if (!ok24) return 1;
    const bool ok25 = !(!close_enough(audio.music_volume(), 0.0f));
    CHECK(ok25, "music volume did not clamp low");
    if (!ok25) return 1;
    const bool ok26 = !(!close_enough(audio.sfx_volume(), 0.25f));
    CHECK(ok26, "sfx volume changed unexpectedly");
    if (!ok26) return 1;

    const bool ok27 = !(audio.muted());
    CHECK(ok27, "fresh audio starts muted");
    if (!ok27) return 1;
    const bool ok28 = !(!audio.toggle_muted());
    CHECK(ok28, "toggle_muted did not mute");
    if (!ok28) return 1;
    const bool ok29 = !(!audio.muted());
    CHECK(ok29, "muted flag not persisted");
    if (!ok29) return 1;
    audio.set_muted(false);
    const bool ok30 = !(audio.muted());
    CHECK(ok30, "set_muted(false) failed");
    if (!ok30) return 1;

    audio.stop_music(0);
    const bool ok31 = !(audio.music_playing());
    CHECK(ok31, "stop_music before init left music playing");
    if (!ok31) return 1;
    const bool ok32 = !(audio.current_music() != sm::MusicId::Count);
    CHECK(ok32, "stop_music before init changed state incorrectly");
    if (!ok32) return 1;

    std::printf("OK audio_contract_test metadata assets volumes mute preinit contract\n");
    return sm::test::report("audio_contract_test");
}
