#pragma once

#include <entt/entt.hpp>

namespace ecs {

// Safe wrapper for entity references that tracks validity and version

class EntityRef {
public:
    EntityRef() = default;

    EntityRef(entt::registry& reg, entt::entity e)
        : registry_(&reg), entity_(e), version_(static_cast<std::uint32_t>(entt::to_version(e))) {}

    // Check if this reference is still valid
    // Returns false if:
    // - No registry was assigned
    // - Entity was destroyed
    // - Entity was recycled (version mismatch)
    [[nodiscard]] bool valid() const noexcept {
        if (!registry_)
            return false;
        if (!registry_->valid(entity_))
            return false;
        // Check if entity was recycled by comparing versions
        return static_cast<std::uint32_t>(entt::to_version(entity_)) == version_;
    }

    // Explicit bool conversion for if-statements
    explicit operator bool() const noexcept {
        return valid();
    }

    // Get the raw entity (use with caution - check valid() first!)
    [[nodiscard]] entt::entity get() const noexcept {
        return entity_;
    }

    // Get the registry (may be null!)
    [[nodiscard]] entt::registry* registry() const noexcept {
        return registry_;
    }

    // Safe component access - returns nullptr if invalid or component doesn't exist
    template <typename T>
    [[nodiscard]] T* try_get() {
        if (!valid())
            return nullptr;
        return registry_->try_get<T>(entity_);
    }

    template <typename T>
    [[nodiscard]] const T* try_get() const {
        if (!valid())
            return nullptr;
        return registry_->try_get<T>(entity_);
    }

    // Check if entity has a component (returns false if invalid)
    template <typename... Components>
    [[nodiscard]] bool has() const {
        if (!valid())
            return false;
        return registry_->all_of<Components...>(entity_);
    }

    // Reset the reference
    void reset() noexcept {
        registry_ = nullptr;
        entity_ = entt::null;
        version_ = 0;
    }

    // Assign a new entity
    void assign(entt::registry& reg, entt::entity e) {
        registry_ = &reg;
        entity_ = e;
        version_ = static_cast<std::uint32_t>(entt::to_version(e));
    }

    // Compare with null
    [[nodiscard]] bool is_null() const noexcept {
        return entity_ == entt::null;
    }

    // Comparison operators
    [[nodiscard]] bool operator==(const EntityRef& other) const noexcept {
        return registry_ == other.registry_ && entity_ == other.entity_;
    }

    [[nodiscard]] bool operator!=(const EntityRef& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] bool operator==(entt::entity other) const noexcept {
        return entity_ == other;
    }

    [[nodiscard]] bool operator!=(entt::entity other) const noexcept {
        return entity_ != other;
    }

private:
    entt::registry* registry_ = nullptr;
    entt::entity entity_ = entt::null;
    std::uint32_t version_ = 0;  // Entity version for recycling detection
};

// Helper to check if an entity was recycled since we stored it

[[nodiscard]] inline bool
entity_was_recycled(entt::registry& registry, entt::entity entity, std::uint32_t stored_version) {
    if (!registry.valid(entity))
        return true;
    return entt::to_version(entity) != stored_version;
}

}  // namespace ecs
