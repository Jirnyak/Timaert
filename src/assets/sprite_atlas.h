// Sprite atlas — lazy-loaded Vulkan textures for the public/ PNG asset set
// (cities, villages, NPCs, spires, the player). Each enum value names a
// real PNG that ships with the TS reference build at
// ../public/assets/sprites/. Loading is on-demand and cached for the
// process lifetime.
//
// Usage from any thread with a valid VulkanDevice:
//   const Sprite* s = sprite_get(SpriteId::City);
//   if (s) ImGui::GetBackgroundDrawList()->AddImage(s->tex, tl, br);
#pragma once
#include "imgui.h"
#include <cstdint>

namespace sm {

enum class SpriteId : std::uint8_t {
    City,
    Village,
    Spire,
    SpireDark,
    Player,
    Peasant,
    Caravan,
    Witch,
    Cultistka,    // sorceress
    ImpGolem,     // bandit / monster
    Coins,
    Count_,
};

struct Sprite {
    ImTextureID tex = 0;
    int    w     = 0;
    int    h     = 0;
    bool   tried = false; // already attempted load (avoid retry storm)
};

// Returns nullptr if the asset failed to load (file missing, decode error,
// no GPU device). Otherwise the cached sprite. Safe to call every frame.
const Sprite* sprite_get(SpriteId id);

// Free all loaded textures (call once at shutdown). Optional.
void sprite_atlas_shutdown();

} // namespace sm
