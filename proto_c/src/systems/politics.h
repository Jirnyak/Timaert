#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <queue>
#include <vector>
#include <istream>
#include <ostream>
#include "core/types.h"
#include "core/game_context.h"
#include "core/binary_io.h"

namespace politics {

struct Faction
{
    FactionID id = FactionID::Neutral;
    char name[32]{0};
    std::int32_t population = 0;
    std::int32_t treasury = 0;
    TilePosition capital_pos = INVALID_POS;
    std::int32_t controlled_tiles = 0;  // Track territory size
    
    // RGB color for political map visualization
    std::uint8_t R = 128;
    std::uint8_t G = 128;
    std::uint8_t B = 128;
    
    // Relationships with other factions: -127 (war) to +127 (alliance)
    std::array<std::int8_t, static_cast<std::size_t>(FactionID::Count)> relationships{};
    
    void set_name(const char* n) noexcept
    {
        if (!n) return;
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    
    [[nodiscard]] bool is_hostile_to(FactionID other) const noexcept
    {
        if (other >= FactionID::Count) return false;
        return relationships[static_cast<std::size_t>(other)] < -50;
    }
    
    [[nodiscard]] bool is_allied_with(FactionID other) const noexcept
    {
        if (other >= FactionID::Count) return false;
        return relationships[static_cast<std::size_t>(other)] > 50;
    }
};

class PoliticsSystem
{
private:
    static constexpr std::size_t MAX_FACTIONS = static_cast<std::size_t>(FactionID::Count);
    std::array<Faction, MAX_FACTIONS> factions_{};
    bool initialized_ = false;
    static constexpr std::size_t NUM_PLAYABLE_FACTIONS = 8;  // 8 factions to claim territory
    
public:
    void init(rng_t& rng) noexcept
    {
        // Принудительный сброс при инициализации
        factions_.fill(Faction{});
        initialized_ = false;     
        // Initialize Neutral faction
        factions_[static_cast<std::size_t>(FactionID::Neutral)].id = FactionID::Neutral;
        factions_[static_cast<std::size_t>(FactionID::Neutral)].set_name("Neutral");
        factions_[static_cast<std::size_t>(FactionID::Neutral)].population = 0;
        factions_[static_cast<std::size_t>(FactionID::Neutral)].R = 128;
        factions_[static_cast<std::size_t>(FactionID::Neutral)].G = 128;
        factions_[static_cast<std::size_t>(FactionID::Neutral)].B = 128;
        
        // Initialize 8 playable factions with distinct colors
        static const char* faction_names[] = {
            "Crimson", "Azure", "Emerald", "Golden", 
            "Silver", "Violet", "Akai", "Sable"
        };
        
        for (std::size_t i = 0; i < NUM_PLAYABLE_FACTIONS; ++i) {
            const FactionID id = static_cast<FactionID>(i + 1);
            if (id >= FactionID::Count) break;
            
            Faction& f = factions_[i + 1];
            f.id = id;
            f.set_name(faction_names[i]);
            f.population = 1000;
            f.treasury = 5000;
            
            // Generate distinct random colors for each faction
            std::uint32_t color = random_u32_inclusive(rng, 0xFFFFFF);
            f.R = static_cast<std::uint8_t>((color >> 16) & 0xFF);
            f.G = static_cast<std::uint8_t>((color >> 8) & 0xFF);
            f.B = static_cast<std::uint8_t>(color & 0xFF);
            
            // Ensure color is bright enough to be visible
            const int brightness = (f.R + f.G + f.B) / 3;
            if (brightness < 80) {
                f.R = static_cast<std::uint8_t>(brightness + 100);
                f.G = static_cast<std::uint8_t>(brightness + 100);
                f.B = static_cast<std::uint8_t>(brightness + 100);
            }
        }
        
        // Initialize Wilderness faction
        factions_[static_cast<std::size_t>(FactionID::Wilderness)].id = FactionID::Wilderness;
        factions_[static_cast<std::size_t>(FactionID::Wilderness)].set_name("Wilderness");
        factions_[static_cast<std::size_t>(FactionID::Wilderness)].population = 0;
        factions_[static_cast<std::size_t>(FactionID::Wilderness)].R = 34;
        factions_[static_cast<std::size_t>(FactionID::Wilderness)].G = 139;
        factions_[static_cast<std::size_t>(FactionID::Wilderness)].B = 34;
        
        initialized_ = true;
    }
    
    [[nodiscard]] Faction* get_faction(FactionID id) noexcept
    {
        if (id >= FactionID::Count) return nullptr;
        return &factions_[static_cast<std::size_t>(id)];
    }
    
    [[nodiscard]] const Faction* get_faction(FactionID id) const noexcept
    {
        if (id >= FactionID::Count) return nullptr;
        return &factions_[static_cast<std::size_t>(id)];
    }
    
    void set_relationship(FactionID a, FactionID b, std::int8_t value) noexcept
    {
        if (a >= FactionID::Count || b >= FactionID::Count) return;
        if (a == b) return;  // Can't have relationship with self
        
        factions_[static_cast<std::size_t>(a)].relationships[static_cast<std::size_t>(b)] = value;
    }
    
    void adjust_relationship(FactionID a, FactionID b, std::int8_t delta) noexcept
    {
        if (a >= FactionID::Count || b >= FactionID::Count) return;
        if (a == b) return;
        
        auto& rel = factions_[static_cast<std::size_t>(a)].relationships[static_cast<std::size_t>(b)];
        std::int32_t new_val = static_cast<std::int32_t>(rel) + delta;
        if (new_val > 127) new_val = 127;
        if (new_val < -127) new_val = -127;
        rel = static_cast<std::int8_t>(new_val);
    }
    
    [[nodiscard]] bool are_hostile(FactionID a, FactionID b) const noexcept
    {
        if (a >= FactionID::Count || b >= FactionID::Count) return false;
        if (a == b) return false;
        if (a == FactionID::Neutral || b == FactionID::Neutral) return false;
        
        const auto& faction_a = factions_[static_cast<std::size_t>(a)];
        return faction_a.is_hostile_to(b);
    }
    
    // Get terrain movement cost for BFS expansion (1 = easy, 255 = very hard)
    [[nodiscard]] static std::uint8_t get_terrain_cost(TerrainType terrain) noexcept
    {
        switch (terrain) {
            case TerrainType::Water:
            case TerrainType::Nothing:
                return 255;  // Impassable
            case TerrainType::Mount:
            case TerrainType::Snow:
                return 200;  // Very slow
            case TerrainType::Jungle:
            case TerrainType::Swamp:
                return 150;  // Slow
            case TerrainType::Grass:
            case TerrainType::Dirt:
            case TerrainType::Sand:
            case TerrainType::Tundra:
                return 1;    // Easy
            default:
                return 100;
        }
    }
    
    // Fill politics map via BFS from faction capitals
    void fill_politics_map(GameContext& ctx) noexcept
    {
        // Initialize: set all non-water tiles as unclaimed, water stays at 0
        for (std::size_t i = 0; i < WORLD_SIZE; ++i) {
            const std::uint16_t x = static_cast<std::uint16_t>(i % WORLD_WIDTH);
            const std::uint16_t y = static_cast<std::uint16_t>(i / WORLD_WIDTH);
            const TilePosition pos{x, y};
            if (ctx.relief[pos] == TerrainType::Water) {
                ctx.owner[pos] = static_cast<std::uint8_t>(FactionID::Neutral);
            } else {
                ctx.owner[pos] = static_cast<std::uint8_t>(FactionID::Wilderness);
            }
        }
        
        // BFS from all capitals simultaneously
        using QueueEntry = std::pair<TilePosition, FactionID>;
        std::queue<QueueEntry> bfs_queue;
        
        // Seed capitals for each playable faction
        for (std::size_t i = 1; i <= NUM_PLAYABLE_FACTIONS; ++i) {
            const FactionID faction_id = static_cast<FactionID>(i);
            if (faction_id >= FactionID::Wilderness) break;
            
            Faction* f = get_faction(faction_id);
            if (f && f->capital_pos != INVALID_POS) {
                ctx.owner[f->capital_pos] = static_cast<std::uint8_t>(faction_id);
                bfs_queue.push({f->capital_pos, faction_id});
            }
        }
        
        // BFS expansion with terrain-based weighting
        std::vector<std::uint32_t> expansion_counters(static_cast<std::size_t>(FactionID::Count), 0);
        
        while (!bfs_queue.empty()) {
            const auto [current_pos, faction_id] = bfs_queue.front();
            bfs_queue.pop();
            
            // Expand to 4 neighbors
            for (int dir = 0; dir < 4; ++dir) {
                const TilePosition neighbor = neighbor_from_pos(current_pos, static_cast<Direction>(dir));
                const TerrainType neighbor_terrain = ctx.relief[neighbor];
                
                // Can't claim water or nothing
                if (neighbor_terrain == TerrainType::Water || neighbor_terrain == TerrainType::Nothing) {
                    continue;
                }
                
                // Skip if already owned by another faction
                const FactionID current_owner = static_cast<FactionID>(ctx.owner[neighbor]);
                if (current_owner != FactionID::Wilderness && current_owner != FactionID::Neutral) {
                    continue;
                }
                
                // Terrain-based expansion: mountains expand slower
                const std::uint8_t terrain_cost = get_terrain_cost(neighbor_terrain);
                if (terrain_cost < 255) {
                    ctx.owner[neighbor] = static_cast<std::uint8_t>(faction_id);
                    expansion_counters[static_cast<std::size_t>(faction_id)]++;
                    bfs_queue.push({neighbor, faction_id});
                }
            }
        }
        
        // Count controlled tiles for each faction
        for (std::size_t i = 0; i < WORLD_SIZE; ++i) {
            const std::uint16_t x = static_cast<std::uint16_t>(i % WORLD_WIDTH);
            const std::uint16_t y = static_cast<std::uint16_t>(i / WORLD_WIDTH);
            const TilePosition pos{x, y};
            const FactionID owner = static_cast<FactionID>(ctx.owner[pos]);
            if (owner < FactionID::Count && owner != FactionID::Neutral && owner != FactionID::Wilderness) {
                Faction* f = get_faction(owner);
                if (f) {
                    f->controlled_tiles++;
                }
            }
        }
    }
    
    void update_monthly(GameContext& /*ctx*/) noexcept
    {
        // Monthly updates: population growth, treasury income, etc.
        for (auto& faction : factions_) {
            if (faction.id == FactionID::Neutral || faction.id == FactionID::Wilderness) continue;
            if (faction.population <= 0) continue;
            
            // Population growth (0.1% per month)
            faction.population += static_cast<std::int32_t>(faction.population * 0.001);
            
            // Tax income (population * 0.1 gold per month)
            faction.treasury += faction.population / 10;
        }
    }
    void save(std::ostream& out) const
    {
        BinaryWriter writer(out);
        writer.write(initialized_);
        
        for (const auto& f : factions_) {
            writer.write(f.id);
            writer.write_bytes(f.name, sizeof(f.name));
            writer.write(f.population);
            writer.write(f.treasury);
            writer.write(f.capital_pos.x);
            writer.write(f.capital_pos.y);
            writer.write(f.controlled_tiles);
            writer.write(f.R);
            writer.write(f.G);
            writer.write(f.B);
            // Save relationships array
            writer.write_bytes(f.relationships.data(), f.relationships.size() * sizeof(std::int8_t));
        }
    }

    void load(std::istream& in)
    {
        BinaryReader reader(in);
        reader.read(initialized_);
        
        for (auto& f : factions_) {
            reader.read(f.id);
            reader.read_bytes(f.name, sizeof(f.name));
            reader.read(f.population);
            reader.read(f.treasury);
            f.capital_pos.x = reader.read<std::uint16_t>();
            f.capital_pos.y = reader.read<std::uint16_t>();
            reader.read(f.controlled_tiles);
            reader.read(f.R);
            reader.read(f.G);
            reader.read(f.B);
            // Load relationships array
            reader.read_bytes(f.relationships.data(), f.relationships.size() * sizeof(std::int8_t));
        }
    }
};

} // namespace politics
