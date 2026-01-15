if (SDL_PollEvent(&event))
{
    if (event.type == SDL_MOUSEBUTTONDOWN)
    {
        pick_x = curs_x;
        pick_y = curs_y;
        picked = !picked;  
    }
    else if (event.type == SDL_KEYDOWN)
    {
        switch(event.key.keysym.sym)
        {
            case SDLK_ESCAPE:
                quit = true;
                break;
            case SDLK_0: //SDLK_BACKQUOTE:
                fullscreen = not fullscreen;
                if (fullscreen == true)
                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                else
                    SDL_SetWindowFullscreen(window, 0);
                break;
            case SDLK_RETURN:
                game_mod = GAME;
                picked = 0;
                break;
            case SDLK_k:
                screenshot = 1;
                break;
            default:
                break;
        }
    }
}

SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
SDL_RenderClear(renderer);

ui.w = WINDOW_HEIGHT;
ui.h = WINDOW_HEIGHT;
ui.x = WINDOW_WIDTH/2-ui.w/2;
ui.y = WINDOW_HEIGHT/2-ui.h/2;

SDL_RenderCopy(renderer, world_image, nullptr, &ui);