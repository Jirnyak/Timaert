#include "macro/cell_facts.h"
#include "macro/labour.h"   // souls_flock — паства через ТЕЛО (M-90)

#include "macro/deposit_layer.h"
#include "macro/map_generator.h"
#include "macro/npc_ai.h"
#include "macro/place_registry.h"   // place_kind_of_feature — род едет из ФИЧИ (M-318 Б)
#include "macro/politik.h"   // faction_index_for_cell — владелец земли клетки
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
    // Live vein within the profession reach — the same ground and the same
    // radius that raise a macro gatherer (npc_ai.h kGathererReach) put his
    // trade in this cell's street crowd (spawn law, fauna.h).
    // ONE array read. This used to scan the ENTIRE vein list — 69 624
    // entries in a real world — 161 µs per cell_facts, 4.3 ms of a 6.8 ms
    // seam crossing, for one byte. The radius question is a stamped field
    // (deposit_layer.h reach, problems.md §52). Жилы единые (2026-10-22):
    // рода у маски больше нет, байт несёт один факт «жила рядом».
    if (w.deposits && w.deposits->vein_near(f.x, f.y)) {
        f.depositsNear = 1u;
    }

    if (w.gs) {
        f.seasonTempOffset = season_temp_offset(w.gs->worldTime.day());
        f.cropHarvested = resource_field_scar(*w.gs, ResourceFieldId::Wheat,
                                              std::uint32_t(idx));
        // Чья это земля — тем же выводом, которым спавн наследует почву
        // (невладеемое = вольный народ); колонка строки, не вторая дверь.
        f.cellOwner = faction_index_for_cell(w.gs->cellOwner,
                                             td.width, td.height, f.x, f.y);
    }

    // ── КЛЕТКА ОТВЕЧАЕТ О СЕБЕ ФИЧЕЙ; СКВАД — О СЕБЕ. СВЯЗИ МЕЖДУ НИМИ НЕТ ─
    //
    // Вердикт владельца 2026-10-26, дословно: «МЕСТ НЕТ! СКВАДЫ ЭТО СКВАДЫ
    // ФИЧИ АГНОСТИЧНЫЙ НЕЗАВИСИМЫЙ СКВАД МОДУЛЬ РЕШАЕТ ЧТО ТАМ ПРОИСХОДИТ С
    // НИМ И МИРОМ ФИЧИ ПРОСТО ЕСТЬ В МИРЕ».
    //
    // ЗДЕСЬ СТОЯЛ ОДИН ГЕЙТ `lmId != 0` НА ВСЁ, И ОН СПРАШИВАЛ О КЛЕТКЕ У
    // СКВАДА. M-318 часть Б увела `.type` на БАЙТ ФИЧИ — но оставила гейт на
    // сквaде, то есть фича отвечала о клетке ровно тогда, когда сверху
    // случился сквад. Цена названа замером 256 дней (сид 12345): байт
    // `FT_Ruin` держался **70 плоско все 256 дней**, `FT_Spire` — **10
    // плоско**, а сквадов оставалось **70 → 26** и **10 → 2**. То есть 52
    // входа в данж из 80 стояли в мире невидимыми: байт был на месте, а
    // спросить его было не у кого. Ровно это в `bugs.md` Л6 читалось как
    // «данж испаряется» — он не испарялся, его переставали видеть.
    //
    // ПОЭТОМУ ВОПРОСОВ ДВА, И У КАЖДОГО СВОЙ НОСИТЕЛЬ:
    //   ЧТО ЭТО ЗА КЛЕТКА — байт фичи и число под ним (`.type`, `.tier`,
    //     `.spell`, `.depleted`). Безусловно: фича просто есть в мире.
    //   КТО НА НЕЙ СТОИТ — колонки сквада (`.id`, `.size`, `.factionIdx`).
    //     Нет сквада — ординала нет (`-1`), паствы нет (0), знамени нет:
    //     это ЗНАЧЕНИЯ «никого», а не отсутствие ответа.
    const SquadType kind = place_kind_of_feature(f.feature);
    // Орб шпиля — ОДИН вывод на весь мир (spire_orb@src/macro/spires.h,
    // M-233 п.8). Здесь он СТОЯЛ, и шесть соседей писали его заново;
    // теперь сборщик его ПУБЛИКУЕТ, а вывод живёт в модуле шпиля и
    // ключуется ФИЧЕЙ клетки, не колонкой рода (ЗАКОН ПОЛЯ п.3).
    const SpireOrb orb = spire_orb(w, f.x, f.y);
    const MacroHandle who = (w.squads && w.store)
        ? settlement_at(*w.squads, *w.store, f.x, f.y) : MacroHandle{};
    // One population, one find (ломтик F): the by-kind switch over three
    // vectors died with the vectors, and the creatures of rows died with the
    // flip — всё названное читается КОЛОНКАМИ того же слота, который
    // каркас уже вернул. Population and tier are SEPARATE fields (§42):
    // `size` used to carry the spire's tier, which was harmless only
    // while the population door was locked.
    const bool manned = w.gs && w.store && w.store->valid(who)
                     && w.store->spawnId[who.slot].index != 0;
    // ПОЛЯ НАЗЫВАЮТСЯ, А НЕ СЧИТАЮТСЯ (DOD п.9). Здесь стояла
    // ПОЗИЦИОННАЯ выкладка, и это ровно грабля, купленная прошлой
    // сессией: новая колонка в середине садится в соседнее поле МОЛЧА,
    // а мир остаётся правдоподобным.
    f.place = {.type = kind,
                  .id = manned
                      ? std::int32_t(w.store->spawnId[who.slot].index) : -1,
                  // ЧИСЛО ПОЛЯ ЕДЕТ ВНИЗ БЕЗУСЛОВНО, КАК И РОД: оно факт
                  // КЛЕТКИ, а не сквада (M-320 снёс `souls_flock`, бравшую
                  // слот). Под `FT_City` модуль города прочтёт его своей
                  // популяцией, под `FT_Spire` там спелл — толкует ЧИТАЮЩИЙ
                  // МОДУЛЬ, внутри себя.
                  .size = w.gs ? worked_read(*w.gs, f.x, f.y) : 0,
                  .tier = orb.tier,
                  .spell = orb.spell,
                  .factionIdx = manned
                      ? int(std::int16_t(w.store->kind[who.slot].factionIdx))
                      : -1,
                  .depleted = orb.depleted};
    return f;
}

} // namespace sm
