#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sm {

enum class MusicId : std::uint8_t {
    Explore = 0,
    Subworld,
    Count,
};

// EMPTY today (canon audit 2026-08-29): the one row this enum ever had,
// Witch, was loaded on every boot and played by nothing in the game — dead
// content whose only reader was the test guarding it. The machinery stays (an
// empty registry is a registry, and S23 will want honest world sounds); the
// first live sound is one enum value + one kSfxAssets row (audio.cpp) again.
enum class SfxId : std::uint8_t {
    Count = 0,
};

const char* music_key(MusicId id);
const char* music_file(MusicId id);
const char* sfx_key(SfxId id);
const char* sfx_file(SfxId id);

class AudioSystem final {
public:
    static constexpr int kDefaultFadeMs = 500;

    AudioSystem() = default;
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    AudioSystem(AudioSystem&&) = delete;
    AudioSystem& operator=(AudioSystem&&) = delete;

    bool init(const char* assetRoot = nullptr);
    void shutdown();

    bool is_initialized() const { return initialized_; }
    const char* last_error() const { return lastError_.data(); }

    void set_master_volume(float value);
    void set_music_volume(float value);
    void set_sfx_volume(float value);
    void set_muted(bool muted);
    bool toggle_muted();

    float master_volume() const { return masterVolume_; }
    float music_volume() const { return musicVolume_; }
    float sfx_volume() const { return sfxVolume_; }
    bool muted() const { return muted_; }

    bool play_music(MusicId id, int fadeMs = kDefaultFadeMs);
    void stop_music(int fadeMs = 0);
    bool play_sfx(SfxId id, int channel = -1);

    bool music_loaded(MusicId id) const;
    bool sfx_loaded(SfxId id) const;
    MusicId current_music() const { return currentMusic_; }
    bool music_playing() const;

private:
    static constexpr std::size_t kMusicCount =
        static_cast<std::size_t>(MusicId::Count);
    static constexpr std::size_t kSfxCount =
        static_cast<std::size_t>(SfxId::Count);
    static constexpr std::size_t kMaxErrorLen = 256;

    void set_error(const char* prefix, const char* message);
    void clear_error();
    void apply_volumes();

    std::array<void*, kMusicCount> music_{};
    std::array<void*, kSfxCount> sfx_{};
    std::array<char, kMaxErrorLen> lastError_{};
    MusicId currentMusic_ = MusicId::Count;
    float masterVolume_ = 1.0f;
    float musicVolume_ = 0.4f;
    float sfxVolume_ = 0.8f;
    int mixerInitFlags_ = 0;
    bool initialized_ = false;
    bool audioSubSystem_ = false;
    bool mixerOpen_ = false;
    bool muted_ = false;
};

} // namespace sm
