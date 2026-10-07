#include "macro/spires.h"
#include "macro/labour.h"      // settle_souls — одна дверь поселения душ
#include "macro/place_birth.h" // birth_place — место родится ТЕЛОМ
#include "macro/landmark_iter.h"   // for_each_place — места по слотам
#include "macro/landmark_registry.h"
#include "macro/features.h"      // FT_Spire — смысл числу задаёт ФИЧА (п.8)
#include "macro/macro_world.h"   // конверт слоёв: закон его собственной шапки
#include "tables/faction.h"
#include "macro/map_generator.h"
#include "macro/anketa.h"
#include "macro/state.h"
#include "macro/zones.h"
#include "core/rng.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

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

// A spire may not share a cell with any named place: the baked cell→landmark
// frame (settlement_at, macro/squad_index.h) awards a shared cell to the FIRST landmark
// its builder yields (settlements → villages → spires), so a co-located spire
// would be silently shadowed by the town on top of it. This scan stays
// hand-written because it runs DURING world-gen, before the grid exists —
// the one legitimate pre-grid reader.
bool cell_occupied(const GameState& gs, const MacroStore& st, int x, int y) {
    const std::uint32_t idx = ecs::cell_index(x, y, gs.mapW);
    bool taken = false;
    for_each_place(st, [&](std::uint16_t slot) {
        if (st.cell[slot].idx == idx) taken = true;
    });
    return taken;
}

} // namespace

void generate_spires(GameState& gs, MacroStore& st, const ZoneLayer& zones,
                     const TerrainData& terrain) {
    if (gs.mapW <= 0 || gs.mapH <= 0 || !terrain.has_rgba_storage()
        || !zones.has_complete_storage()) {
        return;
    }
    const LandmarkDef& def = landmark_def(SquadType::Spire);
    // WHOSE the tower is, answered ONCE and stored on the instance (owner
    // 2026-09-21). The generator is the EDITOR: it names the banner, the
    // registry row carries no faction column, and the string never leaves
    // the generation path (ЗАКОН СЛОВАРЯ). This is also what makes the
    // garrison-vs-game line below true — the demons are the POPULATION,
    // stamped through this index, while the slope's beasts stay wildlife.
    const std::int16_t infernal = std::int16_t(faction_index("demons"));
    // Own deterministic stream, distinct from the landmark-naming salt in
    // populate_landmarks_from_politik (0xC1A05E1D).
    Rng rng(gs.worldSeed ^ 0x59B12E50u);

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
                if (cell_occupied(gs, st, x, y)) continue;
                int score = gs.mapW + gs.mapH;   // no spires yet: any site wins
                for_each_place(st, [&](std::uint16_t slot) {
                    if (SquadType(st.runtime[slot].squadType)
                            != SquadType::Spire)
                        return;
                    score = std::min(score, torus_chebyshev(
                        x, y,
                        ecs::cell_x(st.cell[slot], gs.mapW),
                        ecs::cell_y(st.cell[slot], gs.mapW),
                        gs.mapW, gs.mapH));
                });
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
        // The spell is the spire's WORKED number («у поля урожай, у шахты
        // залежи, у порта корабли — у шпиля спелл»): kSpellDefs ordinal + 1
        // under FT_Spire, 0 = drained (закон нуля-ординала).
        worked_write(gs, bestX, bestY, ord + 1);
        // Место родится ТЕЛОМ одной дверью (ломтик F); ординал эмитит она —
        // шпиль есть макро-субъект как всякий другой, и берёт его из того же
        // единого эмитента, что сквады (M-37).
        const MacroHandle h = birth_place(gs, st, SquadType::Spire,
                                          bestX, bestY, infernal);
        if (!st.valid(h)) continue;   // отказ капа уже прозвучал вслух
        // Born with its haunt (§42 Инк 5): the registry row's own born
        // columns × the spell's tier, a discrete bell around the mean. Its
        // OWN stream (world salt × spell ordinal) so the placement draws
        // above stay bit-for-bit what they were.
        //
        // ПЕРЕВОРОТ v122 (вердикт 3): души данжа — ГОЛОВАМИ, вид стака
        // ВЫВОДИТСЯ — слабейшая строка полосы толпы (шпиль → Imp); worked
        // клетки не трогается — под FT_Spire там живёт СПЕЛЛ (ломтик B).
        {
            Rng popRng(gs.worldSeed ^ 0xB0125EEDu
                       ^ (std::uint32_t(ord) * 2654435761u));
            settle_souls(gs, st, h.slot, landmark_born_population(
                int(def.bornPopBase), int(def.bornPopPerScore), tier,
                popRng));
        }
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
