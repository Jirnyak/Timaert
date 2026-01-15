enum type
{
    NOTHING,
    SAND,
    GRASS,
    DIRT,
    MOUNT,
    WATER
};

SDL_Rect tile_background;
tile_background.w = WINDOW_WIDTH;
tile_background.h = WINDOW_HEIGHT;
tile_background.x = 0;
tile_background.y = 0;

SDL_Texture *tile_texture[100];

tile_texture[NOTHING] = IMG_LoadTexture(renderer, "sprites/void.png");
tile_texture[SAND] = IMG_LoadTexture(renderer, "sprites/sand.png");
tile_texture[GRASS] = IMG_LoadTexture(renderer, "sprites/grass.png");
tile_texture[DIRT] = IMG_LoadTexture(renderer, "sprites/dirt.png");
tile_texture[MOUNT] = IMG_LoadTexture(renderer, "sprites/mount.png");
tile_texture[WATER] = IMG_LoadTexture(renderer, "sprites/water.png");

SDL_Texture *sprite_texture[100];

sprite_texture[0] = IMG_LoadTexture(renderer, "sprites/girl1.png");

SDL_Texture *background[100];

background[0] = IMG_LoadTexture(renderer, "backgrounds/0.png");

SDL_Texture *gridTex;

SDL_Texture* heatmapTexture = SDL_CreateTexture(
    renderer,
    SDL_PIXELFORMAT_RGB24,
    SDL_TEXTUREACCESS_STREAMING,
    WORLD_WIDTH,
    WORLD_WIDTH
);


