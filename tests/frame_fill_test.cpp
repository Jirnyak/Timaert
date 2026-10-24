// ЛОКАЛЬНЫЙ ФРЕЙМ — СВИДЕТЕЛЬ СБОРЩИКА (M-171, ломоть 3).
//
// Закон под охраной: fill_local_frame выкладывает пять родов окна 5×5 ТЕМИ ЖЕ
// дверями, какими мир отвечает на эти вопросы порознь, — строка клетки равна
// ответу cell_facts, запас ряда — resource_field_read, сквады клеток фрейма
// собраны адресами (и ни одного чужого), окно фактов несёт факт клетки окна и
// НЕ несёт факт дальней клетки, мировые переменные — скаляры gs.
//
// Фикстура — ЧАСТИЧНЫЙ конверт (gs + store, слои null): cell_facts обязан
// отвечать нулевыми вкладами отсутствующих слоёв (его собственный контракт),
// и паритет строк это наследует. Свидетель рожает себе предусловия САМ
// (§8 п.11): сквады ставятся на клетки руками, факты подшиваются дверью.
#include "check.h"

#include "bus/local_frame.h"
#include "core/torus.h"
#include "macro/cell_facts.h"
#include "macro/chronicle.h"
#include "macro/frame_fill.h"
#include "macro/macro_world.h"
#include "macro/map_generator.h"
#include "macro/resource_field.h"
#include "macro/squad_index.h"
#include "macro/state.h"
#include "macro/store.h"

#include <cstring>

namespace {

// Равенство строк клетки — ПО ПОЛЯМ: у CellFacts есть паддинг, и memcmp двух
// временных объектов сравнивал бы мусор стека, а не ответ мира.
bool same_row(const sm::CellFacts& a, const sm::CellFacts& b) {
    return a.x == b.x && a.y == b.y && a.biome == b.biome
        && a.feature == b.feature && a.place.type == b.place.type
        && a.place.id == b.place.id && a.place.size == b.place.size
        && a.place.tier == b.place.tier
        && a.place.spell == b.place.spell
        && a.place.factionIdx == b.place.factionIdx
        && a.place.depleted == b.place.depleted
        && a.treeCount == b.treeCount && a.height01 == b.height01
        && a.fertility01 == b.fertility01
        && a.temperature01 == b.temperature01
        && a.seasonTempOffset == b.seasonTempOffset && a.zone == b.zone
        && a.depositsNear == b.depositsNear
        && a.cropHarvested == b.cropHarvested && a.water == b.water;
}

sm::MacroHandle birth_at(sm::MacroStore& st, int x, int y, int side) {
    const sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = sm::ecs::MacroCell{sm::ecs::cell_index(x, y, side)};
    st.runtime[h.slot].squadType = std::uint8_t(sm::SquadType::Artel);
    return h;
}

} // namespace

int main() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    gs.worldSeed = 777u;
    auto storeOwn = sm::make_macro_store();
    sm::MacroStore& st = *storeOwn;
    // РУКОТВОРНЫЙ ТЕРРАИН — обязательное предусловие свидетеля (§8 п.11): без
    // него cell_facts честно отвечает «открытая вода» ОДИНАКОВЫМ дефолтом, и
    // паритет строк был бы тавтологией «дефолт == дефолт», слепой к порядку
    // клеток (мутация-транспозиция прошла зелёной — поймано вживую).
    sm::TerrainData terrain{};
    terrain.width = 128;
    terrain.height = 128;
    terrain.seaLevel16 = 8192;
    terrain.rgba.assign(std::size_t(128) * 128 * 4, 0);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const std::size_t idx = (std::size_t(y) * 128 + x) * 4;
            // Градиент по обеим осям: каждая клетка мира РАЗЛИЧИМА.
            terrain.rgba[idx + 0] = std::uint16_t(200 * x + 90 * y);
            terrain.rgba[idx + 1] = std::uint16_t(60 * x + 300 * y);
            terrain.rgba[idx + 2] = std::uint16_t(30000 + 11 * x);
            terrain.rgba[idx + 3] = std::uint16_t((x + y) % 7 == 0
                                                      ? 0 : 0xFFFFu);
        }
    }
    sm::MacroWorld mw{};
    mw.gs = &gs;
    mw.store = &st;
    mw.terrain = &terrain;
    // КАРКАС ОБЯЗАТЕЛЕН (шрам ломтя 6): без mw.squads сборщик честно падал
    // на свип-фолбэк, и бакетный путь — вместе с его арифметикой кольца —
    // не исполнялся ни разу; мутация трункационного деления прошла зелёной.
    sm::SquadIndex idx{};
    mw.squads = &idx;

    const int ccx = 64, ccy = 64;
    const std::uint32_t center = sm::cell_of(ccx, ccy, gs.mapW);

    // Предусловия: сквад на углу окна (−2,−2), сквад ЗА окном (+3,0),
    // факт на клетке окна (+2,+1), факт за окном (+5,+5).
    const sm::MacroHandle inSquad = birth_at(st, ccx - 2, ccy - 2, gs.mapW);
    const sm::MacroHandle outSquad = birth_at(st, ccx + 3, ccy, gs.mapW);
    // Мёртвая запись НА клетке окна: фрейм адресует живых (§8 п.11 — труп
    // свидетель рожает сам, а не надеется на мир).
    const sm::MacroHandle deadSquad = birth_at(st, ccx + 1, ccy, gs.mapW);
    st.dead[deadSquad.slot] = 1;
    sm::build_squad_index(idx, st, gs.mapW, gs.mapH);
    sm::chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    sm::WorldFact inFact{};
    inFact.day = gs.worldTime.day();
    inFact.kind = std::uint16_t(sm::FactKind::Explored);
    inFact.x = std::int16_t(ccx + 2);
    inFact.y = std::int16_t(ccy + 1);
    inFact.amount = 7;
    sm::chronicle_record(gs.chronicle, inFact);
    sm::WorldFact outFact = inFact;
    outFact.x = std::int16_t(ccx + 5);
    outFact.y = std::int16_t(ccy + 5);
    sm::chronicle_record(gs.chronicle, outFact);
    sm::lease_window_at(gs, center);

    sm::LocalContext frame{};
    sm::fill_local_frame(mw, center, frame);

    // ── Род 4: скаляры мира ────────────────────────────────────────────────
    CHECK(frame.worldSeed == 777u && frame.side == 128
              && frame.centerCell == center
              && frame.leasedWindow == center,
          "world scalars ride kind 4 verbatim");

    // ── Род 1: строка клетки == ответ cell_facts, все 25 ──────────────────
    int rowsEqual = 0;
    const int half = sm::kFrameSpan / 2;
    int i = 0;
    for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx, ++i) {
            const sm::CellFacts want =
                sm::cell_facts(mw, ccx + dx, ccy + dy);
            if (same_row(frame.cells[std::size_t(i)], want)) ++rowsEqual;
        }
    }
    CHECK(rowsEqual == sm::kFrameCells,
          "every kind-1 row equals the world's own cell_facts answer");

    // ── Род 3: запас ряда == resource_field_read ──────────────────────────
    int stockEqual = 0;
    i = 0;
    for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx, ++i) {
            for (int f = 0; f < sm::kFrameFieldRows; ++f) {
                if (frame.stock[std::size_t(f) * sm::kFrameCells
                                + std::size_t(i)]
                    == sm::resource_field_read(mw, sm::ResourceFieldId(f),
                                               ccx + dx, ccy + dy))
                    ++stockEqual;
            }
        }
    }
    CHECK(stockEqual == sm::kFrameCells * sm::kFrameFieldRows,
          "every kind-3 stock number equals the field door's answer");

    // ── Род 2: сквад окна собран адресом, чужого нет ───────────────────────
    bool sawIn = false, sawOut = false;
    for (std::uint32_t s = 0; s < frame.squadCount; ++s) {
        if (frame.squads[s] == inSquad) sawIn = true;
        if (frame.squads[s] == outSquad) sawOut = true;
    }
    CHECK(sawIn, "a squad standing on a frame cell rides kind 2 by handle");
    CHECK(!sawOut, "a squad outside the 5x5 window does not");
    bool sawDead = false;
    for (std::uint32_t s = 0; s < frame.squadCount; ++s)
        if (frame.squads[s] == deadSquad) sawDead = true;
    CHECK(!sawDead, "a dead record on a frame cell is not addressed");

    // ── Род 5: факт окна есть, дальний — нет ──────────────────────────────
    bool factIn = false, factOut = false;
    for (std::uint32_t s = 0; s < frame.factCount; ++s) {
        if (frame.facts[s].x == inFact.x && frame.facts[s].y == inFact.y)
            factIn = true;
        if (frame.facts[s].x == outFact.x && frame.facts[s].y == outFact.y)
            factOut = true;
    }
    CHECK(factIn, "a fact filed on a frame cell rides kind 5");
    CHECK(!factOut, "a fact three index-cells away does not leak in");

    // ── Шов тора: окно у края мира собирается той же дверью ───────────────
    sm::LocalContext edge{};
    sm::fill_local_frame(mw, sm::cell_of(0, 0, gs.mapW), edge);
    const sm::CellFacts wrapWant = sm::cell_facts(mw, -2, -2);
    CHECK(same_row(edge.cells[0], wrapWant),
          "the frame's corner row wraps the torus like the world does");
    // ── …И СКВАДЫ ТОЖЕ (шрам ломтя 6): сквад за швом, в охвате центра (0,0)
    // через заворот. Рукописное кольцо бакетов делило отрицательный край
    // ТРУНКАЦИЕЙ ((−2)/8 == 0) и теряло завёрнутое кольцо — слот у
    // северо-западного шва не попадал в род 2 фрейма; floor-деление двери
    // squads_in_frame чинит, этот чек держит.
    const sm::MacroHandle seamSquad =
        birth_at(st, gs.mapW - 2, gs.mapH - 2, gs.mapW);
    sm::build_squad_index(idx, st, gs.mapW, gs.mapH);   // каркас выводимый
    sm::fill_local_frame(mw, sm::cell_of(0, 0, gs.mapW), edge);
    bool sawSeam = false;
    for (std::uint32_t i = 0; i < edge.squadCount; ++i) {
        if (edge.squads[i] == seamSquad) sawSeam = true;
    }
    CHECK(sawSeam,
          "a squad across the torus seam inside the 5x5 rides kind 2");

    return sm::test::report("frame_fill_test");
}
