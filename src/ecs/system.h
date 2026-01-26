#pragma once

#include <memory>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace ecs {

class World;


// System priority levels for execution ordering
// Lower values = earlier execution
enum class SystemPriority : int {
    Input = 0,       // 0-49: Input processing
    Movement = 50,   // 50-99: Physics/movement
    AI = 100,        // 100-149: AI decision making
    Combat = 150,    // 150-199: Combat resolution
    Cleanup = 200,   // 200-249: Entity cleanup
    Render = 250     // 250+: Rendering preparation
};

// Base class for all ECS systems
class System {
public:
    virtual ~System() = default;
    
    // Called every frame to update the system
    virtual void update(World& world, float dt) = 0;
    
    // Returns the priority for execution ordering (lower = earlier)
    [[nodiscard]] virtual int priority() const { return static_cast<int>(SystemPriority::AI); }
    
    // Returns the name of this system for debugging
    [[nodiscard]] virtual const char* name() const { return "System"; }
    
    // Whether this system is enabled
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void set_enabled(bool e) noexcept { enabled_ = e; }
    
private:
    bool enabled_ = true;
};

// Manages a collection of systems and updates them in priority order
class SystemManager {
public:
    // Add a system to the manager
    void add(std::unique_ptr<System> system) {
        systems_.push_back(std::move(system));
        needs_sort_ = true;
    }
    
    // Remove a system from the manager
    void remove(System* system) {
        systems_.erase(
            std::remove_if(systems_.begin(), systems_.end(),
                [system](const auto& s) { return s.get() == system; }),
            systems_.end()
        );
    }
    
    // Update all systems in priority order
    void update_all(World& world, float dt) {
        if (needs_sort_) {
            sort_systems();
            needs_sort_ = false;
        }
        
        for (auto& system : systems_) {
            if (system->enabled()) {
                system->update(world, dt);
            }
        }
    }
    
    // Get the number of registered systems
    [[nodiscard]] std::size_t size() const noexcept { return systems_.size(); }
    
    // Clear all systems
    void clear() { systems_.clear(); }
    
private:
    void sort_systems() {
        std::stable_sort(systems_.begin(), systems_.end(),
            [](const auto& a, const auto& b) {
                return a->priority() < b->priority();
            });
    }
    
    std::vector<std::unique_ptr<System>> systems_;
    bool needs_sort_ = false;
};

} // namespace ecs
