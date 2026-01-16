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

//GENERATE OBJECTS

init_pool(); //make arhitecture

checker = 0;
while (checker < MAX_OBJECTS)
{
    int drop = randomer(rng, WORLD_WIDTH*WORLD_WIDTH-1);
    if (relief[drop] == GRASS or relief[drop] == DIRT)
    {
        entity* e = new_entity(TREE, drop);
        checker++;
    }
}

// Rebuild address_map after spawning
pos_map.clear();
for (int id = 0; id < MAX_OBJECTS*MAX_OBJECTS; ++id)
{
    const entity& e = objects[id];
    if (!e.active) continue;

    pos_map[e.pos].push_back(id);
}

//start game state
game_mod = GAME;