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

struct SfxAsset {
    SfxId id;
    const char* key;
    const char* file;
};

// The melee feedback trio (owner 2026-09-06). The file column is where a real
// recording will land when one exists — TODAY none do, and the procedural
// default (synth_sfx_chunk, mixer branch below) answers for every missing
// file. The row is live either way: play_sfx has a chunk by construction.
constexpr SfxAsset kSfxAssets[kSfxCount] = {
    {SfxId::MeleeSwing,   "melee-swing",   "melee-swing.wav"},
    {SfxId::MeleeHit,     "melee-hit",     "melee-hit.wav"},
    {SfxId::MeleeBlocked, "melee-blocked", "melee-blocked.wav"},
    {SfxId::SpellCast,    "spell-cast",    "spell-cast.wav"},
    {SfxId::PlayerHurt,   "player-hurt",   "player-hurt.wav"},
    {SfxId::Death,        "death",         "death.wav"},
};
static_assert(rows_in_enum_order(kSfxAssets, &SfxAsset::id),
              "kSfxAssets row order must mirror SfxId");

const MusicAsset* find_music_asset(MusicId id) {
    const std::size_t i = std::size_t(id);
    return i < kMusicCount ? &kMusicAssets[i] : nullptr;
}

const SfxAsset* find_sfx_asset(SfxId id) {
    const std::size_t i = std::size_t(id);
    return i < kSfxCount ? &kSfxAssets[i] : nullptr;
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
const char* sfx_key(SfxId id) {
    const SfxAsset* a = find_sfx_asset(id);
    return a ? a->key : nullptr;
}
const char* sfx_file(SfxId id) {
    const SfxAsset* a = find_sfx_asset(id);
    return a ? a->file : nullptr;
}

} // namespace sm

#if defined(TIMAERT_HAS_SDL_MIXER)

#include <SDL.h>
#include <SDL_mixer.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

Mix_Chunk* load_sfx_file(const char* file, const char* assetRoot,
                         char* loadedPath, std::size_t loadedPathSize,
                         char* error, std::size_t errorSize) {
    char path[512];
    if (build_asset_root_path(path, sizeof(path), assetRoot, file)
        && file_exists(path)) {
        copy_text(loadedPath, loadedPathSize, path);
        Mix_Chunk* chunk = Mix_LoadWAV(path);
        if (!chunk) copy_text(error, errorSize, Mix_GetError());
        return chunk;
    }

    if (build_base_path(path, sizeof(path), file) && file_exists(path)) {
        copy_text(loadedPath, loadedPathSize, path);
        Mix_Chunk* chunk = Mix_LoadWAV(path);
        if (!chunk) copy_text(error, errorSize, Mix_GetError());
        return chunk;
    }

    for (const char* prefix : kSoundPrefixes) {
        if (!build_path(path, sizeof(path), prefix, file)) continue;
        if (!file_exists(path)) continue;
        copy_text(loadedPath, loadedPathSize, path);
        Mix_Chunk* chunk = Mix_LoadWAV(path);
        if (!chunk) copy_text(error, errorSize, Mix_GetError());
        return chunk;
    }

    if (build_path(path, sizeof(path), kSoundPrefixes[0], file)) {
        copy_text(loadedPath, loadedPathSize, path);
    }
    copy_text(error, errorSize, "file not found");
    return nullptr;
}

// ── Procedural SFX defaults ────────────────────────────────────────────────
// The universal-resolver law, sound edition (owner 2026-09-06: «где нет
// ассетов пускаем процедурный дефолт»): a row whose file is absent is
// synthesized right here, so play_sfx always has a chunk and a shipped .wav
// simply overrides the synth. Deterministic by construction — a fixed-seed
// xorshift per sound, no clocks — so every boot makes the identical bytes.
//
// The samples are written in the EXACT format Mix_OpenAudio opened below
// (kSampleRate, MIX_DEFAULT_FORMAT = signed 16-bit native, kOutputChannels
// interleaved): Mix_QuickLoad_RAW performs no conversion, it trusts the
// buffer to be device-formatted, and this proximity is the guarantee.

struct SynthRng {   // xorshift32; [-1,1) noise
    std::uint32_t s;
    explicit SynthRng(std::uint32_t seed) : s(seed ? seed : 1u) {}
    float noise() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return float(s >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

constexpr float kSynthPi = 3.14159265358979f;

// The arc through the air: noise through a one-pole lowpass whose cutoff
// rises and falls across the stroke — pink-ish rush, no tone of its own.
void synth_melee_swing(std::vector<float>& mono) {
    const int n = int(0.20f * kSampleRate);
    mono.resize(std::size_t(n));
    SynthRng rng(0xA5F00D1u);
    float lp = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float ph = float(i) / float(n);            // 0..1 over the stroke
        const float cutoff = 250.0f + 1900.0f * std::sin(kSynthPi * ph);
        const float a = 1.0f - std::exp(-2.0f * kSynthPi * cutoff
                                        / float(kSampleRate));
        lp += a * (rng.noise() - lp);
        const float env = std::pow(std::sin(kSynthPi * ph), 1.5f);
        mono[std::size_t(i)] = lp * env;
    }
}

// The blow biting flesh: a low body whose pitch falls fast, under a short
// noise slap — thud, not ring.
void synth_melee_hit(std::vector<float>& mono) {
    const int n = int(0.15f * kSampleRate);
    mono.resize(std::size_t(n));
    SynthRng rng(0xBEEF11u);
    float phase = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(kSampleRate);
        const float f = 60.0f + 110.0f * std::exp(-t * 28.0f);
        phase += 2.0f * kSynthPi * f / float(kSampleRate);
        const float body = std::sin(phase) * std::exp(-t * 26.0f);
        const float slap = rng.noise() * 0.8f * std::exp(-t * 600.0f);
        mono[std::size_t(i)] = body + slap;
    }
}

// The plate ringing: a handful of inharmonic metal partials, the high ones
// dying first, over a tick of noise attack — clink, unmistakably not a wound.
void synth_melee_blocked(std::vector<float>& mono) {
    const int n = int(0.28f * kSampleRate);
    mono.resize(std::size_t(n));
    SynthRng rng(0xC1A46u);
    constexpr float kFreq[5] = {1913.0f, 2547.0f, 3289.0f, 4177.0f, 5401.0f};
    constexpr float kAmp[5]  = {1.0f, 0.62f, 0.44f, 0.30f, 0.18f};
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(kSampleRate);
        float v = 0.0f;
        for (int p = 0; p < 5; ++p) {
            v += kAmp[p] * std::sin(2.0f * kSynthPi * kFreq[p] * t)
               * std::exp(-t * (14.0f + kFreq[p] * 0.004f));
        }
        v += rng.noise() * 0.5f * std::exp(-t * 900.0f);
        mono[std::size_t(i)] = v;
    }
}

// A cast leaving the hand: one bright partial sweeping upward under a
// breath of air — shimmer, not a whoosh (the whoosh is the swing's voice).
void synth_spell_cast(std::vector<float>& mono) {
    const int n = int(0.22f * kSampleRate);
    mono.resize(std::size_t(n));
    SynthRng rng(0x5CA1AB1u);
    float phase = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float ph = float(i) / float(n);
        const float t = float(i) / float(kSampleRate);
        const float f = 320.0f + 1500.0f * ph * ph;
        phase += 2.0f * kSynthPi * f / float(kSampleRate);
        const float vib = 1.0f + 0.03f * std::sin(2.0f * kSynthPi * 9.0f * t);
        const float env = std::pow(std::sin(kSynthPi * ph), 0.8f);
        const float tone = std::sin(phase * vib) * 0.8f;
        const float air = rng.noise() * 0.3f;
        mono[std::size_t(i)] = (tone + air) * env;
    }
}

// The blow that found the INHABITED body: lower and duller than the hit we
// deal out — a thump under the ribs, longer in the chest, no ring.
void synth_player_hurt(std::vector<float>& mono) {
    const int n = int(0.24f * kSampleRate);
    mono.resize(std::size_t(n));
    SynthRng rng(0xD00F00Du);
    float phase = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(kSampleRate);
        const float f = 42.0f + 70.0f * std::exp(-t * 18.0f);
        phase += 2.0f * kSynthPi * f / float(kSampleRate);
        const float body = std::sin(phase) * std::exp(-t * 12.0f);
        const float slap = rng.noise() * 0.5f * std::exp(-t * 300.0f);
        mono[std::size_t(i)] = body + slap;
    }
}

// A body going down: a tone falling through an octave into floor noise —
// collapse, distinct from the wound that caused it.
void synth_death(std::vector<float>& mono) {
    const int n = int(0.35f * kSampleRate);
    mono.resize(std::size_t(n));
    SynthRng rng(0xDEAD5EEDu);
    float phase = 0.0f;
    float lp = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = float(i) / float(kSampleRate);
        const float f = 55.0f + 170.0f * std::exp(-t * 7.0f);
        phase += 2.0f * kSynthPi * f / float(kSampleRate);
        const float body = std::sin(phase) * std::exp(-t * 7.0f);
        // the slump: low-passed noise swelling briefly as the ground takes it
        const float a = 1.0f - std::exp(-2.0f * kSynthPi * 300.0f
                                        / float(kSampleRate));
        lp += a * (rng.noise() - lp);
        const float thud = lp * 0.9f * std::exp(-t * 9.0f);
        mono[std::size_t(i)] = body * 0.8f + thud;
    }
}

Mix_Chunk* synth_sfx_chunk(SfxId id) {
    std::vector<float> mono;
    switch (id) {
        case SfxId::MeleeSwing:   synth_melee_swing(mono);   break;
        case SfxId::MeleeHit:     synth_melee_hit(mono);     break;
        case SfxId::MeleeBlocked: synth_melee_blocked(mono); break;
        case SfxId::SpellCast:    synth_spell_cast(mono);    break;
        case SfxId::PlayerHurt:   synth_player_hurt(mono);   break;
        case SfxId::Death:        synth_death(mono);         break;
        case SfxId::Count:        break;
    }
    if (mono.empty()) return nullptr;

    float peak = 0.0f;
    for (float v : mono) peak = std::max(peak, std::fabs(v));
    const float gain = peak > 0.0f ? 0.6f / peak : 0.0f;

    const std::size_t frames = mono.size();
    const std::size_t bytes =
        frames * std::size_t(kOutputChannels) * sizeof(std::int16_t);
    auto* buf = static_cast<std::int16_t*>(SDL_malloc(bytes));
    if (!buf) return nullptr;
    for (std::size_t i = 0; i < frames; ++i) {
        const float v = std::clamp(mono[i] * gain, -1.0f, 1.0f);
        const auto s = std::int16_t(v * 32767.0f);
        for (int c = 0; c < kOutputChannels; ++c) {
            buf[i * std::size_t(kOutputChannels) + std::size_t(c)] = s;
        }
    }
    Mix_Chunk* chunk =
        Mix_QuickLoad_RAW(reinterpret_cast<Uint8*>(buf), Uint32(bytes));
    if (!chunk) {
        SDL_free(buf);
        return nullptr;
    }
    chunk->allocated = 1;   // hand the SDL_malloc'd buffer to Mix_FreeChunk
    return chunk;
}

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

    for (const SfxAsset& asset : kSfxAssets) {
        path[0] = '\0';
        error[0] = '\0';
        Mix_Chunk* chunk = load_sfx_file(asset.file, assetRoot,
                                         path, sizeof(path),
                                         error, sizeof(error));
        if (chunk) {
            std::fprintf(stderr, "[audio] loaded sfx %s path=%s\n",
                         asset.key, path);
        } else {
            // THE fallback law: absence of a file is not silence — the row's
            // procedural default answers (synth_sfx_chunk above).
            chunk = synth_sfx_chunk(asset.id);
            std::fprintf(stderr, "[audio] procedural sfx %s (%s: %s)\n",
                         asset.key, asset.file, error);
        }
        sfx_[sfx_index(asset.id)] = chunk;
    }
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
