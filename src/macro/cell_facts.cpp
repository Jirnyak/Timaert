#include "macro/cell_facts.h"

#include "macro/map_generator.h"
#include "macro/resource_field.h"
#include "macro/seasons.h"
#include "macro/spells.h"
#include "macro/state.h"
#include "macro/tree_layer.h"
#include "macro/zones.h"

namespace sm {

CellFacts cell_facts(const MacroWorld& w, int x, int y) {
    CellFacts f{};
    // No terrain, no world: open water everywhere (the fail-closed zero — a
    // missing world has no land to walk, grow or hunt).
    if (!w.terrain || !w.terrain->has_rgba_storage()) return f;
    const TerrainData& td = *w.terrain;
    f.x = wrapi(x, td.width);
    f.y = wrapi(y, td.height);
    const std::size_t idx =
        std::size_t(f.y) * std::size_t(td.width) + std::size_t(f.x);

    f.height01      = float(td.rgba[idx * 4u + 0u]) / 255.0f;
    f.fertility01   = float(td.rgba[idx * 4u + 1u]) / 255.0f;
    f.temperature01 = float(td.rgba[idx * 4u + 2u]) / 255.0f;
    f.biome = biome_at_cell(td, f.x, f.y);
    f.water = f.biome == Biome::Water;

    f.feature = w.features ? w.features->at(f.x, f.y) : FT_None;
    f.treeCount = (w.trees && w.trees->has_complete_storage())
        ? int(w.trees->at(f.x, f.y)) : -1;
    f.zone = w.zones ? w.zones->at(f.x, f.y) : std::uint8_t(0);

    if (w.gs) {
        f.seasonTempOffset = season_temp_offset(w.gs->worldTime.day());
        f.cropHarvested = resource_field_scar(*w.gs, ResourceFieldId::Wheat,
                                              std::uint32_t(idx));
        // Land owner — the politik layer's per-cell kingdom (0xff = unowned).
        const Politik& pk = w.gs->politik;
        if (pk.mapW == td.width && pk.mapH == td.height
            && pk.cellOwner.size() == std::size_t(pk.mapW) * pk.mapH) {
            const std::uint8_t owner = pk.cellOwner[idx];
            f.ownerKingdom = owner == 0xFFu ? std::int8_t(-1)
                                            : std::int8_t(owner);
        }
    }

    // WHO stands here — the baked index (one lookup, one priority order);
    // the named thing's LIVE fields — population, tier, kingdom, depleted —
    // resolved from GameState now, because they drift daily. The by-id find
    // runs only on the rare cell the grid says is owned.
    const LandmarkRef lm = w.landmarks ? w.landmarks->at(f.x, f.y)
                                       : LandmarkRef{};
    if (w.gs && lm.type != LandmarkType::None) {
        switch (lm.type) {
            case LandmarkType::City:
                for (const auto& s : w.gs->settlements) {
                    if (s.id != lm.id) continue;
                    f.landmark = {lm.type, s.id, s.population, s.kingdomIdx,
                                  false};
                    break;
                }
                break;
            case LandmarkType::Village:
                for (const auto& v : w.gs->villages) {
                    if (v.id != lm.id) continue;
                    f.landmark = {lm.type, v.id, v.population, v.kingdomIdx,
                                  false};
                    break;
                }
                break;
            case LandmarkType::Spire:
                for (const auto& sp : w.gs->spires) {
                    if (sp.id != lm.id) continue;
                    // A spire's "size" IS its spell's tier — the strength
                    // column of this landmark, asked from the spell registry
                    // by ordinal (a foreign ordinal degrades to tier 1).
                    const int tier = sp.spellId < std::uint32_t(kSpellCount)
                        ? kSpellDefs[sp.spellId].tier : 1;
                    f.landmark = {lm.type, sp.id, tier, -1, sp.depleted};
                    break;
                }
                break;
            default:
                // Ruin / Lair / Shrine / Mine / Tower have no live registers
                // yet; the grid can already name them, and when their
                // registers arrive (CANON S9) this switch gains their rows.
                break;
        }
    }
    return f;
}

} // namespace sm
