#include "macro/audio.h"
#include "core/math.h"

#include "core/table_guard.h"
#include <array>

// ── THE asset tables, ONE copy, ABOVE the backend split ────────────────────
// This module used to be implemented TWICE in one file: a table under
// `#if TIMAERT_HAS_SDL_MIXER` and, under `#else`, four switch ladders spelling
// the very same keys and filenames — the same content answered by two
// vocabularies, with nothing to keep them equal. Which backend is compiled in
// says nothing about WHAT the game's music is called, so the data lives here
// and both branches read it. The stub keeps only what is genuinely different:
// no device, no playback.
namespace sm {
namespace {

constexpr std::size_t kMusicCount = static_cast<std::size_t>(MusicId::Count);
constexpr std::size_t kSfxCount = static_cast<std::size_t>(SfxId::Count);

struct MusicAsset {
    MusicId id;
    const char* key;
    const char* file;
};

// Row order mirrors the enum, and the guard proves it — the switch twin could
// not have said that about itself.
// (EmpireTheme died in the 2026-08-29 canon audit: loaded on every boot,
// played by nothing — the tests were the only reader of the row.)
constexpr MusicAsset kMusicAssets[kMusicCount] = {
    {MusicId::Explore,  "explore",  "15-dungeon-suno.mp3"},
    {MusicId::Subworld, "subworld", "subworld.mp3"},
};
static_assert(rows_in_enum_order(kMusicAssets, &MusicAsset::id),
              "kMusicAssets row order must mirror MusicId");

// The SFX table has ZERO rows while SfxId is empty (see audio.h) — and a
// zero-length C array is ill-formed, so the table itself is absent and the
// lookups answer "no row" directly. When SfxId gains its first live value,
// restore `constexpr SfxAsset kSfxAssets[kSfxCount]` with the
// rows_in_enum_order guard, exactly like the music table above.
static_assert(kSfxCount == 0,
              "SfxId grew a row: bring back the kSfxAssets table + guard");

const MusicAsset* find_music_asset(MusicId id) {
    const std::size_t i = std::size_t(id);
    return i < kMusicCount ? &kMusicAssets[i] : nullptr;
}

} // namespace

const char* music_key(MusicId id) {
    const MusicAsset* a = find_music_asset(id);
    return a ? a->key : nullptr;
}
const char* music_file(MusicId id) {
    const MusicAsset* a = find_music_asset(id);
    return a ? a->file : nullptr;
}
const char* sfx_key(SfxId) {
    return nullptr;   // no rows — see the kSfxAssets note above
}
const char* sfx_file(SfxId) {
    return nullptr;   // no rows — see the kSfxAssets note above
}

} // namespace sm

#if defined(TIMAERT_HAS_SDL_MIXER)

#include <SDL.h>
#include <SDL_mixer.h>

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sm {
namespace {

constexpr int kSampleRate = 44100;
constexpr int kOutputChannels = 2;
constexpr int kChunkSize = 1024;
constexpr int kSfxChannels = 16;
// The ordinal IS the index — both tables are enum-ordered and the guards above
// prove it, so "which slot" needs no lookup.
std::size_t music_index(MusicId id) { return static_cast<std::size_t>(id); }
std::size_t sfx_index(SfxId id) { return static_cast<std::size_t>(id); }

constexpr const char* kSoundPrefixes[] = {
    "assets/sound/",
    "../assets/sound/",
    "../public/assets/sound/",
    "../../public/assets/sound/",
    "public/assets/sound/",
};

using sm::clamp01;   // THE one curve (core/math.h), not a third copy of it.

int to_mixer_volume(float value) {
    const float clamped = clamp01(value);
    return int(clamped * float(MIX_MAX_VOLUME) + 0.5f);
}

bool file_exists(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

void copy_text(char* dst, std::size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    std::snprintf(dst, dstSize, "%s", src ? src : "");
}

bool build_path(char* dst, std::size_t dstSize,
                const char* prefix, const char* file) {
    const int n = std::snprintf(dst, dstSize, "%s%s",
                                prefix ? prefix : "", file ? file : "");
    return n > 0 && std::size_t(n) < dstSize;
}

bool build_asset_root_path(char* dst, std::size_t dstSize,
                           const char* assetRoot, const char* file) {
    if (!assetRoot || assetRoot[0] == '\0') return false;
    const int n = std::snprintf(dst, dstSize, "%s/sound/%s", assetRoot, file);
    return n > 0 && std::size_t(n) < dstSize;
}

bool build_base_path(char* dst, std::size_t dstSize, const char* file) {
    char* base = SDL_GetBasePath();
    if (!base || base[0] == '\0') {
        if (base) SDL_free(base);
        return false;
    }
    const int n = std::snprintf(dst, dstSize, "%sassets/sound/%s", base, file);
    SDL_free(base);
    return n > 0 && std::size_t(n) < dstSize;
}

Mix_Music* load_music_file(const char* file, const char* assetRoot,
                           char* loadedPath, std::size_t loadedPathSize,
                           char* error, std::size_t errorSize) {
    char path[512];
    if (build_asset_root_path(path, sizeof(path), assetRoot, file)
        && file_exists(path)) {
        copy_text(loadedPath, loadedPathSize, path);
        Mix_Music* music = Mix_LoadMUS(path);
        if (!music) copy_text(error, errorSize, Mix_GetError());
        return music;
    }

    if (build_base_path(path, sizeof(path), file) && file_exists(path)) {
        copy_text(loadedPath, loadedPathSize, path);
        Mix_Music* music = Mix_LoadMUS(path);
        if (!music) copy_text(error, errorSize, Mix_GetError());
        return music;
    }

    for (const char* prefix : kSoundPrefixes) {
        if (!build_path(path, sizeof(path), prefix, file)) continue;
        if (!file_exists(path)) continue;
        copy_text(loadedPath, loadedPathSize, path);
        Mix_Music* music = Mix_LoadMUS(path);
        if (!music) copy_text(error, errorSize, Mix_GetError());
        return music;
    }

    if (build_path(path, sizeof(path), kSoundPrefixes[0], file)) {
        copy_text(loadedPath, loadedPathSize, path);
    }
    copy_text(error, errorSize, "file not found");
    return nullptr;
}

// (load_sfx_file left with the last SFX row — restore it beside kSfxAssets
// when the table gains one; load_music_file above is the shape to copy.)

} // namespace

AudioSystem::~AudioSystem() {
    shutdown();
}

void AudioSystem::set_error(const char* prefix, const char* message) {
    if (!prefix || prefix[0] == '\0') {
        copy_text(lastError_.data(), lastError_.size(), message);
        return;
    }
    std::snprintf(lastError_.data(), lastError_.size(), "%s: %s",
                  prefix, message ? message : "");
}

void AudioSystem::clear_error() {
    lastError_[0] = '\0';
}

bool AudioSystem::init(const char* assetRoot) {
    shutdown();
    clear_error();

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        set_error("SDL_InitSubSystem(SDL_INIT_AUDIO)", SDL_GetError());
        return false;
    }
    audioSubSystem_ = true;

    mixerInitFlags_ = Mix_Init(MIX_INIT_MP3);
    if ((mixerInitFlags_ & MIX_INIT_MP3) != MIX_INIT_MP3) {
        set_error("Mix_Init(MP3)", Mix_GetError());
        shutdown();
        return false;
    }

    if (Mix_OpenAudio(kSampleRate, MIX_DEFAULT_FORMAT,
                      kOutputChannels, kChunkSize) != 0) {
        set_error("Mix_OpenAudio", Mix_GetError());
        shutdown();
        return false;
    }
    mixerOpen_ = true;
    initialized_ = true;

    Mix_AllocateChannels(kSfxChannels);

    char path[512];
    char error[256];
    for (const MusicAsset& asset : kMusicAssets) {
        path[0] = '\0';
        error[0] = '\0';
        Mix_Music* loaded = load_music_file(asset.file, assetRoot,
                                            path, sizeof(path),
                                            error, sizeof(error));
        music_[music_index(asset.id)] = loaded;
        if (loaded) {
            std::fprintf(stderr, "[audio] loaded music %s path=%s\n",
                         asset.key, path);
        } else {
            std::fprintf(stderr, "[audio] missing music %s path=%s error=%s\n",
                         asset.key, path, error);
        }
    }

    // No SFX rows to load today (see the kSfxAssets note above); the loader
    // loop returns with the table's first live row.
    std::fflush(stderr);

    apply_volumes();
    return true;
}

void AudioSystem::shutdown() {
    if (mixerOpen_) {
        Mix_HaltChannel(-1);
        Mix_HaltMusic();
    }

    for (void*& chunk : sfx_) {
        if (chunk) {
            Mix_FreeChunk(static_cast<Mix_Chunk*>(chunk));
            chunk = nullptr;
        }
    }
    for (void*& music : music_) {
        if (music) {
            Mix_FreeMusic(static_cast<Mix_Music*>(music));
            music = nullptr;
        }
    }

    if (mixerOpen_) {
        Mix_CloseAudio();
        mixerOpen_ = false;
    }
    if (mixerInitFlags_ != 0) {
        Mix_Quit();
        mixerInitFlags_ = 0;
    }
    if (audioSubSystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audioSubSystem_ = false;
    }

    initialized_ = false;
    currentMusic_ = MusicId::Count;
}

void AudioSystem::apply_volumes() {
    if (!initialized_) return;
    const float master = muted_ ? 0.0f : masterVolume_;
    Mix_VolumeMusic(to_mixer_volume(master * musicVolume_));
    const int sfxVol = to_mixer_volume(master * sfxVolume_);
    Mix_Volume(-1, sfxVol);
    for (void* chunk : sfx_) {
        if (chunk) Mix_VolumeChunk(static_cast<Mix_Chunk*>(chunk), sfxVol);
    }
}

void AudioSystem::set_master_volume(float value) {
    masterVolume_ = clamp01(value);
    apply_volumes();
}

void AudioSystem::set_music_volume(float value) {
    musicVolume_ = clamp01(value);
    apply_volumes();
}

void AudioSystem::set_sfx_volume(float value) {
    sfxVolume_ = clamp01(value);
    apply_volumes();
}

void AudioSystem::set_muted(bool muted) {
    muted_ = muted;
    apply_volumes();
}

bool AudioSystem::toggle_muted() {
    set_muted(!muted_);
    return muted_;
}

bool AudioSystem::music_loaded(MusicId id) const {
    const std::size_t idx = music_index(id);
    return idx < music_.size() && music_[idx] != nullptr;
}

bool AudioSystem::sfx_loaded(SfxId id) const {
    const std::size_t idx = sfx_index(id);
    return idx < sfx_.size() && sfx_[idx] != nullptr;
}

bool AudioSystem::music_playing() const {
    return initialized_ && Mix_PlayingMusic() != 0;
}

bool AudioSystem::play_music(MusicId id, int fadeMs) {
    const std::size_t idx = music_index(id);
    if (idx >= music_.size()) {
        set_error("play_music", "invalid music id");
        return false;
    }
    if (!initialized_) {
        set_error("play_music", "audio not initialized");
        return false;
    }
    if (!music_[idx]) {
        set_error("play_music", "music asset not loaded");
        return false;
    }
    clear_error();
    if (currentMusic_ == id && Mix_PlayingMusic() != 0) return true;

    apply_volumes();
    const int ms = std::max(0, fadeMs);
    const int rc = ms > 0
        ? Mix_FadeInMusic(static_cast<Mix_Music*>(music_[idx]), -1, ms)
        : Mix_PlayMusic(static_cast<Mix_Music*>(music_[idx]), -1);
    if (rc != 0) {
        set_error("play_music", Mix_GetError());
        return false;
    }
    currentMusic_ = id;
    return true;
}

void AudioSystem::stop_music(int fadeMs) {
    if (!initialized_) return;
    const int ms = std::max(0, fadeMs);
    if (ms > 0) {
        Mix_FadeOutMusic(ms);
    } else {
        Mix_HaltMusic();
    }
    currentMusic_ = MusicId::Count;
}

bool AudioSystem::play_sfx(SfxId id, int channel) {
    const std::size_t idx = sfx_index(id);
    if (idx >= sfx_.size()) {
        set_error("play_sfx", "invalid sfx id");
        return false;
    }
    if (!initialized_) {
        set_error("play_sfx", "audio not initialized");
        return false;
    }
    if (!sfx_[idx]) {
        set_error("play_sfx", "sfx asset not loaded");
        return false;
    }

    clear_error();
    apply_volumes();
    const int played = Mix_PlayChannel(channel,
                                       static_cast<Mix_Chunk*>(sfx_[idx]), 0);
    if (played < 0) {
        set_error("play_sfx", Mix_GetError());
        return false;
    }
    return true;
}

} // namespace sm

#else

#include <algorithm>
#include <cstdio>

namespace sm {

AudioSystem::~AudioSystem() {
    shutdown();
}

void AudioSystem::set_error(const char* prefix, const char* message) {
    if (!prefix || prefix[0] == '\0') {
        std::snprintf(lastError_.data(), lastError_.size(), "%s", message ? message : "");
        return;
    }
    std::snprintf(lastError_.data(), lastError_.size(), "%s: %s",
                  prefix, message ? message : "");
}

void AudioSystem::clear_error() {
    lastError_[0] = '\0';
}

bool AudioSystem::init(const char*) {
    shutdown();
    set_error("Audio disabled", "SDL2_mixer was not available at configure time");
    std::fprintf(stderr, "[audio] disabled: %s\n", lastError_.data());
    std::fflush(stderr);
    return false;
}

void AudioSystem::shutdown() {
    initialized_ = false;
    currentMusic_ = MusicId::Count;
    audioSubSystem_ = false;
    mixerOpen_ = false;
    mixerInitFlags_ = 0;
    for (void*& music : music_) music = nullptr;
    for (void*& chunk : sfx_) chunk = nullptr;
}

void AudioSystem::apply_volumes() {}

void AudioSystem::set_master_volume(float value) {
    masterVolume_ = std::clamp(value, 0.0f, 1.0f);
}

void AudioSystem::set_music_volume(float value) {
    musicVolume_ = std::clamp(value, 0.0f, 1.0f);
}

void AudioSystem::set_sfx_volume(float value) {
    sfxVolume_ = std::clamp(value, 0.0f, 1.0f);
}

void AudioSystem::set_muted(bool muted) {
    muted_ = muted;
}

bool AudioSystem::toggle_muted() {
    muted_ = !muted_;
    return muted_;
}

bool AudioSystem::play_music(MusicId id, int) {
    const bool valid = music_key(id) != nullptr;
    set_error("play_music", valid ? "audio not initialized"
                                  : "invalid music id");
    return false;
}

void AudioSystem::stop_music(int) {
    currentMusic_ = MusicId::Count;
}

bool AudioSystem::play_sfx(SfxId id, int) {
    const bool valid = sfx_key(id) != nullptr;
    set_error("play_sfx", valid ? "audio not initialized"
                                : "invalid sfx id");
    return false;
}

bool AudioSystem::music_loaded(MusicId) const {
    return false;
}

bool AudioSystem::sfx_loaded(SfxId) const {
    return false;
}

bool AudioSystem::music_playing() const {
    return false;
}

} // namespace sm

#endif
