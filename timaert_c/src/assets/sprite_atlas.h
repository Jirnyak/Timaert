// Sprite atlas — lazy-loaded GL textures for the public/ PNG asset set
// (cities, villages, NPCs, spires, the player). Each enum value names a
// real PNG that ships with the TS reference build at
// ../public/assets/sprites/. Loading is on-demand and cached for the
// process lifetime; the GL handles are owned globally.
//
// Usage from any GL-active thread:
//   const Sprite* s = sprite_get(SpriteId::City);
//   if (s) ImGui::GetBackgroundDrawList()->AddImage(
//             (ImTextureID)(intptr_t)s->tex, tl, br);
#pragma once
#include "gl/gl.h"
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
    GLuint tex   = 0;
    int    w     = 0;
    int    h     = 0;
    bool   tried = false; // already attempted load (avoid retry storm)
};

// Returns nullptr if the asset failed to load (file missing, decode error,
// no GL context). Otherwise the cached sprite. Safe to call every frame.
const Sprite* sprite_get(SpriteId id);

// Free all loaded textures (call once at shutdown). Optional.
void sprite_atlas_shutdown();

} // namespace sm
