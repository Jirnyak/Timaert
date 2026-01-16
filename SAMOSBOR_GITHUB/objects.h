enum object_type
{
    CITY,
    TREE,
    BAND
};


struct entity
{
    protected:
    static int ID;

    public:
    int id;        
    int pos;       
    bool active;    
    int aim;
    unsigned char type;
    unsigned char state;

    void reset()
    {
        id = -1;
        pos = 0;
        active = false;
        aim = 0;
        type = 0;
        state = DEFAULT;
    }
};

int entity::ID = 0; 
//std::array<entity, MAX_OBJECTS*MAX_OBJECTS> objects;
entity* objects =  new entity[MAX_OBJECTS*MAX_OBJECTS];
std::vector<size_t> free_ids;

void init_pool()
{
    free_ids.clear();
    free_ids.reserve(MAX_OBJECTS*MAX_OBJECTS);
    for (size_t i = 0; i < MAX_OBJECTS*MAX_OBJECTS; ++i)
    {
        objects[i].reset();
        objects[i].id = (int)i;
        free_ids.push_back(i);
    }
}

entity* new_entity(int type, int pos)
{
    if (free_ids.empty()) return nullptr;
    size_t id = free_ids.back();
    free_ids.pop_back();
    entity* e = &objects[id];
    e->reset();
    e->id = (int)id;
    e->active = true;
    e->type = (unsigned char)type;
    e->pos = pos;
    e->state = DEFAULT;

    return e;
}

void destroy_entity(entity* e)
{
    if (!e) return;
    if (!e->active) return;

    int id = e->id;
    if (id < 0 || id >= (int)MAX_OBJECTS*MAX_OBJECTS) return;

    e->reset();       
    free_ids.push_back((size_t)id);
}
