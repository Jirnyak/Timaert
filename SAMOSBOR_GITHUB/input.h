bool inputbox(SDL_Renderer* renderer, TTF_Font* font,
              int x, int y, int w, int h,
              char output[64], int type = 0)
{
    SDL_Event e;
    bool done = false;
    std::string inputStr;

    SDL_StartTextInput();

    SDL_Rect boxRect = { x, y, w, h };

    // Clear output buffer
    std::memset(output, 0, 64);

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
                char c = e.text.text[0];

                if (inputStr.size() < 63)
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
            SDL_Color color = { 255, 255, 255, 255 };
            SDL_Surface* surface =
                TTF_RenderText_Solid(font, inputStr.c_str(), color);

            SDL_Texture* texture =
                SDL_CreateTextureFromSurface(renderer, surface);

            SDL_Rect textRect = { x + 5, y + 5, surface->w, surface->h };
            SDL_RenderCopy(renderer, texture, nullptr, &textRect);

            SDL_FreeSurface(surface);
            SDL_DestroyTexture(texture);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    SDL_StopTextInput();

    // Safe copy to output buffer
    const size_t len = std::min(inputStr.size(), size_t(63));
    std::copy_n(inputStr.c_str(), len, output);
    output[len] = '\0';

    return true;
}