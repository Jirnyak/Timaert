//generate terrain map
seed = randomer(rng,10000);
generateUniversalField(field,temp,WORLD_WIDTH,
    6,      // octaves
    64,     // diffusion steps
    0.25f,   // base diffusion
    0.1f,   // base noise
    seed  // seed
);
normalize01(field, WORLD_WIDTH*WORLD_WIDTH);

//save world
save_array("field.dat", field, WORLD_WIDTH*WORLD_WIDTH);

//generate map 
for (int i = 0; i < WORLD_WIDTH*WORLD_WIDTH; i++)
{
    if (field[i] < 0.4f) 
    {
        relief[i] = WATER;
        world_map[i] = {(unsigned char)0,(unsigned char)0,(unsigned char)255};
    }
    else if (field[i] < 0.45f) 
    {
        relief[i] = SAND;
        world_map[i] = {(unsigned char)255,(unsigned char)255,(unsigned char)0};
    }
    else if (field[i] < 0.8f) 
    {
        drop = randomer(rng,1);
        if (drop == 0)
        {
            relief[i] = DIRT;
            world_map[i] = {(unsigned char)128,(unsigned char)255,(unsigned char)0};
        }
        else
        {
            relief[i] = GRASS;
            world_map[i] = {(unsigned char)0,(unsigned char)255,(unsigned char)0};
        }
    }
    else if (field[i] >= 0.8f) 
    {
        relief[i] = MOUNT;
        world_map[i] = {(unsigned char)128,(unsigned char)128,(unsigned char)128};
    }
}


world_image = img_mapo(renderer, world_image, world_map, WORLD_WIDTH);

//start game state
game_mod = GAME;