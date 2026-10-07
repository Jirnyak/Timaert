#include "macro/cell_facts.h"
#include "macro/labour.h"   // souls_flock — паства через ТЕЛО (M-90)

#include "macro/deposit_layer.h"
#include "macro/map_generator.h"
#include "macro/npc_ai.h"
#include "macro/resource_field.h"
#include "macro/spires.h"   // spire_orb — ОДИН вывод об орбе (M-233 п.8)
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
    // ВОДА — ОТ ПОРОГА (M-212). Здесь факт выводился из СОСЕДНЕГО факта того
    // же набора, и оба были производными одного порога: лишнее звено, которое
    // умеет разойтись и не умеет ничего добавить.
    f.water = td.is_water(std::uint32_t(idx));

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

    // КТО ЗДЕСЬ ЖИВЁТ — тело неподвижного сквада из КАРКАСА КЛЕТОК (ломтик
    // F: запечённая сетка мест умерла, ответ один и тот же порядок
    // приоритета несёт settlement_at); живые поля названного — паства, тир,
    // фракция, выкачанность — достаются из `GameState` СЕЙЧАС, потому что
    // плывут ежедневно.
    const MacroHandle who = (w.squads && w.store)
        ? settlement_at(*w.squads, *w.store, f.x, f.y) : MacroHandle{};
    const std::int32_t lmId = (w.store && w.store->valid(who))
        ? std::int32_t(w.store->spawnId[who.slot].index) : 0;
    if (w.gs && lmId != 0) {
        // One population, one find (ломтик F): the by-kind switch over three
        // vectors died with the vectors, and the creatures of rows died with the
        // flip — всё названное читается КОЛОНКАМИ того же слота, который
        // каркас уже вернул. Population and tier are SEPARATE fields (§42):
        // `size` used to carry the spire's tier, which was harmless only
        // while the population door was locked.
        const std::uint16_t slot = who.slot;
        const SquadType kind = SquadType(w.store->runtime[slot].squadType);
        // Орб шпиля — ОДИН вывод на весь мир (spire_orb@src/macro/spires.h,
        // M-233 п.8). Здесь он СТОЯЛ, и три соседа писали его заново; теперь
        // сборщик его ПУБЛИКУЕТ, а вывод живёт в модуле шпиля.
        const SpireOrb orb = spire_orb(*w.gs, *w.store, slot);
        // ПОЛЯ НАЗЫВАЮТСЯ, А НЕ СЧИТАЮТСЯ (DOD п.9). Здесь стояла
        // ПОЗИЦИОННАЯ выкладка, и это ровно грабля, купленная прошлой
        // сессией: новая колонка в середине садится в соседнее поле МОЛЧА,
        // а мир остаётся правдоподобным.
        f.landmark = {.type = kind,
                      .id = lmId,
                      .size = souls_flock(*w.gs, *w.store, slot),
                      .tier = orb.tier,
                      .spell = orb.spell,
                      .factionIdx =
                          int(std::int16_t(w.store->kind[slot].factionIdx)),
                      .depleted = orb.depleted};
    }
    return f;
}

} // namespace sm
