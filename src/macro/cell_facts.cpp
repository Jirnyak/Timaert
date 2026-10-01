#include "macro/cell_facts.h"

#include "macro/deposit_layer.h"
#include "macro/map_generator.h"
#include "macro/npc_ai.h"
#include "macro/resource_field.h"
#include "tables/seasons.h"
#include "macro/anketa.h"
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
    f.x = wrap_axis(x, td.width);
    f.y = wrap_axis(y, td.height);
    const std::size_t idx =
        std::size_t(f.y) * std::size_t(td.width) + std::size_t(f.x);

    f.height01      = field01_of(td.rgba[idx * 4u + 0u]);
    f.fertility01   = field01_of(td.rgba[idx * 4u + 1u]);
    f.temperature01 = field01_of(td.rgba[idx * 4u + 2u]);
    f.biome = biome_at_cell(td, f.x, f.y);
    f.water = f.biome == Biome::Water;

    f.feature = w.features ? w.features->at(f.x, f.y) : FT_None;
    f.treeCount = (w.trees && w.trees->has_complete_storage())
        ? int(w.trees->at(f.x, f.y)) : -1;
    f.zone = w.zones ? w.zones->at(f.x, f.y) : std::uint8_t(0);
    // Live veins within the profession reach — the same ground and the same
    // radius that raise a macro gatherer (npc_ai.h kGathererReach) put his
    // trade in this cell's street crowd (spawn law, fauna.h).
    // One array read per kind. This used to scan the ENTIRE vein list — 69 624
    // entries in a real world, and the early break only fires for the rare
    // cell that has a vein near it, so almost every caller paid the full scan:
    // 161 µs per cell_facts, 4.3 ms of a 6.8 ms seam crossing, for one byte.
    // The radius question is a stamped field now (deposit_layer.h reach,
    // problems.md §52) and the metric is the same circle it always was.
    if (w.deposits) {
        for (std::size_t k = 0; k < std::size_t(kDepositKindCount); ++k) {
            if (w.deposits->kind_near(DepositKind(k), f.x, f.y)) {
                f.depositsNear |= std::uint8_t(1u << k);
            }
        }
    }

    if (w.gs) {
        f.seasonTempOffset = season_temp_offset(w.gs->worldTime.day());
        f.cropHarvested = resource_field_scar(*w.gs, ResourceFieldId::Wheat,
                                              std::uint32_t(idx));
    }

    // КТО ЗДЕСЬ ЖИВЁТ — ординал личности из запечённой сетки (один взгляд,
    // один порядок приоритета); живые поля названного — паства, тир, фракция,
    // выкачанность — достаются из `GameState` СЕЙЧАС, потому что плывут
    // ежедневно. Поиск по ординалу идёт только на той редкой клетке, которую
    // сетка объявила занятой.
    //
    // Гейт был `lm.type != None` по КОПИИ РОДА в строке сетки — и копия была
    // колонкой-сиротой: род, который сюда ложится, всё равно берётся из
    // колонки самой записи (`rec->type` ниже), а «есть ли тут кто-то» говорит
    // сам ординал (0 = никто, ЗАКОН НУЛЯ-ОРДИНАЛА). Копия умерла (M-90 шаг 4).
    const std::int32_t lmId = w.landmarks ? w.landmarks->at(f.x, f.y) : 0;
    if (w.gs && lmId != 0) {
        // One roster, one find (CANON S9, 2026-08-29): the by-kind switch
        // over three vectors died with the vectors. Population and tier are
        // SEPARATE fields (§42): `size` used to carry the spire's tier,
        // which was harmless only while the population door was locked.
        if (const Landmark* rec = landmark_by_id(*w.gs, lmId)) {
            const bool spire = rec->type == LandmarkType::Spire;
            // The spell is the cell's WORKED number (ordinal+1, 0 = drained;
            // закон нуля-ординала). A drained spire forgets its spell like a
            // worked-out vein, so its tier honestly reads 0.
            const int orb = spire ? worked_read(*w.gs, f.x, f.y) : 0;
            const int tier = orb > 0
                ? (orb <= kSpellCount ? kSpellDefs[orb - 1].tier : 1)
                : 0;
            f.landmark = {rec->type, rec->id, souls_flock(*w.gs, *rec), tier,
                          int(rec->factionIdx), spire && orb == 0};
        }
    }
    return f;
}

} // namespace sm
