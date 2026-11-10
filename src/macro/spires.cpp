#include "macro/spires.h"
#include "macro/features.h"      // feature_put — фича просто ЕСТЬ в мире
#include "macro/site_pick.h"     // pick_site — одна дверь подбора клетки (M-248)
#include "macro/macro_world.h"   // конверт слоёв: закон его собственной шапки
#include "macro/map_generator.h"
#include "bus/anketa.h"
#include "macro/state.h"
#include "macro/zones.h"
#include "core/rng.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace sm {

namespace {

// Candidate draws per gate step. The gate admits the wild tail of the danger
// field (zone >= 5); the fbm base minus civ pull keeps a healthy fraction of
// land there on every non-degenerate world, so 64 draws miss an admissible
// band only when the band is genuinely rare — and a rare band is handled by
// relaxing the gate one zone, not by drawing harder.
constexpr int kSpireCandidateTries = 64;

} // namespace

void generate_spires(GameState& gs, const ZoneLayer& zones,
                     const TerrainData& terrain) {
    FeatureLayer& features = gs.features;   // поле фич — член состояния (M-456)
    if (gs.mapW <= 0 || gs.mapH <= 0 || !terrain.has_rgba_storage()
        || !zones.has_complete_storage()) {
        return;
    }
    // Own deterministic stream, distinct from the place-naming salt in
    // populate_rooted_from_politik (0xC1A05E1D).
    Rng rng(gs.worldSeed ^ 0x59B12E50u);

    // ШПИЛИ ЭТОГО ПРОХОДА — БУФЕР ГЕНЕРАЦИИ (ЗАКОН ГЕНЕРАЦИИ п.3): разброс
    // Mitchell мерит расстояние до уже поставленных шпилей, и мерил он его
    // сканом стора по сквадам рода `Spire`. Сквада у шпиля больше нет, и без
    // буфера разброс выродился бы в случайную выборку. Длина известна
    // построением — один шпиль на строку каталога спеллов.
    std::vector<std::uint32_t> placedCells;
    placedCells.reserve(std::size_t(kSpellCount));

    for (int ord = 0; ord < kSpellCount; ++ord) {
        // The tier walks the gate through the table's own band: tier 1 opens
        // at minZone ("Untamed"), tier 5 demands maxZone ("Hellgate") — the
        // spire of a doom spell stands where the world is at its worst, and
        // its garrison strength follows from the site's own spawn context,
        // not from any per-spire scaling.
        // Tier 1 opens at the row's minZone, tier 5 demands its maxZone —
        // the danger is a CONTINUUM now, so the tier walks the row's band in
        // four derived steps instead of four hand +1s.
        const int tier = std::clamp(kSpellDefs[ord].tier, 1, 5);
        const int tierStep = (int(kSpireMaxZone) - int(kSpireMinZone)) / 4;
        const int relaxStep = std::max(1, 256 / kZoneCount);
        std::uint32_t site = kNoSite;
        int gate = std::min<int>(kSpireMaxZone,
                                 int(kSpireMinZone) + (tier - 1) * tierStep);
        for (; gate >= int(kSpireMinZone); gate -= relaxStep) {
            // Клетку называет ОБЩАЯ ДВЕРЬ ПОДБОРА
            // (`pick_site@src/macro/site_pick.h`, M-248): лучшая из N по всему
            // миру, дальше всех от уже поставленных шпилей — ровный разброс по
            // дикой полосе без подобранной константы разноса. Здесь стоял
            // частный цикл, близнец цикла руин; поток розыгрышей не сдвинут ни
            // на бит. От шпиля в отборе осталось его собственное: порог
            // опасности этого шага.
            site = pick_site(
                rng, gs.mapW, site_area_world(gs.mapW), kSpireCandidateTries,
                placedCells, [&](std::uint32_t cell) {
                    if (!site_open_for_structure(features, terrain, cell))
                        return false;
                    return int(zones.at(cell_x(cell, gs.mapW),
                                        cell_y(cell, gs.mapW))) >= gate;
                });
            if (site != kNoSite) break;
        }
        if (site == kNoSite) {
            // A world can genuinely lack admissible land (all-ocean or fully
            // civilized test maps). The spell is then not offered — say so.
            std::fprintf(stderr,
                         "[worldgen] spire for spell ordinal %d: no admissible "
                         "site at any zone >= %d\n",
                         ord, int(kSpireMinZone));
            continue;
        }
        // ── ШПИЛЬ ЕСТЬ ЧИСТАЯ ФИЧА СО СВОИМ ЧИСЛОМ (M-320, вердикт
        // владельца: «да под снос») ───────────────────────────────────────
        //
        // ЗДЕСЬ РОЖДАЛСЯ СКВАД (`birth_squad`) И САДИЛСЯ ГАРНИЗОН
        // (`settle_souls` по колонкам `bornPop*`). Замер 256 дней (сид
        // 12345): байт `FT_Spire` держался **10 плоско ВСЕ 256 дней**, а
        // сквадов шпиля оставалось **10 → 2** (у руины 70 → 26) — их сносит
        // первый прохожий макро-автобоем, потому что гарнизон такого сквада
        // есть ОДНА ГОЛОВА (`bugs.md` Л6). Вместе со сквадом из мира уходил
        // вход в данж, потому что род клетки спрашивали У СКВАДА; сама фича
        // стояла нетронутой — строка шпиля в `kAiModuleRows` несёт `nullptr`
        // на смерти, байт не снимал никто. Наполнение башни — дело ЕГО
        // СОБСТВЕННОГО генератора (CANON S28), не инвентаря сквада сверху.
        //
        // ПОРЯДОК: ФИЧА, ПОТОМ ЕЁ ЧИСЛО. Число принадлежит фиче (ЗАКОН ПОЛЯ
        // п.3), поэтому спелл пишется ТОЛЬКО после того, как байт реально
        // встал: иначе отказ двери оставил бы в мире осиротевшее число под
        // чужой клеткой. Прежде `feature_number_write` стоял ПЕРЕД постановкой.
        if (feature_put(features, terrain, site, FT_Spire) != FT_Spire)
            continue;
        // The spell is the spire's FEATURE NUMBER («у поля урожай, у шахты
        // залежи, у порта корабли — у шпиля спелл»): kSpellDefs ordinal + 1
        // under FT_Spire, 0 = drained (закон нуля-ординала).
        feature_number_write(gs, cell_x(site, gs.mapW), cell_y(site, gs.mapW),
                             ord + 1);
        placedCells.push_back(site);
    }
}

// ── ОДИН ВЫВОД О ОРБЕ (M-233 п.8; закон — в шапке spires.h) ──────────────
// Вне строки: вопрос задают панель, оверлей, сборщик фактов клетки и дневной
// тик — ни одного горячего цикла, а заголовок за это остаётся на `<cstdint>`
// и fwd-декларациях (§5 п.13: тело в заголовке платит каждый включивший).
SpireOrb spire_orb(const MacroWorld& w, int x, int y) {
    SpireOrb o{};
    // Нулевой вклад отсутствующего слоя (S6) — не ветка, а данные.
    if (!w.gs) return o;
    const GameState& gs = *w.gs;
    const int cx = wrap_axis(x, gs.mapW);
    const int cy = wrap_axis(y, gs.mapH);
    if (w.gs->features.at(cx, cy) != FT_Spire) return o;
    o.spell = feature_number_read(gs, cx, cy);
    // Выкачанный шпиль ЗАБЫЛ свой спелл, как выработанная жила — свою руду,
    // поэтому тир у него честно ноль, а не последний запомненный.
    o.tier = o.spell > 0
        ? (o.spell <= kSpellCount ? kSpellDefs[o.spell - 1].tier : 1)
        : 0;
    o.depleted = o.spell == 0;
    return o;
}

} // namespace sm
