// Spire placement (macro/spires.cpp). Pinned promises:
//   · ШПИЛЬ И РУИНА — ЧИСТЫЕ ФИЧИ, СКВАДА У НИХ НЕТ НИ ОДНОГО (M-320,
//     вердикт владельца 2026-10-26). Поэтому ВСЁ, что ниже, спрашивается у
//     БАЙТА КЛЕТКИ, а не у стора сквадов, и отсутствие сквада утверждается
//     ПРЯМО — это негативный контроль самого сноса;
//   · one spire per kSpellDefs row (the generator asks the spell registry
//     itself — Rule 13); the spell is the spire cell's WORKED number (v120:
//     ordinal + 1, 0 = drained), born charged. Проверяется МНОЖЕСТВОМ
//     {1..kSpellCount}, а не порядком рождения тел, которого больше нет;
//   · a spire stands on land, inside the place table's own zone band
//     (place_def(Spire).minZone), and the top-tier spell's spire stands at
//     the band's CAP — the doom spell lives where the world is at its worst;
//   · занятая клетка ветирует шпиль, и занятость спрашивается у БАЙТА
//     (`place_kind_of_feature`, та же дверь, что у постановки шахт M-321);
//     the pick spreads: best-candidate sampling never stacks two side by side;
//   · placement is a fact of the world seed (the spell rows are compile-time
//     constants): same seed reproduces the sites, another seed moves them;
//   · a world with no admissible ground places NOTHING (negative control for
//     the zone gate and the occupancy veto both).
//
// ── ТАБЛИЦА МУТАЦИЙ M-320, ОБЕ ПОЛЯРНОСТИ, С ЧИСЛОМ (§8 п.6) ─────────────
// Незаписанный контроль доказывает, что детектор видел дефект ОДИН РАЗ — в
// голове автора; следующий правит прибор вслепую. Поэтому числа здесь:
//
//   ОБЯЗАНЫ КРАСНИТЬ:
//   · фикстура рожает руине сквад (`birth_place(…Ruin…)` перед переписью)
//       → КРАСНЫЙ, **1 из 77** — утверждение «сквадов ноль» имеет зубы;
//   · гейт сквада возвращён над родом клетки (`cell_facts`: `.type` только
//     при `manned`) — то есть ровно тот дефект, который наряд и лечил
//       → `check` **138/138 ЗЕЛЁНЫЙ**, краснеет ОДИН смоук из 63
//         (`spire_climb`). **СЮИТА ЭТОТ ЗАКОН НЕ ОХРАНЯЕТ**, и это тот же
//         класс слепоты, что замерен в M-318 Б («род ВСЕГДА None» → 137/137
//         зелено): ctest судит ЧИСЛА, а здесь ломается ОТВЕТ О КЛЕТКЕ,
//         которого не спрашивает ни один прибор сюиты;
//
//   ОБЯЗАНА МОЛЧАТЬ (детектор, краснеющий на всё, — не детектор):
//   · `kRuinBaseCount` 4 → 5 (балансное число, закона не нарушает)
//       → `check` 138/138, смоуки 63/63 — МОЛЧАТ, как и должны;
//
//   НЕ ЗАСЧИТАНЫ:
//   · снос вызова `cell_taken_` целиком → `-Werror -Wunused-function`, то
//     есть проверен КОМПИЛЯТОР, а не прибор;
//   · вето занятости никогда не срабатывает (`== SquadType::Count`)
//       → `check` 138/138 и смоуки 63/63 МОЛЧАТ, но это НЕ дыра прибора:
//         замер показал, что размещение 70 руин ИДЕНТИЧНО с вето и без него
//         (сид 12345, 0 коллизий), то есть эффект мутации ниже порога
//         наблюдения. Вето есть страж редкого случая (занятых клеток ~0.2 %
//         мира), а не несущее поведение, — и сказано это ЧИСЛОМ, а не
//         «мутация прошла, значит прибор слеп».
#include "check.h"
#include "macro/labour.h"   // souls_flock — паства ГОРОДА (живая половина)

#include "macro/place_registry.h"
#include "macro/map_generator.h"
#include "macro/place_iter.h"  // for_each_place — перепись СКВАДОВ (их ноль)
#include "macro/place_birth.h"  // birth_place — сквад города/деревни фикстуры
#include "macro/ruins.h"
#include "macro/anketa.h"
#include "macro/spires.h"
#include "macro/state.h"
#include "macro/store.h"
#include "macro/zones.h"
#include "tables/faction.h"    // faction_index — знамя ГОРОДА фикстуры

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

// ── ПЕРЕПИСЬ ШПИЛЕЙ И РУИН — ПО БАЙТУ ФИЧИ, А НЕ ПО СКВАДАМ (M-320) ──────
//
// ЗДЕСЬ СТОЯЛА `census` ПО КОЛОНКАМ ТЕЛ (род, адрес, ординал, паства,
// фракция одним проходом), И ОНА УМЕРЛА ЦЕЛИКОМ ВМЕСТЕ С ПРЕДМЕТОМ: четыре
// её пятых — ординал, паства, фракция и сам род сквада — у шпиля и руины
// больше не существуют. Снёс её КОМПИЛЯТОР, а не глаза: после переезда всех
// утверждений на байт она осталась без звонящих и упала `-Wunused-function`.
// Это и есть доказательство, что переезд ПОЛНЫЙ, — ни одно утверждение
// файла не осталось висеть на сквадах.
//
// Вердикт владельца 2026-10-26: шпиль — «да под снос», руина — «под снос как
// шпиль, чистая фича без сквада»; и о форме мира: «МЕСТ НЕТ! СКВАДЫ ЭТО
// СКВАДЫ ФИЧИ АГНОСТИЧНЫЙ НЕЗАВИСИМЫЙ СКВАД МОДУЛЬ РЕШАЕТ ЧТО ТАМ
// ПРОИСХОДИТ С НИМ И МИРОМ ФИЧИ ПРОСТО ЕСТЬ В МИРЕ».
//
// ПОЧЕМУ СВИДЕТЕЛЬ ПЕРЕЕХАЛ, А НЕ ПОДОГНАН: он спрашивал о шпиле У СКВАДА, а
// сквада у шпиля больше нет ни одного — то есть умер НОСИТЕЛЬ его вопроса,
// а не закон (§5 п.6). Законы те же: один шпиль на строку каталога, на земле,
// в полосе своей строки, врозь, по сиду. Спрашиваются они теперь у того, чем
// шпиль ЯВЛЯЕТСЯ, — у байта клетки.
//
// И ЭТО СИЛЬНЕЕ ПРЕЖНЕГО СВИДЕТЕЛЯ, А НЕ СЛАБЕЕ: раньше «спелл есть ординал
// + 1» проверялось по ПОРЯДКУ РОЖДЕНИЯ тел (`spires[i]` ↔ `i + 1`), то есть
// держалось на том, что свободный список отдаёт слоты по возрастанию. Теперь
// утверждается МНОЖЕСТВО: числа под байтами шпилей обязаны быть РОВНО
// {1..kSpellCount} без повторов — порядок обхода в это утверждение не входит.
std::vector<std::uint32_t> feature_cells(const FeatureLayer& fl,
                                         FeatureType want) {
    std::vector<std::uint32_t> out;
    for (std::size_t c = 0; c < fl.data.size(); ++c)
        if (FeatureLayer::decode(fl.data[c]) == want)
            out.push_back(std::uint32_t(c));
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


// Слой фич на один прогон генератора: фичу рода теперь ставит рождение
// места (вердикт 2026-10-23), и генератору нужен носитель. Свидетели этого
// файла судят МЕСТА (тела и их клетки) — слой им не предмет, поэтому каждая
// генерация берёт свежий.
sm::FeatureLayer fresh_features(int w, int h) {
    sm::FeatureLayer fl;
    fl.resize(w, h);
    return fl;
}

void test_one_spire_per_spell_in_the_band() {
    GameState gs = world(12345u);
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    store_reset(places());
    sm::FeatureLayer flA = fresh_features(terrain.width, terrain.height);
    generate_spires(gs, zones, terrain, flA);

    const PlaceDef& def = place_def(SquadType::Spire);
    const std::vector<std::uint32_t> spires = feature_cells(flA, FT_Spire);
    CHECK_OR_RETURN(spires.size() == std::size_t(kSpellCount),
                    "every learnable spell got its spire");
    // СКВАДА У ШПИЛЯ НЕТ НИ ОДНОГО, И ЭТО УТВЕРЖДАЕТСЯ ПРЯМО — иначе снос
    // проверялся бы только отсутствием красного, а отсутствие красного
    // доказывает лишь то, что никто не смотрел.
    int spireSquads = 0;
    for_each_place(places(), [&](std::uint16_t slot) {
        if (SquadType(places().runtime[slot].squadType) == SquadType::Spire)
            ++spireSquads;
    });
    CHECK(spireSquads == 0,
          "шпиль есть ЧИСТАЯ ФИЧА: сквада он не рождает ни одного (M-320)");

    // ЗДЕСЬ СТОЯЛИ ДВА УТВЕРЖДЕНИЯ ОБ ОРДИНАЛЕ («уникален у одного эмитента»,
    // «эмитент монотонен»), И ОНИ СНЯТЫ ВМЕСТЕ СО СВОИМ НОСИТЕЛЕМ: ординал
    // выдаёт РОЖДЕНИЕ СКВАДА, а шпиль сквада не рождает, значит ординала у
    // него нет вовсе — не ноль, не -1, а нет как вопроса. Сам закон одного
    // эмитента ЖИВ и судится там, где жив его предмет: `ordinal_space_test`
    // (города и деревни). Снимать его здесь — не ослабление сюиты, а отказ
    // спрашивать о несуществующем.
    //
    // СПЕЛЛ ПРОВЕРЯЕТСЯ МНОЖЕСТВОМ, А НЕ ПОРЯДКОМ: числа под байтами шпилей
    // обязаны составить РОВНО {1..kSpellCount}. Повтор означал бы, что два
    // шпиля несут один спелл, пропуск — что спелл не предложен миру.
    std::set<int> spells;
    for (const std::uint32_t c : spires) {
        const int x = cell_x(c, gs.mapW), y = cell_y(c, gs.mapW);
        const int spell = worked_read(gs, x, y);
        CHECK(spell >= 1 && spell <= kSpellCount
                  && spells.insert(spell).second,
              "клетка шпиля работает СВОЙ спелл: ординал реестра + 1, и два "
              "шпиля одного спелла не несут");
        CHECK(!terrain.is_water(x, y), "a spire stands on land");
        CHECK(int(zones.at(x, y)) >= int(def.minZone),
              "a spire stands inside the place table's zone band");
    }
    CHECK(spells.size() == std::size_t(kSpellCount),
          "каждый спелл каталога получил РОВНО один шпиль");
    // The top-tier spell demands the band's cap (the table's maxZone), and
    // this world has free zone-9 ground, so no relaxation may kick in.
    // Клетка ищется ПО СВОЕМУ ЧИСЛУ — порядка рождения больше нет.
    const int doomSpell = top_tier_ordinal() + 1;
    int doomSeen = 0;
    for (const std::uint32_t c : spires) {
        const int x = cell_x(c, gs.mapW), y = cell_y(c, gs.mapW);
        if (worked_read(gs, x, y) != doomSpell) continue;
        ++doomSeen;
        CHECK(int(zones.at(x, y)) == int(def.maxZone),
              "the top-tier spire stands at the band's cap");
    }
    CHECK(doomSeen == 1, "шпиль старшего спелла нашёлся, и ровно один");
    // Best-candidate spread: with the whole wild half free, spires never end
    // up stacked or adjacent (the veto alone only forbids the same cell).
    int minPair = kW + kH;
    for (std::size_t a = 0; a < spires.size(); ++a)
        for (std::size_t b = a + 1; b < spires.size(); ++b)
            minPair = std::min(minPair,
                               torus_cheb(cell_x(spires[a], gs.mapW),
                                          cell_y(spires[a], gs.mapW),
                                          cell_x(spires[b], gs.mapW),
                                          cell_y(spires[b], gs.mapW)));
    CHECK(minPair >= 2, "spires spread - no two side by side");
}

void test_placement_is_a_fact_of_the_seed() {
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    GameState a = world(12345u), b = world(12345u), c = world(777u);
    // Слой на прогон СВОЙ: сид есть факт мира, и мир у каждого прогона свой.
    sm::FeatureLayer flB = fresh_features(terrain.width, terrain.height);
    generate_spires(a, zones, terrain, flB);
    const std::vector<std::uint32_t> sa = feature_cells(flB, FT_Spire);
    sm::FeatureLayer flC = fresh_features(terrain.width, terrain.height);
    generate_spires(b, zones, terrain, flC);
    const std::vector<std::uint32_t> sb = feature_cells(flC, FT_Spire);
    sm::FeatureLayer flD = fresh_features(terrain.width, terrain.height);
    generate_spires(c, zones, terrain, flD);
    const std::vector<std::uint32_t> sc = feature_cells(flD, FT_Spire);

    CHECK_OR_RETURN(sa.size() == sb.size()
                        && sa.size() == std::size_t(kSpellCount),
                    "both same-seed runs placed the full registry");
    // Перепись идёт по слою, то есть УЖЕ отсортирована по адресу клетки:
    // сравнение множеств адресов и есть «те же площадки», без оговорки о
    // порядке рождения, которой прежняя форма молча пользовалась.
    CHECK(sa == sb, "same seed reproduces the same sites");
    CHECK(sa != sc, "another seed is another world - some spire moved");
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
        sm::FeatureLayer flE = fresh_features(terrain.width, terrain.height);
        generate_spires(gs, tame, terrain, flE);
        CHECK(feature_cells(flE, FT_Spire).empty(),
              "no wild land = no spires");
    }
    // Negative control 2: an all-ocean world offers no site either.
    {
        GameState gs = world(12345u);
        TerrainData ocean;
        ocean.width = kW;
        ocean.height = kH;
        ocean.rgba.assign(std::size_t(kW * kH) * 4u, 0);
        sm::FeatureLayer flF = fresh_features(ocean.width, ocean.height);
        generate_spires(gs, banded_zones(), ocean, flF);
        CHECK(feature_cells(flF, FT_Spire).empty(),
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
        sm::FeatureLayer flG = fresh_features(terrain.width, terrain.height);
        generate_spires(gs, pin, terrain, flG);
        const std::vector<std::uint32_t> spires = feature_cells(flG, FT_Spire);
        CHECK_OR_RETURN(spires.size() == std::size_t(kSpellCount),
                        "the wild block hosts every spire");
        int inside = 0;
        for (const std::uint32_t c : spires) {
            const int x = cell_x(c, gs.mapW), y = cell_y(c, gs.mapW);
            if (x >= 32 && x < 48 && y >= 32 && y < 48) ++inside;
        }
        CHECK(inside == kSpellCount,
              "every spire stands inside the only admissible ground");
    }
    {
        // ПРЕДУСЛОВИЕ РОЖДАЕТСЯ ПОЛНОСТЬЮ, И ЭТО ПОПРАВКА M-320 (§8 п.11).
        // Здесь `birth_place` звалась БЕЗ слоя фич, то есть деревни вставали
        // телами, а байтов `FT_Village` в мире не появлялось ни одного —
        // предусловие было рождено ПОЛОВИНОЙ. Прежний вето-скан смотрел в
        // стор и этого не замечал; новый спрашивает БАЙТ, и половинная
        // фикстура покраснела бы честно. Деревня здесь — носитель вето, а не
        // украшение, значит рождается она со всем, чем является.
        GameState gs = world(12345u);
        store_reset(places());
        sm::FeatureLayer flI = fresh_features(terrain.width, terrain.height);
        for (int y = 32; y < 48; ++y)
            for (int x = 32; x < 48; ++x)
                birth_place(gs, places(), SquadType::Village, x, y, -1,
                            nullptr, &flI, &terrain);
        int vetoCells = 0;
        for (int y = 32; y < 48; ++y)
            for (int x = 32; x < 48; ++x)
                if (flI.at(x, y) == FT_Village) ++vetoCells;
        CHECK_OR_RETURN(vetoCells == 16 * 16,
                        "фикстура заняла ВСЮ пригодную землю — иначе вето "
                        "нечем проверять, и пустой результат ничего не значит");
        generate_spires(gs, pin, terrain, flI);
        CHECK(feature_cells(flI, FT_Spire).empty(),
              "занятая клетка ветирует шпиль — вопрос задан БАЙТУ");
    }
}

} // namespace

// ── M-320: ШПИЛЬ И РУИНА — ЧИСТЫЕ ФИЧИ, И НИ ОДНОГО СКВАДА НА НИХ ─────────
//
// ЗДЕСЬ УТВЕРЖДАЛОСЬ «МЕСТА РОЖДАЮТСЯ С ДУШАМИ» (§42 Инк 5): у шпиля
// гарнизон колоколом, у руины — наваждение от байта опасности. ОБА
// УТВЕРЖДЕНИЯ СНЯТЫ ВМЕСТЕ СО СВОИМ НОСИТЕЛЕМ, и это не ослабление сюиты, а
// вердикт владельца 2026-10-26: шпиль «да под снос», руина «под снос как
// шпиль — чистая фича без сквада».
//
// ЧТО ИМЕННО ОНИ ОХРАНЯЛИ, И ПОЧЕМУ ЭТО БЫЛО ВРЕДНО: гарнизон такого сквада
// есть ОДНА ГОЛОВА, поэтому его сносил первый прохожий макро-автобоем —
// замер 256 дней (сид 12345) дал сквадов руины **70 → 26**, шпиля **10 → 2**,
// при том что байт фичи держался **70 и 10 ПЛОСКО все 256 дней**. То есть
// старое утверждение «рождён с гарнизоном» было верным ровно в день ноль и
// охраняло механику, которая убивала вход в данж.
//
// ВМЕСТО НИХ УТВЕРЖДАЕТСЯ ОБРАТНОЕ, И ПРЯМО: сквадов у этих родов НОЛЬ, а
// фичи на месте. Это негативный контроль самого сноса — он краснеет, если
// рождение сквада вернётся.
void test_genesis_places_ruins_and_spires_as_features() {
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    GameState gs = world(12345u);
    store_reset(places());
    sm::FeatureLayer flH = fresh_features(terrain.width, terrain.height);
    generate_spires(gs, zones, terrain, flH);
    generate_ruins(gs, places(), zones, terrain, flH);

    int ruinSquads = 0, spireSquads = 0;
    for_each_place(places(), [&](std::uint16_t slot) {
        const SquadType k = SquadType(places().runtime[slot].squadType);
        if (k == SquadType::Ruin) ++ruinSquads;
        else if (k == SquadType::Spire) ++spireSquads;
    });
    CHECK(ruinSquads == 0 && spireSquads == 0,
          "ни руина, ни шпиль не рождают сквада — они ЧИСТЫЕ ФИЧИ (M-320)");

    // Ruins exist — the §42 stillborn kind lives, inside its own band, on
    // land. Спрашивается это у БАЙТА: им руина и является.
    const PlaceDef& ruinDef = place_def(SquadType::Ruin);
    const std::vector<std::uint32_t> ruinCells = feature_cells(flH, FT_Ruin);
    for (const std::uint32_t c : ruinCells) {
        const int x = cell_x(c, gs.mapW), y = cell_y(c, gs.mapW);
        CHECK(!terrain.is_water(x, y), "a ruin stands on land");
        const int z = int(zones.at(x, y));
        CHECK(z >= int(ruinDef.minZone) && z <= int(ruinDef.maxZone),
              "a ruin stands inside its registry zone band");
    }
    CHECK(!ruinCells.empty(), "the world places ruins");
    // РУИНА И ШПИЛЬ НЕ ДЕЛЯТ КЛЕТКУ — вето спрашивает БАЙТ, и шпили в этом
    // прогоне встали ПЕРВЫМИ, то есть у руин было чем подавиться.
    CHECK(feature_cells(flH, FT_Spire).size()
              == std::size_t(kSpellCount),
          "шпили прохода руин не потеряли: байт шпиля руина не затирает");

    // Determinism: the same seed stamps the same cells.
    GameState b = world(12345u);
    store_reset(places());
    sm::FeatureLayer flB2 = fresh_features(terrain.width, terrain.height);
    generate_spires(b, zones, terrain, flB2);
    generate_ruins(b, places(), zones, terrain, flB2);
    CHECK(feature_cells(flB2, FT_Ruin) == ruinCells
              && feature_cells(flB2, FT_Spire)
                     == feature_cells(flH, FT_Spire),
          "genesis is a fact of the seed: те же клетки под теми же байтами");

    // THE REGISTRY WATCHMAN (§42): what genesis placed obeys the column,
    // and the placing kinds this harness ran really do declare themselves —
    // "a kind nobody rolled" can never again pose as "a kind that does not
    // exist".
    // ЗДЕСЬ СТОЯЛИ ТРИ ПРОВЕРКИ КОЛОНКИ `worldPlaces`, И ОНИ ОХРАНЯЛИ ДУБЛЬ
    // (M-246, снесены 2026-10-18 вместе с колонкой). Колонка объявляла,
    // расставляет ли генезис род, — а сами размещатели брали род ЛИТЕРАЛОМ и
    // её не читали НИ РАЗУ: свидетель сверял данные с хардкодом, то есть
    // утверждал согласие двух ответов вместо одного закона. Что РАССТАВЛЕНО
    // на самом деле, этот тест по-прежнему судит выше — по телам в сторе.
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
    // СЛОЙ ФИЧ — ДО МЕСТ (2026-10-23): рождение ставит фичу рода само.
    FeatureLayer feat;
    feat.width = kW;
    feat.height = kH;
    feat.data.assign(std::size_t(kW) * std::size_t(kH), std::uint8_t(FT_None));
    generate_spires(gs, zones, terrain, feat);

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ С ЗУБАМИ: свидетель рождает его САМ (§8 п.11).
    const int freefolk = faction_index("freefolk");
    CHECK_OR_RETURN(freefolk >= 0, "реестр фракций несёт строку freefolk");
    const MacroHandle city = birth_place(gs, places(), SquadType::City,
                                        40, 40, std::int16_t(freefolk),
                                        nullptr, &feat, &terrain);
    CHECK_OR_RETURN(places().valid(city), "фикстура родила себе город");
    // ЛОВУШКА ЖИВА, НО КОЛОНКОЙ ЕЁ БОЛЬШЕ НЕ НАЗВАТЬ (M-320 снёс `bornPop*`).
    // Она про ОДНО ПОЛЕ С ДВУМЯ СМЫСЛАМИ: число поля фич под клеткой — это
    // просто число, и смысл ему даёт МОДУЛЬ СКВАДА, который на нём стоит
    // (ЗАКОН ПОЛЯ п.3). Под `FT_City` модуль города читает его как свою
    // популяцию; под `FT_Spire` там спелл, и сквада нет вовсе. Поэтому
    // утверждение теперь о ЧИСЛЕ, а не о колонке: у свежего города оно ноль
    // — ровно то значение, которое у шпиля значит «выкачан», — и ни один
    // вывод о выкачанности не смеет спутать их без вопроса о ФИЧЕ.
    CHECK(worked_read(gs, 40, 40) == 0,
          "свежий город пуст — число поля под ним ноль, то есть ровно то "
          "число, которое под FT_Spire значит «выкачан»");

    // Предусловие рождено выше ВМЕСТЕ с местами: фичи рода встали в их
    // дверях рождения (перештамп из тел умер 2026-10-23).
    MacroWorld mw{};
    mw.gs = &gs;
    mw.store = &places();
    mw.features = &feat;
    mw.terrain = &terrain;

    // РИСК, КОТОРЫЙ ПРИНЁС КЛЮЧ-ФИЧА, И ОН ПИНИТСЯ ЗДЕСЬ. Байт фичи у
    // клетки ОДИН, и пишет его не только штамп поселений: `plough_field_cell`
    // ставит `FT_Field`. Затёртая фича молча превращает число орба в чужое
    // число — шпиль читается «не шпиль», выкачанный выглядит заряженным.
    // Клетку шпиля затирать сегодня некому (`cell_taken` ветирует занятую
    // клетку по БАЙТУ), и вот это утверждение:
    const std::vector<std::uint32_t> spireCells = feature_cells(feat, FT_Spire);
    CHECK(spireCells.size() == std::size_t(kSpellCount),
          "штамп прошёл по настоящим шпилям, и их столько же, сколько спеллов");
    CHECK(feat.at(40, 40) == FT_City,
          "а на клетке города стоит ЕГО фича — значит ключ различает их");

    const SpireOrb cityOrb = spire_orb(mw, 40, 40);
    CHECK(!cityOrb.depleted,
          "ГОРОД С ПУСТОЙ ПАСТВОЙ НЕ ВЫКАЧАН: выкачанность есть вопрос о "
          "ШПИЛЕ, и у не-шпиля она ЛОЖЬ всегда");
    CHECK(cityOrb.spell == 0 && cityOrb.tier == 0,
          "и ни спелла, ни тира у не-шпиля нет");

    // ── ЗАРЯЖЕННЫЙ ШПИЛЬ: тир выводится из ТОЙ ЖЕ таблицы, что читает мир
    // (§8 п.4 — никогда пересказанным числом).
    int charged = 0, wrongTier = 0, wrongSpell = 0, falseDepleted = 0;
    for (const std::uint32_t c : spireCells) {
        ++charged;
        const SpireOrb o = spire_orb(mw, cell_x(c, gs.mapW),
                                     cell_y(c, gs.mapW));
        if (o.depleted) ++falseDepleted;
        if (o.spell <= 0 || o.spell > kSpellCount) ++wrongSpell;
        else if (o.tier != kSpellDefs[o.spell - 1].tier) ++wrongTier;
    }
    CHECK(charged > 0, "мир действительно поставил шпили — иначе цикл ничего "
                       "не померил (§8 п.2-3)");
    CHECK(falseDepleted == 0, "рождённый шпиль ЗАРЯЖЕН, а не выкачан");
    CHECK(wrongSpell == 0, "спелл орба есть законный ординал реестра + 1");
    CHECK(wrongTier == 0, "тир орба есть колонка силы ЕГО строки спелла");

    // ── ПЕРЕХОД: орб забрали. Та же дверь обязана перевернуться, и ВИД,
    // который публикует визитор мест, обязан перевернуться ВМЕСТЕ с ней —
    // это и есть проверка ПРОВОДКИ, а не тавтология: написание, оторванное
    // от двери, здесь расходится.
    CHECK_OR_RETURN(!spireCells.empty(),
                    "нашёлся шпиль, у которого можно забрать орб");
    const int vx = cell_x(spireCells[0], gs.mapW);
    const int vy = cell_y(spireCells[0], gs.mapW);
    worked_write(gs, vx, vy, 0);   // ровно то, что делает рука игрока внизу

    const SpireOrb drained = spire_orb(mw, vx, vy);
    CHECK(drained.depleted && drained.spell == 0 && drained.tier == 0,
          "забранный орб: выкачан, спелла нет, тир ноль — выкачанный шпиль "
          "ЗАБЫЛ свой спелл, как выработанная жила — свою руду");
    // ОСТАЛЬНЫЕ ШПИЛИ НЕ ТРОНУТЫ: дверь отвечает О КЛЕТКЕ, а не о роде вообще.
    int otherDepleted = 0;
    for (std::size_t i = 1; i < spireCells.size(); ++i)
        if (spire_orb(mw, cell_x(spireCells[i], gs.mapW),
                      cell_y(spireCells[i], gs.mapW)).depleted)
            ++otherDepleted;
    CHECK(otherDepleted == 0,
          "обобран РОВНО один шпиль — выкачанность есть факт КЛЕТКИ");

    // ЗДЕСЬ СТОЯЛА ПРОВЕРКА ВИЗИТОРА МЕСТ (`for_each_place_view`) НА ТОМ ЖЕ
    // ОБОБРАННОМ ШПИЛЕ, И ЕЁ ПРЕДМЕТ СНЯТ M-320: визитор обходит СКВАДЫ, а
    // сквада у шпиля нет ни одного, значит «вид места несёт ту же
    // выкачанность, что дверь» — утверждение о пустом множестве.
    //
    // ⚠ И ЭТО НАЗВАННАЯ ДЫРА, А НЕ ЗАКРЫТЫЙ ВОПРОС: колонка
    // `PlaceView::depleted@src/macro/place_iter.h` после сноса ВСЕГДА ложна,
    // то есть стала сиротой (DOD п.9), а её читатель
    // `macro_lighting.cpp` тем самым перестал зажигать шпили в ночи.
    // Разбор и число — в отчёте шага; снос колонки трогает ui/ и свет, то
    // есть ДРУГУЮ поверхность, и в этот наряд не входит.
    //
    // ЖИВАЯ ПОЛОВИНА ЗАКОНА ОСТАЁТСЯ, И ОНА ПРО ГОРОД: пустая паства НЕ
    // значит «выкачан», и у города это по-прежнему судится визитором.
    int cityViewDepleted = 0, cityViewSeen = 0;
    for_each_place_view(mw, [&](const PlaceView& lv) {
        if (lv.type != SquadType::City) return;
        ++cityViewSeen;
        if (lv.depleted) ++cityViewDepleted;
    });
    CHECK(cityViewSeen == 1, "визитор выдал город фикстуры — цикл померил");
    CHECK(cityViewDepleted == 0,
          "город с пустой паствой у визитора НЕ выкачан: выкачанность есть "
          "вопрос о ШПИЛЕ, и у не-шпиля она ЛОЖЬ всегда");
}

// ── M-39 «ЧЬЯ ЭТО РУИНА» — ВОПРОС СНЯТ ВМЕСТЕ С ВЛАДЕЛЬЦЕМ (M-320) ───────
//
// ЗДЕСЬ СТОЯЛ `test_place_faction_is_the_instance_only`, и он утверждал, что
// каждая рождённая руина и каждый шпиль несут индекс демонов СВОЕЙ колонкой
// экземпляра, а не колонкой вида. Утверждение снято НЕ ослаблением, а тем,
// что у него больше нет предмета: руина и шпиль — чистые фичи, экземпляра у
// них нет ни одного, значит и знамени нет — не `-1`, не freefolk, а нет как
// вопроса. Фича не бывает чья-то: она просто есть в мире (вердикт владельца
// 2026-10-26). Генератор руин перестал спрашивать `faction_index("demons")`
// вовсе — с ним ушла и единственная строка на этом пути (ЗАКОН СЛОВАРЯ).
//
// ЧТО ИЗ ЗАКОНА ОСТАЛОСЬ ЖИТЬ И ГДЕ ЕГО СУДЯТ, чтобы его не искали заново:
//   · «владельца называет ЭКЗЕМПЛЯР, не вид» — предмет жив у ГОРОДА и
//     ДЕРЕВНИ (их рождает `populate_places_from_politik@src/macro/state.cpp`
//     с `c.factionIdx`), и колонки вида `spawnFaction`, против которой M-39
//     был направлен, в дереве нет вовсе — её снёс сам M-39;
//   · «знамя не достаёт до дичи на земле» — `fauna_registry_test`, как и
//     было.

// ── СТЕНА: ВТОРОГО ВЫВОДА О ОРБЕ В ДЕРЕВЕ НЕТ ────────────────────────────
//
// §0 п.2: утверждение об ОТСУТСТВИИ адреса в коде не имеет и указывает на
// СВОЙ ТЕСТ. Прецедент формы — `tests/world_fold_law_test.cpp`. Разница с ним
// одна и она в пользу стены: канал ПУСТ после M-233 п.8, поэтому послаблений
// нет вовсе — ни белого списка, ни тающего остатка.
//
// ПРАВИЛО, И ПОЧЕМУ ИМЕННО ОНО. Вывод о орбе опознаётся по ПАРЕ: файл
// спрашивает `worked_read` И называет шпиль — `FT_Spire` (ключ по вердикту
// владельца 2026-10-07) ИЛИ `SquadType::Spire` (прежний ключ, чтобы возврат
// к нему тоже был виден). Пара и есть преступление — у worked-числа ДВА
// смысла (паства у поселения, спелл у шпиля), поэтому один `worked_read`
// невинен, одно имя шпиля невинно, а вместе они значат «я вывожу состояние
// орба сам». Комментарии и строковые литералы срезаются: иначе прибор ловил
// бы собственные объяснения — и это ЗАМЕРЕНО, а не предположено: без стрижки
// правило ловит `labour.h`, `squad.h` и `state.h`, которые называют `FT_Spire`
// РОВНО в объяснении двух смыслов числа. Стрижка может прибор только
// ОСЛЕПИТЬ, не обмануть.
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
            if (code.find("FT_Spire") != std::string::npos
                || code.find("SquadType::Spire") != std::string::npos)
                namesSpire = true;
        }
        if (readsWorked && namesSpire) offenders.push_back(rel);
    }

    CHECK(scanned > 100,
          "стена прочла всё дерево, а не три файла — иначе ноль нарушений "
          "означает «не смотрел», а не «чисто»");
    for (const std::string& f : offenders) {
        std::printf("  ВТОРОЙ ВЫВОД О ОРБЕ: %s спрашивает worked_read и "
                    "называет шпиль (FT_Spire / SquadType::Spire)\n",
                    f.c_str());
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
    test_genesis_places_ruins_and_spires_as_features();
    test_one_answer_to_the_drained_orb();
    test_no_second_orb_derivation_in_the_tree();
    return sm::test::report("spire_generation_test");
}
