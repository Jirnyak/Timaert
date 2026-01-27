#pragma once

#include <string>

#ifdef __EMSCRIPTEN__
// Web Audio API functions (defined in web_audio_impl.cpp)
extern "C" {
void js_audio_init();
void js_audio_load_bgm(const char* path_ptr);
void js_audio_play_bgm();
void js_audio_stop_bgm();
void js_audio_set_muted(int muted);
void js_audio_set_volume(float vol);
void js_audio_play_sfx(const char* path_ptr);
void js_audio_preload_sfx(const char* path_ptr);
void js_audio_shutdown();
}
#else
    #include "miniaudio.h"
#endif

// Lock-free audio manager that doesn't block the main thread.
// Browser: Uses Web Audio API (browser's audio thread handles everything)
// Desktop: Uses miniaudio with callback-based audio (separate audio thread)
class SoundManager {
public:
    SoundManager() {
#ifdef __EMSCRIPTEN__
        js_audio_init();
#endif
    }
    ~SoundManager() {
        shutdown();
    }

    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;
    SoundManager(SoundManager&&) = delete;
    SoundManager& operator=(SoundManager&&) = delete;

    [[nodiscard]] bool load_background_music(const std::string& path) {
#ifdef __EMSCRIPTEN__
        js_audio_load_bgm(path.c_str());
        return true;
#else
        if (!initialized_) {
            ma_engine_config config = ma_engine_config_init();
            config.channels = 2;
            config.sampleRate = 44100;

            if (ma_engine_init(&config, &engine_) != MA_SUCCESS) {
                return false;
            }
            initialized_ = true;
        }

        // Stop any existing music
        if (music_initialized_) {
            ma_sound_stop(&music_);
            ma_sound_uninit(&music_);
            music_initialized_ = false;
        }

        // Load new music
        ma_uint32 const flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
        if (ma_sound_init_from_file(&engine_, path.c_str(), flags, nullptr, nullptr, &music_)
            != MA_SUCCESS) {
            return false;
        }
        music_initialized_ = true;
        ma_sound_set_volume(&music_, volume_);
        return true;
#endif
    }

    void play_background_music(int loops = -1) {
#ifdef __EMSCRIPTEN__
        (void)loops;
        js_audio_play_bgm();
#else
        if (!music_initialized_)
            return;
        ma_sound_set_looping(&music_, loops < 0);
        ma_sound_start(&music_);
#endif
    }

    void stop_background_music() {
#ifdef __EMSCRIPTEN__
        js_audio_stop_bgm();
#else
        if (music_initialized_) {
            ma_sound_stop(&music_);
        }
#endif
    }

    void toggle_mute() {
        muted_ = !muted_;
        apply_mute();
    }

    void set_muted(bool muted) {
        muted_ = muted;
        apply_mute();
    }

    [[nodiscard]] bool is_muted() const noexcept {
        return muted_;
    }

    void set_volume(float vol) {
        if (vol < 0.0f) {
            volume_ = 0.0f;
        } else if (vol > 1.0f) {
            volume_ = 1.0f;
        } else {
            volume_ = vol;
        }
        if (!muted_)
            apply_volume();
    }

    [[nodiscard]] float get_volume() const noexcept {
        return volume_;
    }

    // Play a one-shot sound effect (fire and forget, doesn't block)
    void play_sfx(const std::string& path) {
#ifdef __EMSCRIPTEN__
        js_audio_play_sfx(path.c_str());
#else
        if (!initialized_ || muted_)
            return;
        ma_engine_play_sound(&engine_, path.c_str(), nullptr);
#endif
    }

    // Preload a sound effect for instant playback later
    static void preload_sfx(const std::string& path) {
#ifdef __EMSCRIPTEN__
        js_audio_preload_sfx(path.c_str());
#else
        (void)path;
#endif
    }

    void shutdown() {
#ifdef __EMSCRIPTEN__
        js_audio_shutdown();
#else
        if (music_initialized_) {
            ma_sound_uninit(&music_);
            music_initialized_ = false;
        }
        if (initialized_) {
            ma_engine_uninit(&engine_);
            initialized_ = false;
        }
#endif
    }

private:
    void apply_mute() {
#ifdef __EMSCRIPTEN__
        js_audio_set_muted(muted_ ? 1 : 0);
#else
        if (music_initialized_) {
            ma_sound_set_volume(&music_, muted_ ? 0.0f : volume_);
        }
#endif
    }

    void apply_volume() {
#ifdef __EMSCRIPTEN__
        js_audio_set_volume(volume_);
#else
        if (music_initialized_) {
            ma_sound_set_volume(&music_, volume_);
        }
#endif
    }

    bool muted_ = false;
    float volume_ = 0.5f;

#ifndef __EMSCRIPTEN__
    ma_engine engine_{};
    ma_sound music_{};
    bool initialized_ = false;
    bool music_initialized_ = false;
#endif
};
