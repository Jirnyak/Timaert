#pragma once

#include <vector>
#include <fstream>
#include <memory>
#include <span>
#include <istream>
#include <ostream>
#include <limits>
#include "core/game_context.h"
#include "core/binary_io.h"

enum class EntityState : std::uint8_t
{
    Default = 0
};

struct Entity
{
    std::int32_t id = -1;        
    TilePosition pos = INVALID_POS;       
    bool active = false;    
    std::int32_t aim = 0;
    std::uint8_t type = 0;
    EntityState state = EntityState::Default;

    constexpr void reset() noexcept
    {
        id = -1;
        pos = INVALID_POS;
        active = false;
        aim = 0;
        type = 0;
        state = EntityState::Default;
    }
};

class EntityManager
{
private:
    std::unique_ptr<Entity[]> objects_;
    std::vector<std::size_t> free_ids_;

    void rebuild_free_ids_() noexcept
    {
        free_ids_.clear();
        free_ids_.reserve(ENTITY_POOL_SIZE);
        for (std::size_t i = 0; i < ENTITY_POOL_SIZE; ++i)
        {
            if (!objects_[i].active)
            {
                free_ids_.push_back(i);
            }
        }
    }
    
public:
    EntityManager()
        : objects_(std::make_unique<Entity[]>(ENTITY_POOL_SIZE))
    {
        free_ids_.reserve(ENTITY_POOL_SIZE);
    }
    
    ~EntityManager() = default;
    
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;
    EntityManager(EntityManager&&) = default;
    EntityManager& operator=(EntityManager&&) = default;
    
    void init_pool() noexcept
    {
        free_ids_.clear();
        free_ids_.reserve(ENTITY_POOL_SIZE);
        for (std::size_t i = 0; i < ENTITY_POOL_SIZE; ++i)
        {
            objects_[i].reset();
            objects_[i].id = static_cast<std::int32_t>(i);
            free_ids_.push_back(i);
        }
    }
    
    [[nodiscard]] Entity* new_entity(int type, TilePosition pos) noexcept
    {
        if (free_ids_.empty()) return nullptr;
        const std::size_t id = free_ids_.back();
        free_ids_.pop_back();
        Entity& e = objects_[id];
        e.reset();
        e.id = static_cast<std::int32_t>(id);
        e.active = true;
        e.type = static_cast<std::uint8_t>(type);
        e.pos = pos;
        e.state = EntityState::Default;
        return &e;
    }
    
    void destroy_entity(Entity* e) noexcept
    {
        if (!e || !e->active) return;

        const int id = e->id;
        if (id < 0 || static_cast<std::size_t>(id) >= ENTITY_POOL_SIZE) return;

        e->reset();       
        free_ids_.push_back(static_cast<std::size_t>(id));
    }
    
    void save(const std::string& filename) const
    {
        std::ofstream out(filename, std::ios::binary);
        if (!out) return;
        save(out);
    }
    
    void load(const std::string& filename)
    {
        std::ifstream in(filename, std::ios::binary);
        if (!in) return;
        load(in);
    }

    void save(std::ostream& out) const
    {
        BinaryWriter writer(out);
        writer.write_bytes(objects_.get(), sizeof(Entity) * ENTITY_POOL_SIZE);
    }

    void load(std::istream& in)
    {
        BinaryReader reader(in);
        reader.read_bytes(objects_.get(), sizeof(Entity) * ENTITY_POOL_SIZE);
        rebuild_free_ids_();
    }
    
    void rebuild_pos_map(WorldMap<std::uint16_t>& pos_map, bool clear_first = true) const
    {
        if (clear_first)
        {
            pos_map.fill(0);
        }
        for (std::size_t id = 0; id < ENTITY_POOL_SIZE; ++id)
        {
            const Entity& e = objects_[id];
            if (!e.active) continue;
            if (!is_valid(e.pos)) continue;
            if (pos_map[e.pos] < std::numeric_limits<std::uint16_t>::max())
            {
                pos_map[e.pos] += 1;
            }
        }
    }
    
    [[nodiscard]] Entity& operator[](std::size_t idx) noexcept { return objects_[idx]; }
    [[nodiscard]] const Entity& operator[](std::size_t idx) const noexcept { return objects_[idx]; }
    
    [[nodiscard]] std::span<Entity> entities() noexcept { return {objects_.get(), ENTITY_POOL_SIZE}; }
    [[nodiscard]] std::span<const Entity> entities() const noexcept { return {objects_.get(), ENTITY_POOL_SIZE}; }
};
