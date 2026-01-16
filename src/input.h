#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <span>
#include "game_context.h"

inline constexpr std::size_t INPUT_BUFFER_SIZE = 64;

[[nodiscard]] inline bool inputbox(SDL_Renderer* renderer, TTF_Font* font,
                                    int x, int y, int w, int h,
                                    std::span<char> output, int type = 0)
{
    if (!renderer || !font || output.empty()) return false;
    
    SDL_Event e;
    bool done = false;
    std::string inputStr;
    inputStr.reserve(output.size());

    SDL_StartTextInput();

    const SDL_Rect boxRect = { x, y, w, h };

    std::fill(output.begin(), output.end(), '\0');

    while (!done)
    {
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
            {
                SDL_StopTextInput();
                return false;
            }
            else if (e.type == SDL_KEYDOWN)
            {
                if (e.key.keysym.sym == SDLK_BACKSPACE && !inputStr.empty())
                {
                    inputStr.pop_back();
                }
                else if (e.key.keysym.sym == SDLK_RETURN ||
                         e.key.keysym.sym == SDLK_KP_ENTER)
                {
                    done = true;
                }
            }
            else if (e.type == SDL_TEXTINPUT)
            {
                const char c = e.text.text[0];

                if (inputStr.size() < output.size() - 1)
                {
                    if (type == 1)
                    {
                        if (std::isdigit(static_cast<unsigned char>(c)))
                            inputStr += c;
                    }
                    else
                    {
                        inputStr += c;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &boxRect);

        if (!inputStr.empty())
        {
            constexpr SDL_Color color = { 255, 255, 255, 255 };
            SDLSurfacePtr surface{TTF_RenderText_Solid(font, inputStr.c_str(), color)};
            if (surface)
            {
                SDLTexturePtr texture{SDL_CreateTextureFromSurface(renderer, surface.get())};
                if (texture)
                {
                    SDL_Rect textRect = { x + 5, y + 5, surface->w, surface->h };
                    SDL_RenderCopy(renderer, texture.get(), nullptr, &textRect);
                }
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    SDL_StopTextInput();

    const std::size_t len = std::min(inputStr.size(), output.size() - 1);
    std::copy_n(inputStr.c_str(), len, output.data());
    output[len] = '\0';

    return true;
}