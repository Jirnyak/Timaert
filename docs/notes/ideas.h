//PSEUDOCODE IDEAS - USE C LIKE FUNCTIONS TO PROPOSE INTERESTING GAME MECHANICS (explain your idea to AI to help generate code)

// -----------------------------------------
// ARMAGEDDON SPELL
// -----------------------------------------
// radius = caster->int / 10
// convert terrain to barren
// damage all objects in radius

void armageddon_spell(obj* caster, world_t* world)
{
    int radius = caster->int_attr / 10;

    for (int dx = -radius; dx <= radius; dx++)
    for (int dy = -radius; dy <= radius; dy++)
    {
        pos p = caster->pos + (dx, dy);

        if (!world_in_bounds(p)) continue;

        world[p].type = BARREN;

        for each (obj* o in world[p].objects)
            o->hp -= 100 * caster->int_attr;
    }
}

// -----------------------------------------
// FLY SPELL
// -----------------------------------------
// pathfinder override → straight line

void fly_spell(obj* caster, pos aim)
{
    caster->movement_mode = MOVE_LINEAR;
    caster->path = line_trace(caster->pos, aim);
}

// -----------------------------------------
// MARK / RECALL SPELL
// -----------------------------------------

void mark_spell(obj* caster, world_t* world)
{
    caster->mark_pos = caster->pos;
    caster->mark_world_id = world->id;
}

void recall_spell(obj* caster, world_t* world)
{
    if (caster->mark_world_id != world->id) return;

    caster->pos = caster->mark_pos;
}

// -----------------------------------------
// RAIN MECHANICS
// -----------------------------------------
// clouds[] is sky-layer map above world[]

void update_clouds(world_t* world)
{
    for (int i = 0; i < WORLD_SIZE; i++)
    {
        // clouds grow over water
        if (world[i].type == WATER)
            clouds[i] += rand_range(0, 2);

        // drift
        int to = i + wind_dir();
        if (in_bounds(to))
        {
            clouds[to] += clouds[i] / 2;
            clouds[i] /= 2;
        }

        // rainfall
        if (clouds[i] > RAIN_THRESHOLD)
        {
            world[i].water += rand_range(1, 3);
            clouds[i]--;
        }
    }
}

// -----------------------------------------
// AMBUSH SYSTEM
// -----------------------------------------

void check_ambush(obj* player, zone_t* zone)
{
    if (player->drop != 0) return;

    int chance = zone->danger_level * 5;

    if (rand_percent() < chance)
        ambush(player, zone);
}

void ambush(obj* player, zone_t* zone)
{
    squad* enemies = tierlist_get(zone->tier);
    start_combat(player, enemies);
}

// -----------------------------------------
// TRACKS SYSTEM
// -----------------------------------------
// tracks[] stores age + owner id

void leave_tracks(obj* o)
{
    tracks[o->pos].owner = o->id;
    tracks[o->pos].age = MAX_TRACK_AGE;
}

void update_tracks()
{
    for (int i = 0; i < WORLD_SIZE; i++)
    {
        if (tracks[i].age > 0)
            tracks[i].age--;

        if (tracks[i].age == 0)
            tracks[i].owner = NONE;
    }
}

// -----------------------------------------
// FORAGER SKILL
// -----------------------------------------

void forage(obj* o, world_t* world)
{
    int roll = rand_percent();

    if (roll < 50)
        spawn_item(world, o->pos, random_food());
    else if (roll < 80)
        spawn_item(world, o->pos, random_material());
    else
        trigger_event(o->pos);
}

// -----------------------------------------
// CONTROL ANOTHER NPC / SQUAD
// -----------------------------------------

void control_obj(obj* target)
{
    player_controlled_obj = target;
    pos_cam = target->pos;
}

void release_control(obj* original_player)
{
    player_controlled_obj = original_player;
    pos_cam = original_player->pos;
}
