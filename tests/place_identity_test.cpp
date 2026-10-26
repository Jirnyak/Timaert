// ЛИЧНОСТЬ МЕСТА ЕДЕТ ВНИЗ ИЗ ФИЧИ — СВИДЕТЕЛЬ ЗАКОНА (M-318 часть Б).
//
// ОХРАНЯЕМЫЙ ЗАКОН — вердикт владельца 2026-10-26, дословно: «лучше сами фичи
// агностичны (у них честно есть ген модуль для субмира) и тип и поле на этом
// всё». Отсюда: на вопрос «что это за место» субмиру отвечает БАЙТ ФИЧИ
// клетки, а не колонка сквада, стоящего на ней (ЗАКОН ДВУХ МИРОВ п.4 —
// данжи и субмир не читают модули сквадов; DOD п.6 — один вопрос, один
// носитель).
//
// ЗАЧЕМ ОТДЕЛЬНЫЙ ПРИБОР, ХОТЯ СЮИТА БЫЛА ЗЕЛЁНОЙ. Потому что она была
// зелёной ЛОЖНО, и это ЗАМЕРЕНО: подстановка «род места ВСЕГДА None» в
// `cell_facts@src/macro/cell_facts.cpp` оставила `check` **137/137**
// зелёным — ни один ctest личность места не охранял вовсе. Покраснели только
// два смоука из 63 (`enter_first_settlement,city_gate_probe` и
// `…,city_day_pump`), то есть закон держался на запуске ИГРЫ и падал бы
// молча у всякого, кто гоняет сюиту. Этот файл закрывает ровно ту дыру.
//
// ПОЧЕМУ УТВЕРЖДЕНИЕ ИМЕННО О РАСХОЖДЕНИИ, А НЕ О СОВПАДЕНИИ. В живом мире
// два носителя СОГЛАСНЫ — замерено дважды: перепись дня 0 на трёх сидах дала
// 0 расхождений, зонд по дням на 256 днях — 623 171 осмотр и 0 расхождений.
// Поэтому свидетель, построенный на согласном мире, был бы ТАВТОЛОГИЕЙ: он
// не отличил бы «род из фичи» от «род из колонки сквада» ни в какую сторону
// (§8 п.6, шестая форма слепоты — тождество двух вычислений, слепое к самому
// вычислению). Прибор РОЖАЕТ СВОЁ ПРЕДУСЛОВИЕ (§8 п.11): он ставит мир, в
// котором два ответа РАЗНЫЕ, и спрашивает, чей победил.

#include "check.h"

#include "core/torus.h"
#include "ecs/components.h"
#include "macro/cell_facts.h"
#include "macro/features.h"
#include "macro/place_registry.h"
#include "macro/squad_index.h"
#include "macro/state.h"
#include "macro/store.h"
#include "tables/feature_type.h"
#include "tables/squad_type.h"

#include <cstdint>

namespace {

constexpr int kSide = 128;   // степень двойки — ЗАКОН АДРЕСА

// Суша с различимыми клетками: без рукотворного терраина `cell_facts` честно
// отвечает «открытая вода» ОДНИМ дефолтом, и прибор судил бы дефолт.
sm::TerrainData make_land() {
    sm::TerrainData td{};
    td.width = kSide;
    td.height = kSide;
    td.seaLevel16 = 8192;
    td.rgba.assign(std::size_t(kSide) * kSide * 4u, 0);
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const std::size_t i = (std::size_t(y) * kSide + x) * 4u;
            td.rgba[i + 0] = std::uint16_t(30000 + 7 * x + 3 * y);
            td.rgba[i + 1] = std::uint16_t(20000 + 5 * x);
            td.rgba[i + 2] = std::uint16_t(30000 + 11 * y);
            td.rgba[i + 3] = 0xFFFFu;   // маска суши
        }
    }
    return td;
}

sm::MacroHandle birth_place_at(sm::MacroStore& st, int x, int y,
                               sm::SquadType kind) {
    const sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = sm::ecs::MacroCell{sm::ecs::cell_index(x, y, kSide)};
    st.runtime[h.slot].squadType = std::uint8_t(kind);
    return h;
}

} // namespace

int main() {
    sm::GameState gs{};
    gs.mapW = kSide;
    gs.mapH = kSide;
    gs.worldSeed = 4242u;
    auto storeOwn = sm::make_macro_store();
    sm::MacroStore& st = *storeOwn;
    sm::TerrainData terrain = make_land();
    sm::FeatureLayer features;
    features.resize(kSide, kSide);
    sm::SquadIndex idx{};

    sm::MacroWorld mw{};
    mw.gs = &gs;
    mw.store = &st;
    mw.terrain = &terrain;
    mw.features = &features;
    mw.squads = &idx;

    // ── ПРЕДУСЛОВИЕ, КОТОРОГО В ЖИВОМ МИРЕ НЕТ, И ПОТОМУ ОНО СТРОИТСЯ ЗДЕСЬ ─
    // Клетка A: фича говорит ГОРОД, колонка сквада говорит ДЕРЕВНЯ.
    // Клетка B: зеркально — фича ДЕРЕВНЯ, колонка ГОРОД.
    // Два ответа разведены НАРОЧНО, чтобы вопрос «чей победил» имел смысл.
    constexpr int kAx = 40, kAy = 40, kBx = 70, kBy = 70;
    sm::feature_put(features, terrain, kAx, kAy, sm::FT_City);
    sm::feature_put(features, terrain, kBx, kBy, sm::FT_Village);
    birth_place_at(st, kAx, kAy, sm::SquadType::Village);
    birth_place_at(st, kBx, kBy, sm::SquadType::City);
    sm::build_squad_index(idx, st, gs.mapW, gs.mapH);

    const sm::CellFacts a = sm::cell_facts(mw, kAx, kAy);
    const sm::CellFacts b = sm::cell_facts(mw, kBx, kBy);

    // Предусловие состоялось: ветка `.place` вообще отработала. Без этого
    // «род равен фиче» было бы зелено на двух нулях (§8 п.2).
    CHECK(a.place.id != 0 && b.place.id != 0,
          "предусловие: сборщик нашёл неподвижный сквад на обеих клетках");
    CHECK(a.feature == sm::FT_City && b.feature == sm::FT_Village,
          "предусловие: байты фич легли так, как их положил свидетель");

    // ГЛАВНОЕ УТВЕРЖДЕНИЕ — РОД РАВЕН ТОМУ, ЧТО СКАЗАЛА ФИЧА.
    CHECK(a.place.type == sm::SquadType::City,
          "род места берётся из БАЙТА ФИЧИ: фича говорит город — вниз едет "
          "город, даже когда колонка сквада говорит деревню");
    CHECK(b.place.type == sm::SquadType::Village,
          "и зеркально: фича говорит деревню — вниз едет деревня");

    // И ОТРИЦАНИЕ, НАЗВАННОЕ ЯВНО: колонка сквада на этот вопрос НЕ отвечает.
    // Формулируется через ту же пару, чтобы мутация «вернуть колонку сквада»
    // роняла прибор ОБОИМИ плечами, а не одним.
    CHECK(a.place.type != sm::SquadType(st.runtime[0].squadType)
              || b.place.type != sm::SquadType(st.runtime[1].squadType),
          "колонка сквада на вопрос о роде места НЕ отвечает — иначе субмир "
          "читал бы макро-ответ О СКВАДЕ (ЗАКОН ДВУХ МИРОВ п.4)");

    // ДВЕРЬ И СБОРЩИК ОБЯЗАНЫ СОГЛАШАТЬСЯ: `cell_facts` не вправе иметь
    // СВОЁ мнение о том, чья это фича, — иначе у вопроса снова два носителя.
    CHECK(a.place.type == sm::place_kind_of_feature(a.feature)
              && b.place.type == sm::place_kind_of_feature(b.feature),
          "сборщик отвечает РОВНО то же, что обратная дверь реестра");

    return sm::test::report("place_identity_test");
}
