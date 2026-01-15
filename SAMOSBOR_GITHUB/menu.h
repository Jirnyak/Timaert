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
                        break;
                    case SDLK_0: //SDLK_BACKQUOTE:
                        fullscreen = not fullscreen;
                        if (fullscreen == true)
                            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                        else
                            SDL_SetWindowFullscreen(window, 0);
                        break;
                    default:
                        break;
                }
            }
        }

    SDL_RenderCopy(renderer, background[0], NULL, &tile_background);

    // Starting vertical position for the first menu item
    box_y = WINDOW_HEIGHT / 3;

    // Loop through menu items and render each one in its own box
    for (int i = 0; i < 3; ++i) 
    {
        // Calculate box position
        ui.w = WINDOW_WIDTH / 3;
        ui.h = WINDOW_HEIGHT / 10;
        box_x = WINDOW_WIDTH / 2 - ui.w / 2;

        // Update ui.x and ui.y for the current box
        ui.x = box_x;
        ui.y = box_y;

        // Set the menu text
        text = menu_items[i];

        // Check if cursor is over the current box
        if (curs_x > ui.x && curs_x < ui.x + ui.w && curs_y > ui.y && curs_y < ui.y + ui.h) 
        {
            // Highlight box if hovered over
            SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

            // Handle selection if picked
            if (picked == 1) 
            {
                picked = 0; // Reset picked after the item is selected
                switch(i)
                {
                    case 0:
                        text = "Loading...";
                        game_mod = GEN;  // Start the game
                        break;
                    case 1:
                        game_mod = LOAD;    // testing
                        break;
                    case 2:
                        quit = true;       // Exit the game
                        break;
                }
            }
        } 
        else 
        {
            // Default color when not hovered
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        }

        // Render the box
        SDL_RenderFillRect(renderer, &ui);

        // Render the text inside the box
        render_text(renderer, font, text, ui.x + ui.w / 4, ui.y + ui.h / 4, ui.w / 2, ui.h / 2, {255, 255, 255});

        // Increment the vertical position for the next menu item
        box_y += ui.h + 20;  // Adjust the gap between boxes (20 pixels)
    }

   