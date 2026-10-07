// Spire placement (macro/spires.cpp). Pinned promises:
//   · one spire per kSpellDefs row (the generator asks the spell registry
//     itself — Rule 13), id drawn from the ONE landmark issuer (v54); the
//     spell is the spire cell's WORKED number (v120: ordinal + 1, 0 =
//     drained), born charged;
//   · a spire stands on land, inside the landmark table's own zone band
//     (landmark_def(Spire).minZone), and the top-tier spell's spire stands at
//     the band's CAP — the doom spell lives where the world is at its worst;
//   · no spire shares a cell with any named place (a co-located spire would
//     be shadowed by resolve_context's scan order), and the pick spreads:
//     best-candidate sampling never stacks two spires side by side;
//   · placement is a fact of the world seed (the spell rows are compile-time
//     constants): same seed reproduces the sites, another seed moves them;
//   · a world with no admissible ground places NOTHING (negative control for
//     the zone gate and the occupancy veto both).
#include "check.h"
#include "macro/labour.h"   // settle_souls / souls_flock — двери душ

#include "macro/landmark_registry.h"
#include "macro/map_generator.h"
#include "macro/landmark_iter.h"  // for_each_place — перепись мест
#include "macro/place_birth.h"  // birth_place — место родится ТЕЛОМ
#include "macro/ruins.h"
#include "macro/anketa.h"
#include "macro/spires.h"
#include "macro/state.h"
#include "macro/store.h"
#include "macro/zones.h"
#include "tables/faction.h"    // faction_index / faction_or_freefolk (M-39)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>   // стена «второго вывода о орбе нет» читает дерево
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <cstdlib>

#ifndef TIMAERT_SOURCE_DIR
#error "TIMAERT_SOURCE_DIR must name the repo root - see CMakeLists.txt"
#endif

namespace {

using namespace sm;

// ОДИН store НА ВЕСЬ СВИДЕТЕЛЬ. Место есть неподвижный сквад, и ломтиком F
// оно ЕСТЬ слот этого store целиком. Миры свидетеля делят один блок, потому
// что профиль памяти store от населения не зависит (ЗАКОН СТАБИЛЬНОСТИ) и
// store на каждый мир был бы гигабайтами за ничто; разделяет их `store_reset`
// перед каждым генезисом — мир начинается ПУСТЫМ, а перепись снимается СРАЗУ
// после своего прогона, пока его тела ещё стоят.
MacroStore& places() {
    static std::unique_ptr<MacroStore> st = make_macro_store();
    return *st;
}

// Перепись мест мира — КОЛОНКИ ТЕЛ (ломтик F: строки места больше нет). Род,
// адрес, ординал, паства и фракция суть колонки одного слота, поэтому один
// проход отвечает на все пять вопросов. Порядок обхода слотов на свежем store
// И ЕСТЬ порядок рождения (свободный список отдаёт слоты по возрастанию), так
// что «позже рождён — позже в переписи» остаётся утверждением, а не удачей.
struct PlaceRow {
    SquadType type = SquadType::None;
    int x = 0, y = 0;
    int id = 0;
    int flock = 0;
    std::int16_t faction = -1;
};

std::vector<PlaceRow> census(const GameState& gs, const MacroStore& st) {
    std::vector<PlaceRow> out;
    for_each_place(st, [&](std::uint16_t slot) {
        out.push_back(PlaceRow{
            SquadType(st.runtime[slot].squadType),
            ecs::cell_x(st.cell[slot], gs.mapW),
            ecs::cell_y(st.cell[slot], gs.mapW),
            int(st.spawnId[slot].index),
            souls_flock(gs, st, slot),
            std::int16_t(st.kind[slot].factionIdx)});
    });
    return out;
}

std::vector<PlaceRow> spires_in(const std::vector<PlaceRow>& all) {
    std::vector<PlaceRow> out;
    for (const PlaceRow& r : all)
        if (r.type == SquadType::Spire) out.push_back(r);
    return out;
}

constexpr int kW = 64, kH = 64;
// Heights: land level 0.502, sea floor 0; the gate between them IS the default
// map's sea plane — имя, а не литерал 102 (прежний байт того же уровня).
// Карта хранит СЛОВО (`kFieldWordMax`), поэтому и плоскость едет словом.
constexpr float kLand01 = 128.0f / 255.0f;
constexpr std::uint16_t kSeaWord = sm::field_word_of(sm::kDefaultSeaLevel);

// Land everywhere except a water strip at x < 8 (so "on land" is a real
// constraint, not a tautology of the fixture).
TerrainData banded_terrain() {
    TerrainData t;
    t.width = kW;
    t.height = kH;
    t.rgba.assign(std::size_t(kW * kH) * 4u, sm::field_word_of(kLand01));
    t.seaLevel16 = kSeaWord;   // плоскость моря — колонка карты (M-109)
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::size_t i = (std::size_t(y) * kW + std::size_t(x)) * 4u;
            t.rgba[i + 0] = 0;   // below sea
            t.rgba[i + 3] = 0;   // water mask
        }
    }
    return t;
}

// Danger rises west to east across the CONTINUUM: danger(x) = x * 256 / kW,
// so every byte band exists and the wild half (>= 128, the Spire row's
// minZone) is the eastern half of the map.
ZoneLayer banded_zones() {
    ZoneLayer z;
    z.width = kW;
    z.height = kH;
    z.data.assign(std::size_t(kW * kH), 0);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            z.data[std::size_t(y) * kW + std::size_t(x)] =
                std::uint8_t(std::min(255, x * 256 / (kW - 1)));
    return z;
}

GameState world(std::uint32_t seed) {
    GameState gs;
    gs.worldSeed = seed;
    gs.mapW = kW;
    gs.mapH = kH;
    return gs;
}

// The registry ordinal of the highest-tier spell — derived from the SAME
// table the generator reads, never restated.
int top_tier_ordinal() {
    int best = 0;
    for (int i = 1; i < kSpellCount; ++i)
        if (kSpellDefs[i].tier > kSpellDefs[best].tier) best = i;
    return best;
}

int torus_cheb(int ax, int ay, int bx, int by) {
    int dx = std::abs(ax - bx);
    dx = std::min(dx, kW - dx);
    int dy = std::abs(ay - by);
    dy = std::min(dy, kH - dy);
    return std::max(dx, dy);
}

void test_one_spire_per_spell_in_the_band() {
    GameState gs = world(12345u);
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    store_reset(places());
    generate_spires(gs, places(), zones, terrain);

    const LandmarkDef& def = landmark_def(SquadType::Spire);
    const std::vector<PlaceRow> all = census(gs, places());
    const std::vector<PlaceRow> spires = spires_in(all);
    CHECK_OR_RETURN(spires.size() == std::size_t(kSpellCount),
                    "every learnable spell got its spire");
    // Ids come from the ONE landmark issuer (v54): unique across every kind
    // of place, monotonic in creation order — never the list index.
    std::set<int> seenIds;
    for (const PlaceRow& r : all)
        if (r.type != SquadType::Spire) seenIds.insert(r.id);
    for (std::size_t i = 0; i < spires.size(); ++i) {
        const PlaceRow& sp = spires[i];
        CHECK(sp.id > 0 && seenIds.insert(sp.id).second,
              "a spire's id is unique across all landmarks (one issuer)");
        if (i > 0) {
            CHECK(sp.id > spires[i - 1].id,
                  "the issuer is monotonic: later spire, later ordinal");
        }
        // v120: the spell is the cell's WORKED number (ordinal + 1; 0 =
        // drained — закон нуля-ординала), never a Landmark column.
        CHECK(worked_read(gs, sp.x, sp.y) == int(i) + 1,
              "the spire's cell works its spell: registry ordinal + 1");
        CHECK(!terrain.is_water(sp.x, sp.y), "a spire stands on land");
        CHECK(int(zones.at(sp.x, sp.y)) >= int(def.minZone),
              "a spire stands inside the landmark table's zone band");
    }
    // The top-tier spell demands the band's cap (the table's maxZone), and
    // this world has free zone-9 ground, so no relaxation may kick in.
    const PlaceRow& doom = spires[std::size_t(top_tier_ordinal())];
    CHECK(int(zones.at(doom.x, doom.y)) == int(def.maxZone),
          "the top-tier spire stands at the band's cap");
    // Best-candidate spread: with the whole wild half free, spires never end
    // up stacked or adjacent (the veto alone only forbids the same cell).
    int minPair = kW + kH;
    for (std::size_t a = 0; a < spires.size(); ++a)
        for (std::size_t b = a + 1; b < spires.size(); ++b)
            minPair = std::min(minPair,
                               torus_cheb(spires[a].x, spires[a].y,
                                          spires[b].x, spires[b].y));
    CHECK(minPair >= 2, "spires spread - no two side by side");
}

void test_placement_is_a_fact_of_the_seed() {
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    GameState a = world(12345u), b = world(12345u), c = world(777u);
    // Перепись снимается СРАЗУ: следующий генезис начинается с пустого мира,
    // и чужих тел в нём стоять не должно.
    store_reset(places());
    generate_spires(a, places(), zones, terrain);
    const std::vector<PlaceRow> sa = spires_in(census(a, places()));
    store_reset(places());
    generate_spires(b, places(), zones, terrain);
    const std::vector<PlaceRow> sb = spires_in(census(b, places()));
    store_reset(places());
    generate_spires(c, places(), zones, terrain);
    const std::vector<PlaceRow> sc = spires_in(census(c, places()));

    CHECK_OR_RETURN(sa.size() == sb.size()
                        && sa.size() == std::size_t(kSpellCount),
                    "both same-seed runs placed the full registry");
    bool identical = true;
    for (std::size_t i = 0; i < sa.size(); ++i)
        identical = identical && sa[i].x == sb[i].x
                              && sa[i].y == sb[i].y;
    CHECK(identical, "same seed reproduces the same sites");

    bool moved = sc.size() != sa.size();
    for (std::size_t i = 0; !moved && i < sa.size(); ++i)
        moved = sa[i].x != sc[i].x
             || sa[i].y != sc[i].y;
    CHECK(moved, "another seed is another world - some spire moved");
}

void test_no_admissible_ground_places_nothing() {
    const TerrainData terrain = banded_terrain();
    // Negative control 1: a fully tame world (all zones 0) offers no site.
    {
        GameState gs = world(12345u);
        ZoneLayer tame;
        tame.width = kW;
        tame.height = kH;
        tame.data.assign(std::size_t(kW * kH), 0);
        store_reset(places());
        generate_spires(gs, places(), tame, terrain);
        CHECK(spires_in(census(gs, places())).empty(),
              "no wild land = no spires");
    }
    // Negative control 2: an all-ocean world offers no site either.
    {
        GameState gs = world(12345u);
        TerrainData ocean;
        ocean.width = kW;
        ocean.height = kH;
        ocean.rgba.assign(std::size_t(kW * kH) * 4u, 0);
        store_reset(places());
        generate_spires(gs, places(), banded_zones(), ocean);
        CHECK(spires_in(census(gs, places())).empty(),
              "no land = no spires");
    }
}

void test_named_places_veto_their_cells() {
    // The only wild ground is a 16×16 block (zone 9 at x,y in [32,48)); the
    // rest of the world is tame. With the block free every registry spell's
    // spire lands inside it; with a village on EVERY block cell the spires
    // have nowhere to go — the occupancy veto is real. (A block, not a single
    // cell: placement is candidate sampling, and one cell in 4096 is a needle
    // the sampler is not promised to find.)
    const TerrainData terrain = banded_terrain();
    ZoneLayer pin;
    pin.width = kW;
    pin.height = kH;
    pin.data.assign(std::size_t(kW * kH), 0);
    for (int y = 32; y < 48; ++y)
        for (int x = 32; x < 48; ++x)
            pin.data[std::size_t(y) * kW + std::size_t(x)] = 255;

    {
        GameState gs = world(12345u);
        store_reset(places());
        generate_spires(gs, places(), pin, terrain);
        const std::vector<PlaceRow> spires = spires_in(census(gs, places()));
        CHECK_OR_RETURN(spires.size() == std::size_t(kSpellCount),
                        "the wild block hosts every spire");
        int inside = 0;
        for (const PlaceRow& sp : spires)
            if (sp.x >= 32 && sp.x < 48 && sp.y >= 32 && sp.y < 48)
                ++inside;
        CHECK(inside == kSpellCount,
              "every spire stands inside the only admissible ground");
    }
    {
        GameState gs = world(12345u);
        store_reset(places());
        for (int y = 32; y < 48; ++y)
            for (int x = 32; x < 48; ++x)
                birth_place(gs, places(), SquadType::Village, x, y);
        generate_spires(gs, places(), pin, terrain);
        CHECK(spires_in(census(gs, places())).empty(),
              "named places on every admissible cell veto the spire");
    }
}

} // namespace

// ── §42 Инк 5: places are BORN WITH SOULS, ruins exist, the watchman ──────
void test_genesis_births_souls_and_ruins() {
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    GameState gs = world(12345u);
    store_reset(places());
    generate_spires(gs, places(), zones, terrain);
    generate_ruins(gs, places(), zones, terrain);
    const std::vector<PlaceRow> all = census(gs, places());

    // Spires are born garrisoned: the registry's born columns × the spell's
    // tier, a bell — never zero, never one fixed number for all.
    int spirePops = 0, distinctPops = 0;
    std::set<int> seenPop;
    for (const PlaceRow& r : all) {
        if (r.type != SquadType::Spire) continue;
        // Души данжа — ГОЛОВЫ его толпы (v122): паства шпиля и есть они,
        // а worked его клетки занят СПЕЛЛОМ (ломтик B).
        CHECK(r.flock > 0, "a spire is born with its garrison");
        ++spirePops;
        if (seenPop.insert(r.flock).second) ++distinctPops;
    }
    CHECK(spirePops > 0, "the sweep saw spires at all");
    CHECK(distinctPops > 1,
          "born garrisons differ spire to spire (a bell, not a constant)");

    // Ruins exist — the §42 stillborn kind lives, inside its own band, born
    // haunted from the site's danger byte.
    const LandmarkDef& ruinDef = landmark_def(SquadType::Ruin);
    int ruins = 0;
    for (const PlaceRow& r : all) {
        if (r.type != SquadType::Ruin) continue;
        ++ruins;
        CHECK(!terrain.is_water(r.x, r.y), "a ruin stands on land");
        const int z = int(zones.at(r.x, r.y));
        CHECK(z >= int(ruinDef.minZone) && z <= int(ruinDef.maxZone),
              "a ruin stands inside its registry zone band");
        CHECK(r.flock > 0, "a ruin is born haunted");
    }
    CHECK(ruins > 0, "the world places ruins");

    // Determinism: the same seed births the same ruins, souls included.
    GameState b = world(12345u);
    store_reset(places());
    generate_spires(b, places(), zones, terrain);
    generate_ruins(b, places(), zones, terrain);
    const std::vector<PlaceRow> allB = census(b, places());
    CHECK_OR_RETURN(allB.size() == all.size(),
                    "same seed, same landmark census");
    bool same = true;
    for (std::size_t i = 0; i < all.size(); ++i) {
        const PlaceRow& p = all[i];
        const PlaceRow& q = allB[i];
        if (p.type != q.type || p.x != q.x || p.y != q.y
            || p.flock != q.flock) {
            same = false;
        }
    }
    CHECK(same, "genesis is a fact of the seed, souls included");

    // THE REGISTRY WATCHMAN (§42): what genesis placed obeys the column,
    // and the placing kinds this harness ran really do declare themselves —
    // "a kind nobody rolled" can never again pose as "a kind that does not
    // exist".
    for (const PlaceRow& r : all) {
        CHECK(landmark_def(r.type).worldPlaces,
              "no pass places a kind whose row says the world does not");
    }
    CHECK(landmark_def(SquadType::Spire).worldPlaces
              && landmark_def(SquadType::Ruin).worldPlaces,
          "the placing kinds declare worldPlaces");
    CHECK(!landmark_def(SquadType::Lair).worldPlaces
              && !landmark_def(SquadType::Shrine).worldPlaces
              && !landmark_def(SquadType::Mine).worldPlaces
              && !landmark_def(SquadType::Tower).worldPlaces,
          "the deliberately-unplaced kinds say so in their rows");
}

// ── M-233 п.8: ОДИН ОТВЕТ НА «ВЫКАЧАН ЛИ ШПИЛЬ» ──────────────────────────
//
// Закон (DOD п.6; ЗАКОН СТРОКИ КАТАЛОГА): у вопроса о мире один ответ. До
// M-233 п.8 «шпиль выкачан» было написано ЧЕТЫРЕЖДЫ (сборщик фактов клетки,
// визитор мест, панель поселения, макро-оверлей), а выведенный из того же
// числа ТИР — дважды, и каждое написание само решало, что `worked_read == 0`
// значит у НЕ-шпиля. Теперь вывод один — `spire_orb@src/macro/spires.h`.
//
// ЛОВУШКА, КОТОРУЮ СТЕРЕЖЁТ ЭТОТ СВИДЕТЕЛЬ, НАЗВАНА ЧИСЛОМ КОДА, А НЕ
// ОПАСЕНИЕМ: worked-число клетки у ПОСЕЛЕНИЯ есть его ПАСТВА, и это ровно та
// же дверь `souls_flock@src/macro/labour.h`, что читает её у города. Значит
// «выкачан == worked 0» без проверки рода объявляет выкачанным каждый
// обезлюдевший город — и объявлял бы молча, потому что у города есть
// `spriteDepleted` в строке рисования.
//
// ЧЕГО ЗДЕСЬ СОЗНАТЕЛЬНО НЕ УТВЕРЖДАЕТСЯ (§8 п.7): согласие ТРЕТЬЕГО
// читателя — `cell_facts`. Ему нужен собранный конверт мира со каркасом
// клеток, то есть `npc_ai.cpp` в линковке этого прибора ради одного байта;
// а то, что он читает ту же дверь, судит стена ниже — текстом дерева, без
// прогона.
void test_one_answer_to_the_drained_orb() {
    GameState gs = world(12345u);
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    store_reset(places());
    generate_spires(gs, places(), zones, terrain);

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ С ЗУБАМИ: свидетель рождает его САМ (§8 п.11).
    const int freefolk = faction_index("freefolk");
    CHECK_OR_RETURN(freefolk >= 0, "реестр фракций несёт строку freefolk");
    const MacroHandle city = birth_place(gs, places(), SquadType::City,
                                        40, 40, std::int16_t(freefolk));
    CHECK_OR_RETURN(places().valid(city), "фикстура родила себе город");
    CHECK(landmark_def(SquadType::City).bornPopBase == 0
              && landmark_def(SquadType::Spire).bornPopBase != 0,
          "ЛОВУШКА ЖИВА В СТРОКАХ: у города паства ЕСТЬ worked-число клетки, "
          "у шпиля worked-число — спелл; одно поле, два смысла");
    CHECK(souls_flock(gs, places(), city.slot) == 0,
          "свежий город пуст — worked его клетки ноль, то есть ровно то "
          "число, которое у шпиля значит «выкачан»");
    const SpireOrb cityOrb = spire_orb(gs, places(), city.slot);
    CHECK(!cityOrb.depleted,
          "ГОРОД С ПУСТОЙ ПАСТВОЙ НЕ ВЫКАЧАН: выкачанность есть вопрос о "
          "ШПИЛЕ, и у не-шпиля она ЛОЖЬ всегда");
    CHECK(cityOrb.spell == 0 && cityOrb.tier == 0,
          "и ни спелла, ни тира у не-шпиля нет");

    // ── ЗАРЯЖЕННЫЙ ШПИЛЬ: тир выводится из ТОЙ ЖЕ таблицы, что читает мир
    // (§8 п.4 — никогда пересказанным числом).
    int charged = 0, wrongTier = 0, wrongSpell = 0, falseDepleted = 0;
    for_each_place(places(), [&](std::uint16_t slot) {
        if (SquadType(places().runtime[slot].squadType) != SquadType::Spire)
            return;
        ++charged;
        const SpireOrb o = spire_orb(gs, places(), slot);
        if (o.depleted) ++falseDepleted;
        if (o.spell <= 0 || o.spell > kSpellCount) ++wrongSpell;
        else if (o.tier != kSpellDefs[o.spell - 1].tier) ++wrongTier;
    });
    CHECK(charged > 0, "мир действительно поставил шпили — иначе цикл ничего "
                       "не померил (§8 п.2-3)");
    CHECK(falseDepleted == 0, "рождённый шпиль ЗАРЯЖЕН, а не выкачан");
    CHECK(wrongSpell == 0, "спелл орба есть законный ординал реестра + 1");
    CHECK(wrongTier == 0, "тир орба есть колонка силы ЕГО строки спелла");

    // ── ПЕРЕХОД: орб забрали. Та же дверь обязана перевернуться, и ВИД,
    // который публикует визитор мест, обязан перевернуться ВМЕСТЕ с ней —
    // это и есть проверка ПРОВОДКИ, а не тавтология: написание, оторванное
    // от двери, здесь расходится.
    std::uint16_t victim = 0;
    bool found = false;
    for_each_place(places(), [&](std::uint16_t slot) {
        if (found) return;
        if (SquadType(places().runtime[slot].squadType) != SquadType::Spire)
            return;
        victim = slot;
        found = true;
    });
    CHECK_OR_RETURN(found, "нашёлся шпиль, у которого можно забрать орб");
    const int vx = ecs::cell_x(places().cell[victim], gs.mapW);
    const int vy = ecs::cell_y(places().cell[victim], gs.mapW);
    const int victimId = int(places().spawnId[victim].index);
    worked_write(gs, vx, vy, 0);   // ровно то, что делает рука игрока внизу

    const SpireOrb drained = spire_orb(gs, places(), victim);
    CHECK(drained.depleted && drained.spell == 0 && drained.tier == 0,
          "забранный орб: выкачан, спелла нет, тир ноль — выкачанный шпиль "
          "ЗАБЫЛ свой спелл, как выработанная жила — свою руду");

    int seenVictim = 0, viewDisagreed = 0, cityViewDepleted = 0;
    for_each_landmark(gs, places(), [&](const LandmarkView& lv) {
        const bool byKind = lv.type == SquadType::Spire;
        if (lv.id == victimId) {
            ++seenVictim;
            if (!lv.depleted) ++viewDisagreed;
        } else if (byKind && lv.depleted) {
            ++viewDisagreed;   // остальные шпили заряжены
        }
        if (lv.type == SquadType::City && lv.depleted) ++cityViewDepleted;
    });
    CHECK(seenVictim == 1, "визитор мест выдал обобранный шпиль РОВНО один раз");
    CHECK(viewDisagreed == 0,
          "вид места несёт ТУ ЖЕ выкачанность, что дверь — он её читает, а "
          "не выводит заново");
    CHECK(cityViewDepleted == 0,
          "и город с пустой паствой у визитора тоже НЕ выкачан");
}

// ── M-39: ОДИН ОТВЕТ НА «ЧЬЯ ЭТО РУИНА» ──────────────────────────────────
// Закон (§7 «ОДИН РЕЕСТР ФРАКЦИЙ», DOD п.6, вердикт владельца 2026-09-21
// «руине надо дать фракцию»): владельца места называет ЭКЗЕМПЛЯР, и только
// он. До M-39 отвечали двое — строковая колонка `spawnFaction` ВИДА места и
// колонка `factionIdx` экземпляра, — и второй ответ держался лишь тем, что
// генераторы руин и шпилей не ставили индекс вовсе. Свидетеля у этого не
// было ни одного: `rg spawnFaction tests/` не давал ни строки.
//
// Здесь утверждается СТОРОНА ГЕНЕЗИСА: каждая рождённая руина и каждый шпиль
// несут индекс демонов, и его видит та самая дверь, которой спрашивает
// заселение. Вторая половина закона — что знамя места НЕ достаёт до дичи на
// земле — живёт у своего предмета, в fauna_registry_test.
// Негативный контроль встроен: прежнее поведение давало `-1`, то есть
// freefolk через дверь бесхозной земли, и утверждение про демонов краснеет.
// Сам контроль проверяется — счётчики руин и шпилей обязаны быть > 0, иначе
// цикл ничего не померил (§8 п.2-3).
void test_place_faction_is_the_instance_only() {
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();

    // ОРДИНАЛ ВЫВОДИТСЯ ИЗ ТОГО ЖЕ РЕЕСТРА, что читает мир, никогда не
    // переписывается числом (§8 п.4).
    const int demons = faction_index("demons");
    CHECK_OR_RETURN(demons >= 0, "the faction registry carries the demons row");
    const int freefolk = faction_index("freefolk");
    CHECK_OR_RETURN(freefolk >= 0 && freefolk != demons,
                    "freefolk is a DIFFERENT row — the negative control has "
                    "something to be wrong about");

    // Один сид — не число: три мира (§5 п.4).
    int ruins = 0, spires = 0, wrong = 0;
    for (std::uint32_t seed : {12345u, 777u, 2026u}) {
        GameState gs = world(seed);
        store_reset(places());
        generate_spires(gs, places(), zones, terrain);
        generate_ruins(gs, places(), zones, terrain);
        for (const PlaceRow& r : census(gs, places())) {
            if (r.type != SquadType::Ruin
                && r.type != SquadType::Spire) {
                continue;
            }
            if (r.type == SquadType::Ruin) ++ruins; else ++spires;
            // Хранимый индекс И ответ двери, которой спрашивает заселение
            // (sub/engine.cpp spawn_cell / enter_dungeon_scene). Раньше тут
            // стоял бы freefolk, а демонов доставала колонка вида.
            if (int(r.faction) != demons
                || int(faction_or_freefolk(r.faction)) != demons) {
                ++wrong;
            }
        }
    }
    CHECK(ruins > 0 && spires > 0,
          "the three worlds really did place ruins AND spires");
    CHECK(wrong == 0,
          "every ruin and spire is BORN with its own faction index — the "
          "kind's column is not what answers");
}

// ── СТЕНА: ВТОРОГО ВЫВОДА О ОРБЕ В ДЕРЕВЕ НЕТ ────────────────────────────
//
// §0 п.2: утверждение об ОТСУТСТВИИ адреса в коде не имеет и указывает на
// СВОЙ ТЕСТ. Прецедент формы — `tests/world_fold_law_test.cpp`. Разница с ним
// одна и она в пользу стены: канал ПУСТ после M-233 п.8, поэтому послаблений
// нет вовсе — ни белого списка, ни тающего остатка.
//
// ПРАВИЛО, И ПОЧЕМУ ИМЕННО ОНО. Вывод о орбе опознаётся по ПАРЕ: файл
// спрашивает `worked_read` И называет `SquadType::Spire`. Пара и есть
// преступление — у worked-числа два смысла (паства у поселения, спелл у
// шпиля), поэтому один `worked_read` невинен, один `SquadType::Spire` невинен,
// а вместе они значат «я вывожу состояние орба сам». Комментарии и строковые
// литералы срезаются: иначе прибор ловил бы собственные объяснения, а
// стрижка может его только ОСЛЕПИТЬ, не обмануть.
//
// ДВА ИМЕНИ РАЗРЕШЕНЫ, И ОБА НАЗВАНЫ, А НЕ ПРОЩЕНЫ:
//   · `src/macro/spires.cpp` — САМА дверь плюс запись орба в генезисе;
//   · `src/app/smoke.cpp` — смоук-свидетели, которым и положено писать СЫРУЮ
//     истину слоя, чтобы проверить ею производный ответ (тот же приём, что
//     объяснён в `tests/deposit_reach_test.cpp`: справа стоит ВОПРОС, а не
//     вторая копия реализации).
// `worked_write` под правило не попадает: у ЗАПИСИ орба один писатель-акт
// (рука игрока в субмире), и он не вывод.
constexpr const char* kOrbDoor = "src/macro/spires.cpp";
constexpr const char* kOrbWitness = "src/app/smoke.cpp";

std::string strip_comments_and_strings(const std::string& line) {
    std::string out;
    bool inStr = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (!inStr && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/')
            break;                       // хвост строки — комментарий
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) inStr = !inStr;
        else if (!inStr) out.push_back(line[i]);
    }
    return out;
}

void test_no_second_orb_derivation_in_the_tree() {
    namespace fs = std::filesystem;
    const fs::path root{TIMAERT_SOURCE_DIR};
    const fs::path src = root / "src";
    CHECK_OR_RETURN(fs::is_directory(src),
                    "прибор нашёл дерево исходников — иначе он мерил пустоту");

    int scanned = 0;
    std::vector<std::string> offenders;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(src)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".h" && ext != ".cpp") continue;
        const std::string rel =
            fs::relative(e.path(), root).generic_string();
        if (rel == kOrbDoor || rel == kOrbWitness) continue;
        std::ifstream in(e.path());
        if (!in) continue;
        ++scanned;
        bool readsWorked = false, namesSpire = false;
        std::string line;
        while (std::getline(in, line)) {
            const std::string code = strip_comments_and_strings(line);
            if (code.find("worked_read") != std::string::npos) readsWorked = true;
            if (code.find("SquadType::Spire") != std::string::npos)
                namesSpire = true;
        }
        if (readsWorked && namesSpire) offenders.push_back(rel);
    }

    CHECK(scanned > 100,
          "стена прочла всё дерево, а не три файла — иначе ноль нарушений "
          "означает «не смотрел», а не «чисто»");
    for (const std::string& f : offenders) {
        std::printf("  ВТОРОЙ ВЫВОД О ОРБЕ: %s спрашивает worked_read и "
                    "называет SquadType::Spire\n", f.c_str());
    }
    CHECK(offenders.empty(),
          "выкачанность шпиля выводится РОВНО в одном месте мира "
          "(spire_orb@src/macro/spires.h) — второго написания в дереве нет");
}

int main() {
    test_one_spire_per_spell_in_the_band();
    test_placement_is_a_fact_of_the_seed();
    test_no_admissible_ground_places_nothing();
    test_named_places_veto_their_cells();
    test_genesis_births_souls_and_ruins();
    test_place_faction_is_the_instance_only();
    test_one_answer_to_the_drained_orb();
    test_no_second_orb_derivation_in_the_tree();
    return sm::test::report("spire_generation_test");
}
