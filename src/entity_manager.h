#pragma once

#include <vector>
#include <fstream>
#include <memory>
#include <span>
#include "game_context.h"

enum class EntityState : std::uint8_t
{
    Default = 0
};

struct Entity
{
    std::int32_t id = -1;        
    std::int32_t pos = 0;       
    bool active = false;    
    std::int32_t aim = 0;
    std::uint8_t type = 0;
    EntityState state = EntityState::Default;

    constexpr void reset() noexcept
    {
        id = -1;
        pos = 0;
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
    
    [[nodiscard]] Entity* new_entity(int type, int pos) noexcept
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
        out.write(reinterpret_cast<const char*>(objects_.get()), 
                  static_cast<std::streamsize>(sizeof(Entity) * ENTITY_POOL_SIZE));
    }
    
    void load(const std::string& filename)
    {
        std::ifstream in(filename, std::ios::binary);
        if (!in) return;
        in.read(reinterpret_cast<char*>(objects_.get()), 
                static_cast<std::streamsize>(sizeof(Entity) * ENTITY_POOL_SIZE));
    }
    
    void rebuild_pos_map(std::unordered_map<int, std::vector<int>>& pos_map) const
    {
        pos_map.clear();
        for (std::size_t id = 0; id < ENTITY_POOL_SIZE; ++id)
        {
            const Entity& e = objects_[id];
            if (!e.active) continue;
            pos_map[e.pos].push_back(static_cast<int>(id));
        }
    }
    
    [[nodiscard]] Entity& operator[](std::size_t idx) noexcept { return objects_[idx]; }
    [[nodiscard]] const Entity& operator[](std::size_t idx) const noexcept { return objects_[idx]; }
    
    [[nodiscard]] std::span<Entity> entities() noexcept { return {objects_.get(), ENTITY_POOL_SIZE}; }
    [[nodiscard]] std::span<const Entity> entities() const noexcept { return {objects_.get(), ENTITY_POOL_SIZE}; }
};
