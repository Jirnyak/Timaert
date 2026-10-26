#include "macro/ruins.h"
#include "macro/features.h"     // feature_put — фича просто ЕСТЬ в мире
#include "macro/place_iter.h"   // for_each_place — сквады по слотам (счёт городов)
#include "macro/place_registry.h"
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

int torus_chebyshev_(int ax, int ay, int bx, int by, int w, int h) {
    int dx = std::abs(ax - bx);
    dx = std::min(dx, w - dx);
    int dy = std::abs(ay - by);
    dy = std::min(dy, h - dy);
    return std::max(dx, dy);
}

// ЗАНЯТА ЛИ КЛЕТКА — ВОПРОС К ФИЧЕ, А НЕ К СКВАДУ (вердикт владельца
// 2026-10-26: «МЕСТ НЕТ! СКВАДЫ ЭТО СКВАДЫ ФИЧИ АГНОСТИЧНЫЙ НЕЗАВИСИМЫЙ
// СКВАД МОДУЛЬ РЕШАЕТ ЧТО ТАМ ПРОИСХОДИТ С НИМ И МИРОМ ФИЧИ ПРОСТО ЕСТЬ В
// МИРЕ»). Клетка разрабатывается РОВНО ОДНИМ способом (ЗАКОН ПОЛЯ п.3), и
// отвечает об этом БАЙТ — та же дверь и тот же носитель, которыми этот же
// вопрос задаёт постановка шахт (`stamp_mines@src/macro/spawners.cpp`,
// M-321). Прежде здесь сканировался стор сквадов, и после сноса сквадов
// руины и шпиля скан отвечал бы «свободно» про клетку, на которой фича
// уже стоит: руина села бы поверх руины и поверх шпиля МОЛЧА.
bool cell_taken_(const FeatureLayer& fl, int x, int y) {
    return place_kind_of_feature(fl.at(x, y)) != SquadType::None;
}

} // namespace

void generate_ruins(GameState& gs, MacroStore& st, const ZoneLayer& zones,
                    const TerrainData& terrain,
                    FeatureLayer& features) {
    if (gs.mapW <= 0 || gs.mapH <= 0 || !terrain.has_rgba_storage()
        || !zones.has_complete_storage()) {
        return;
    }
    const PlaceDef& def = place_def(SquadType::Ruin);
    int cities = 0;
    for_each_place(st, [&](std::uint16_t slot) {
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
        // Best-candidate (Mitchell) pick over the row's own zone band: among
        // admissible draws take the one farthest from every ruin already
        // placed — even spread over the dangerous middle of the world.
        int bestX = -1, bestY = -1, bestScore = -1;
        for (int t = 0; t < kRuinCandidateTries; ++t) {
            const int x = int(rng.next_u32() % std::uint32_t(gs.mapW));
            const int y = int(rng.next_u32() % std::uint32_t(gs.mapH));
            if (terrain.is_water(x, y)) continue;
            const int z = int(zones.at(x, y));
            if (z < int(def.minZone) || z > int(def.maxZone)) continue;
            if (cell_taken_(features, x, y)) continue;
            int score = gs.mapW + gs.mapH;   // no ruins yet: any site wins
            for (const std::uint32_t c : placedCells)
                score = std::min(score, torus_chebyshev_(
                    x, y, cell_x(c, gs.mapW), cell_y(c, gs.mapW),
                    gs.mapW, gs.mapH));
            if (score > bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
            }
        }
        if (bestScore < 0) {
            // A world can genuinely lack admissible land — fewer ruins then,
            // and the shortfall is said, never silent (CANON S28).
            std::fprintf(stderr,
                         "[worldgen] ruin %d/%d: no admissible site in zone "
                         "band %d..%d\n",
                         n + 1, target, int(def.minZone), int(def.maxZone));
            continue;
        }
        // ── РУИНА ЕСТЬ ЧИСТАЯ ФИЧА, И БОЛЬШЕ НИЧЕГО (M-320, вердикт
        // владельца 2026-10-26: «под снос как шпиль, чистая фича без
        // сквада») ────────────────────────────────────────────────────────
        //
        // ЗДЕСЬ РОЖДАЛСЯ СКВАД (`birth_place`) И САДИЛИСЬ ДУШИ
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
        if (feature_put(features, terrain, bestX, bestY, FT_Ruin) != FT_Ruin)
            continue;
        placedCells.push_back(cell_of(bestX, bestY, gs.mapW));
        ++placed;
    }
    std::printf("[worldgen] ruins: %d of %d placed (%d cities)\n",
                placed, target, cities);
}

} // namespace sm
