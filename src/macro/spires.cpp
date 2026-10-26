#include "macro/spires.h"
#include "macro/place_registry.h"
#include "macro/features.h"      // feature_put — фича просто ЕСТЬ в мире
#include "macro/macro_world.h"   // конверт слоёв: закон его собственной шапки
#include "macro/map_generator.h"
#include "macro/anketa.h"
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

int torus_chebyshev(int ax, int ay, int bx, int by, int w, int h) {
    int dx = std::abs(ax - bx);
    dx = std::min(dx, w - dx);
    int dy = std::abs(ay - by);
    dy = std::min(dy, h - dy);
    return std::max(dx, dy);
}

// ЗАНЯТА ЛИ КЛЕТКА — ВОПРОС К ФИЧЕ, А НЕ К СКВАДУ (вердикт владельца
// 2026-10-26: «ФИЧИ ПРОСТО ЕСТЬ В МИРЕ»). Клетка разрабатывается РОВНО ОДНИМ
// способом (ЗАКОН ПОЛЯ п.3), и отвечает об этом БАЙТ — та же дверь и тот же
// носитель, которыми этот же вопрос задаёт постановка шахт (M-321). Прежде
// здесь сканировался стор сквадов, и после сноса сквадов шпиля скан отвечал
// бы «свободно» про клетку, на которой фича шпиля уже стоит.
bool cell_taken(const FeatureLayer& fl, int x, int y) {
    return place_kind_of_feature(fl.at(x, y)) != SquadType::None;
}

} // namespace

void generate_spires(GameState& gs, const ZoneLayer& zones,
                     const TerrainData& terrain,
                    FeatureLayer& features) {
    if (gs.mapW <= 0 || gs.mapH <= 0 || !terrain.has_rgba_storage()
        || !zones.has_complete_storage()) {
        return;
    }
    const PlaceDef& def = place_def(SquadType::Spire);
    // Own deterministic stream, distinct from the place-naming salt in
    // populate_places_from_politik (0xC1A05E1D).
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
        const int tierStep = (int(def.maxZone) - int(def.minZone)) / 4;
        const int relaxStep = std::max(1, 256 / kZoneCount);
        int bestX = -1, bestY = -1, bestScore = -1;
        int gate = std::min<int>(def.maxZone,
                                 int(def.minZone) + (tier - 1) * tierStep);
        for (; gate >= int(def.minZone); gate -= relaxStep) {
            // Best-candidate (Mitchell) pick: among admissible draws take the
            // one farthest from every spire already placed — even spread over
            // the wild band without a tuned separation constant.
            for (int t = 0; t < kSpireCandidateTries; ++t) {
                const int x = int(rng.next_u32() % std::uint32_t(gs.mapW));
                const int y = int(rng.next_u32() % std::uint32_t(gs.mapH));
                if (terrain.is_water(x, y)) continue;
                if (int(zones.at(x, y)) < gate) continue;
                if (cell_taken(features, x, y)) continue;
                int score = gs.mapW + gs.mapH;   // no spires yet: any site wins
                for (const std::uint32_t c : placedCells)
                    score = std::min(score, torus_chebyshev(
                        x, y, cell_x(c, gs.mapW), cell_y(c, gs.mapW),
                        gs.mapW, gs.mapH));
                if (score > bestScore) {
                    bestScore = score;
                    bestX = x;
                    bestY = y;
                }
            }
            if (bestScore >= 0) break;
        }
        if (bestScore < 0) {
            // A world can genuinely lack admissible land (all-ocean or fully
            // civilized test maps). The spell is then not offered — say so.
            std::fprintf(stderr,
                         "[worldgen] spire for spell ordinal %d: no admissible "
                         "site at any zone >= %d\n",
                         ord, int(def.minZone));
            continue;
        }
        // ── ШПИЛЬ ЕСТЬ ЧИСТАЯ ФИЧА СО СВОИМ ЧИСЛОМ (M-320, вердикт
        // владельца: «да под снос») ───────────────────────────────────────
        //
        // ЗДЕСЬ РОЖДАЛСЯ СКВАД (`birth_place`) И САДИЛСЯ ГАРНИЗОН
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
        // чужой клеткой. Прежде `worked_write` стоял ПЕРЕД постановкой.
        if (feature_put(features, terrain, bestX, bestY, FT_Spire)
                != FT_Spire)
            continue;
        // The spell is the spire's WORKED number («у поля урожай, у шахты
        // залежи, у порта корабли — у шпиля спелл»): kSpellDefs ordinal + 1
        // under FT_Spire, 0 = drained (закон нуля-ординала).
        worked_write(gs, bestX, bestY, ord + 1);
        placedCells.push_back(cell_of(bestX, bestY, gs.mapW));
    }
}

// ── ОДИН ВЫВОД О ОРБЕ (M-233 п.8; закон — в шапке spires.h) ──────────────
// Вне строки: вопрос задают панель, оверлей, сборщик фактов клетки и дневной
// тик — ни одного горячего цикла, а заголовок за это остаётся на `<cstdint>`
// и fwd-декларациях (§5 п.13: тело в заголовке платит каждый включивший).
SpireOrb spire_orb(const MacroWorld& w, int x, int y) {
    SpireOrb o{};
    // Нулевой вклад отсутствующего слоя (S6) — не ветка, а данные.
    if (!w.gs || !w.features) return o;
    const GameState& gs = *w.gs;
    const int cx = wrap_axis(x, gs.mapW);
    const int cy = wrap_axis(y, gs.mapH);
    if (w.features->at(cx, cy) != FT_Spire) return o;
    o.spell = worked_read(gs, cx, cy);
    // Выкачанный шпиль ЗАБЫЛ свой спелл, как выработанная жила — свою руду,
    // поэтому тир у него честно ноль, а не последний запомненный.
    o.tier = o.spell > 0
        ? (o.spell <= kSpellCount ? kSpellDefs[o.spell - 1].tier : 1)
        : 0;
    o.depleted = o.spell == 0;
    return o;
}

} // namespace sm
