#include "macro/ruins.h"
#include "macro/features.h"     // feature_put — фича просто ЕСТЬ в мире
#include "macro/site_pick.h"     // pick_site — одна дверь подбора клетки (M-248)
#include "macro/rooted_iter.h"   // for_each_rooted — сквады по слотам (счёт городов)
#include "macro/state.h"
#include "macro/zones.h"
#include "macro/map_generator.h"
#include "core/rng.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace sm {

namespace {

// Candidate draws per ruin — the same budget the spire pass runs
// (spires.cpp kSpireCandidateTries): the ruin band (51..229) covers most
// non-civilized land, so misses are rare and handled by simply placing
// fewer ruins, out loud.
constexpr int kRuinCandidateTries = 64;

// How many ruins a world carries: one per city plus this base handful —
// density follows civilization's own scale instead of a map-size constant
// (a bigger politik raises more cities AND leaves more dead ones behind).
constexpr int kRuinBaseCount = 4;

} // namespace

void generate_ruins(GameState& gs, MacroStore& st, const ZoneLayer& zones,
                    const TerrainData& terrain) {
    FeatureLayer& features = gs.features;   // поле фич — член состояния (M-456)
    if (gs.mapW <= 0 || gs.mapH <= 0 || !terrain.has_rgba_storage()
        || !zones.has_complete_storage()) {
        return;
    }
    int cities = 0;
    for_each_rooted(st, [&](std::uint16_t slot) {
        if (SquadType(st.runtime[slot].squadType) == SquadType::City)
            ++cities;
    });
    const int target = kRuinBaseCount + cities;
    // Own deterministic stream, distinct from the spire pass (0x59B12E50)
    // and the place-naming salt (0xC1A05E1D).
    Rng rng(gs.worldSeed ^ 0x2A15DEADu);

    // РУИНЫ ЭТОГО ПРОХОДА — БУФЕР ГЕНЕРАЦИИ, И ЭТО ЗАКОННАЯ ФОРМА (ЗАКОН
    // ГЕНЕРАЦИИ п.3 дословно: «генератору разрешены буферы и функции — это
    // не дефект… она делается один раз и после неё уже мир живёт системно»).
    // Проход однократен, в тик и в кадр не попадает, после себя не живёт —
    // полем над миром эта величина не является и в реестр полей не идёт.
    //
    // ЗАЧЕМ ОН ПОЯВИЛСЯ: разброс Mitchell мерит расстояние до УЖЕ
    // ПОСТАВЛЕННЫХ руин, и мерил он его сканом стора по сквадам рода `Ruin`.
    // Сквада у руины больше нет, значит скан вернул бы пустоту, разброс
    // выродился бы в случайную выборку, и руины сбились бы в кучи.
    std::vector<std::uint32_t> placedCells;
    placedCells.reserve(std::size_t(std::max(0, target)));

    int placed = 0;
    for (int n = 0; n < target; ++n) {
        // Клетку называет ОБЩАЯ ДВЕРЬ ПОДБОРА (`pick_site@src/macro/site_pick.h`,
        // M-248): лучшая из N по всему миру, дальше всех от уже поставленных
        // руин — ровный разброс по опасной середине мира. Здесь стоял частный
        // цикл розыгрыша и счёта, близнец цикла шпилей; поток розыгрышей при
        // переезде не сдвинут ни на бит. От руины в отборе осталось только её
        // собственное: полоса опасности.
        const std::uint32_t site = pick_site(
            rng, gs.mapW, site_area_world(gs.mapW), kRuinCandidateTries,
            placedCells, [&](std::uint32_t cell) {
                if (!site_open_for_structure(features, terrain, cell))
                    return false;
                const int z = int(zones.at(cell_x(cell, gs.mapW),
                                           cell_y(cell, gs.mapW)));
                return z >= int(kRuinMinZone) && z <= int(kRuinMaxZone);
            });
        if (site == kNoSite) {
            // A world can genuinely lack admissible land — fewer ruins then,
            // and the shortfall is said, never silent (CANON S28).
            std::fprintf(stderr,
                         "[worldgen] ruin %d/%d: no admissible site in zone "
                         "band %d..%d\n",
                         n + 1, target, int(kRuinMinZone), int(kRuinMaxZone));
            continue;
        }
        // ── РУИНА ЕСТЬ ЧИСТАЯ ФИЧА, И БОЛЬШЕ НИЧЕГО (M-320, вердикт
        // владельца 2026-10-26: «под снос как шпиль, чистая фича без
        // сквада») ────────────────────────────────────────────────────────
        //
        // ЗДЕСЬ РОЖДАЛСЯ СКВАД (`birth_squad`) И САДИЛИСЬ ДУШИ
        // (`settle_souls` по колонкам `bornPop*`), И ИМЕННО ЭТО УБИВАЛО
        // ДАНЖ. Замер 256 дней (сид 12345): байт `FT_Ruin` держался **70
        // плоско ВСЕ 256 дней**, а сквадов руины оставалось **70 → 26**, и
        // шпиля **10 → 2**. Причина названа в `bugs.md` Л6: гарнизон такого
        // сквада есть ОДНА ГОЛОВА, значит его сносит первый прохожий
        // макро-автобоем, — а вместе со сквадом из мира уходил и вход в
        // данж, потому что род клетки спрашивали У СКВАДА. Фича при этом
        // стояла нетронутой: строка руины и шпиля в `kAiModuleRows` несёт
        // `nullptr` на смерти, то есть байт не снимал никто.
        //
        // ПОЧЕМУ СНОС, А НЕ ГЕЙТ ИЛИ КЛАМП (запрет назван в самом наряде):
        // гарнизон из одной головы — не слабое число, а НОСИТЕЛЬ, которого
        // в замысле нет вовсе. Наполнение данжа — дело ЕГО СОБСТВЕННОГО
        // генератора (CANON S28, второй механизм), а не инвентаря сквада,
        // стоящего сверху.
        //
        // ВОЗВРАТ ЧИТАЕТСЯ КАК ФАКТ, А НЕ КАК НАМЕРЕНИЕ (контракт двери):
        // вода отказать не может — её отбор отверг выше, — но врать о
        // поставленном мы не вправе, и счёт ведётся по тому, что ВСТАЛО.
        if (feature_put(features, terrain, site, FT_Ruin) != FT_Ruin)
            continue;
        placedCells.push_back(site);
        ++placed;
    }
    std::printf("[worldgen] ruins: %d of %d placed (%d cities)\n",
                placed, target, cities);
}

} // namespace sm
