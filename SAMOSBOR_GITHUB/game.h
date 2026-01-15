if (SDL_PollEvent(&event))
        {
            if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                pick_x = curs_x;
                pick_y = curs_y;
                picked = 1;  
            }
            else if (event.type == SDL_KEYDOWN)
            {
                switch(event.key.keysym.sym)
                {
                    case SDLK_ESCAPE:
                        quit = true;
                        break;
                    case SDLK_0:
                        fullscreen = not fullscreen;
                        if (fullscreen == true)
                            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                        else
                            SDL_SetWindowFullscreen(window, 0);
                        break;
                    case SDLK_SPACE:
                        paused = !paused;
                        break;
                    case SDLK_k:
                        screenshot = 1;
                        break;
                    case SDLK_c:
                        inputbox(renderer, font, WINDOW_WIDTH/2, WINDOW_HEIGHT/2, 200, 100, input, 0);
                        break;
                    case SDLK_p:
                        freecam = !freecam;
                        break;
                    case SDLK_m:
                        game_mod = MAP;
                        break;
                    case SDLK_UP:
                        pos_cam = world[pos_cam].side(0)->get_n();
                        break;
                    case SDLK_LEFT:
                        pos_cam = world[pos_cam].side(1)->get_n();
                        break;
                    case SDLK_DOWN:
                        pos_cam = world[pos_cam].side(2)->get_n();
                        break;
                    case SDLK_RIGHT:
                        pos_cam = world[pos_cam].side(3)->get_n();
                        break;
                    default:
                        break;
                }
            }
        }

//PHYSICS

        if (!paused)
        {
            hour += 1;
        }

//DRAW CAMERA VIEW
        if (freecam == 0)
        {
            //pos_cam = player->pos;
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        pos = &world[pos_cam];
        for (int i = 0; i < WINDOW_WIDTH/(2*TILE_SIZE)-1; i++)
        {
            pos = pos->side(1);
        }
        for (int i = 0; i < WINDOW_HEIGHT/(2*TILE_SIZE)-1; i++)
        {
            pos = pos->side(0);
        }       
        pos = pos->side(0);
        tile.x = 0;
        tile.y = -TILE_SIZE;
        for (int a = 0; a < WINDOW_HEIGHT/TILE_SIZE; a++)
        {
            tile.x = 0;
            tile.y += TILE_SIZE;
            pos = pos->side(2);
            pos_line = pos;  
            for (int b = 0; b < WINDOW_WIDTH/TILE_SIZE; b++)
            { 
                SDL_RenderCopy(renderer, tile_texture[relief[pos_line->get_n()]], NULL, &tile);
                tile.x += TILE_SIZE;
                pos_line = pos_line->side(3);
                //mouse move
                if (picked == 1)
                {
                    if (pick_x >= tile.x and pick_x <= (tile.x + TILE_SIZE) and pick_y >= tile.y and pick_y <= (tile.y + TILE_SIZE))
                    {
                        cam_x = pos_line->get_x();
                        cam_y = pos_line->get_y();
                        pos_cam = cam_x*WORLD_WIDTH + cam_y;
                        picked = 0;
                        pos = &world[pos_cam];
                        pos_line = pos;
                    }
                }
            }
        }

        //INTERFACE

        tile.x = 0;
        tile.y = 0;

        if (paused == 1)
        {
            text = "paused";
            render_text(renderer, font, text, tile.x,tile.y, text.size()*10, 10, {255,0,0});
        }
        else
        {
            text = "unpaused";
            render_text(renderer, font, text, tile.x,tile.y, text.size()*10, 10, {255,255,255});
        }

        tile.y += 10;

        text = "us per tick: " + to_string(SDL_GetTicks() - frame);
        render_text(renderer, font, text, tile.x,tile.y, text.size()*10, 10, {255,255,255});

        tile.y += 10;

        text = "pos: " + to_string(pos_cam);
        render_text(renderer, font, text, tile.x,tile.y, text.size()*10, 10, {255,255,255});

        tile.y += 10;

        text = "seed: " + to_string(seed);
        render_text(renderer, font, text, tile.x,tile.y, text.size()*10, 10, {255,255,255});

        tile.y += 10;

        text = "you said:";
        render_text(renderer, font, text, tile.x,tile.y, text.size()*10, 10, {255,255,255});

        tile.y += 10;

        text = input;
        render_text(renderer, font, text, tile.x, tile.y, text.size() * 10, 10, {255, 255, 255});

