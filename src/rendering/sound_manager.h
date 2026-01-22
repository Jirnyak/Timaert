#pragma once

#include <string>
#include <memory>
#include <SDL.h>
#include <SDL_mixer.h>

struct MixChunkDeleter {
    void operator()(Mix_Chunk* chunk) const {
        if (chunk) Mix_FreeChunk(chunk);
    }
};

struct MixMusicDeleter {
    void operator()(Mix_Music* music) const {
        if (music) Mix_FreeMusic(music);
    }
};

using MixChunkPtr = std::unique_ptr<Mix_Chunk, MixChunkDeleter>;
using MixMusicPtr = std::unique_ptr<Mix_Music, MixMusicDeleter>;

class SoundManager
{
private:
    MixMusicPtr background_music;
    
public:
    SoundManager() = default;
    ~SoundManager() = default;
    
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;
    SoundManager(SoundManager&&) = delete;
    SoundManager& operator=(SoundManager&&) = delete;
    
    [[nodiscard]] bool load_background_music(const std::string& path)
    {
        if (!Mix_LoadMUS(path.c_str())) {
            return false;
        }
        background_music.reset(Mix_LoadMUS(path.c_str()));
        return background_music != nullptr;
    }
    
    void play_background_music(int loops = -1)
    {
        if (background_music) {
            Mix_PlayMusic(background_music.get(), loops);
        }
    }
    
    void stop_background_music()
    {
        Mix_HaltMusic();
    }
    
    void pause_background_music()
    {
        Mix_PauseMusic();
    }
    
    void resume_background_music()
    {
        Mix_ResumeMusic();
    }
    
    [[nodiscard]] bool is_playing() const
    {
        return Mix_PlayingMusic() == 1;
    }
};
