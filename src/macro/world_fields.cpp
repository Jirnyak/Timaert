#include "macro/world_fields.h"

#include "core/caps.h"
#include "core/table_guard.h"
#include "macro/deposit_layer.h"
#include "macro/features.h"
#include "macro/knowledge.h"
#include "macro/resource_field.h"
#include "macro/state.h"

#include <algorithm>
#include <utility>

namespace sm {
namespace {

// One cap serves every whole-map block (a 1024² world) and every sparse
// block (one entry per mutated cell, which the same map bounds) — anything
// beyond it is a corrupt count, fail closed. These moved here with the rows
// they guard (save.cpp owned them until 2026-08-24).
constexpr std::uint32_t kMaxFieldCells = std::uint32_t(kWorldCellCap);
static_assert(kMaxFieldCells == (1u << 20),
              "кап блока поля на проводе есть кап клеток мира; сдвиг капа "
              "двигает потолок payload — пересчитать kWorldFieldsMaxBytes");

// Каждый блок открывается СЧЁТОМ (savefmt::Writer::count — u32), поэтому он
// входит в вес ряда наравне с его клетками. Вес ряда — слагаемое потолка
// payload (world_fields.h kWorldFieldsMaxBytes): он считается по ПЛОТНОМУ
// капу и у разреженных рядов тоже, потому что сторож обязан мерить худший
// ЛЕГАЛЬНЫЙ мир, а не типичный.
constexpr std::uint64_t kBlockCountBytes = sizeof(std::uint32_t);

// ── Trees: the dense u16 carrier, whole (since v36) ─────────────────────
// Лес лежит в состоянии мира (`GameState::treeLayer`, ломоть 3 сейв-эпика):
// ряд берёт его клетки там же, где остальные ряды берут свои. Байты блока те
// же, что были, когда клетки леса приезжали сюда отдельным параметром провода.
void trees_write(savefmt::Writer& w, const GameState& gs) {
    // Клетки МИРА, не кап: неродившийся слой отдаёт пустой вид и счёт 0.
    const std::span<const std::uint16_t> cells = gs.treeLayer.cells();
    if (w.count(cells.size(), kMaxFieldCells)) {
        // ONE insert of the whole contiguous u16 run — byte-for-byte the
        // stream the old per-element pod() loop produced (pod() is a plain
        // memcpy of the value), minus a million vector inserts per save.
        if (!w.ok) return;
        const auto* bytes =
            reinterpret_cast<const std::uint8_t*>(cells.data());
        w.bytes.insert(w.bytes.end(), bytes,
                       bytes + cells.size() * sizeof(std::uint16_t));
    }
}
// Форма знания: счёт 0 значит «слой не рождался» (частичное состояние
// фикстуры) — лес остаётся без сторон и отвечает нулём; ненулевой счёт обязан
// покрыть загруженный мир ровно. Клетка выше потолка — порча файла, а не
// лес: она срезается к потолку той же границей, что держит писатель клетки.
bool trees_read(savefmt::Reader& r, GameState& gs) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    if (n == 0) return true;
    const std::size_t expected = world_cells_within_cap(gs.mapW, gs.mapH);
    if (expected == 0u || std::size_t(n) != expected) {
        r.ok = false;
        return false;
    }
    tree_layer_reset(gs.treeLayer, gs.mapW, gs.mapH);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint16_t v = 0;
        r.pod(v);
        gs.treeLayer.data[i] =
            v > kMaxTreesPerCell ? std::uint16_t(kMaxTreesPerCell) : v;
    }
    return r.ok;
}
constexpr std::uint64_t kTreesMaxBytes =
    kBlockCountBytes + std::uint64_t(kMaxFieldCells) * sizeof(std::uint16_t);

// ── Knowledge: the explored map, whole (since v40) ──────────────────────
// Visible (2) is a session projection of where the player stands — it decays
// to Explored on write, and a load recomputes sight from the restored
// position. Zero cells means the layer was never built (a partial state some
// tests save): the world simply stays dark, because an absent grid answers
// Unknown (fail closed). A non-zero count must cover the loaded map exactly.
void knowledge_write(savefmt::Writer& w, const GameState& gs) {
    // Клетки МИРА, не кап: неродившийся слой отдаёт пустой вид и счёт 0.
    const std::span<const std::uint8_t> data = gs.knowledge.cells();
    if (w.count(data.size(), kMaxFieldCells)) {
        for (const std::uint8_t v : data)
            w.pod(std::uint8_t(v >= kKnowledgeExplored ? kKnowledgeExplored
                                                       : kKnowledgeUnknown));
    }
}
bool knowledge_read(savefmt::Reader& r, GameState& gs) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    if (n == 0) return true;
    GameState& s = gs;
    const std::size_t expected = world_cells_within_cap(s.mapW, s.mapH);
    if (expected == 0u || std::size_t(n) != expected) {
        r.ok = false;
        return false;
    }
    knowledge_reset(s.knowledge, s.mapW, s.mapH);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint8_t v = 0;
        r.pod(v);
        s.knowledge.data[i] = v >= kKnowledgeExplored ? kKnowledgeExplored
                                                      : kKnowledgeUnknown;
    }
    return r.ok;
}
constexpr std::uint64_t kKnowledgeMaxBytes =
    kBlockCountBytes + std::uint64_t(kMaxFieldCells) * sizeof(std::uint8_t);

// (Блоки жил УНИЧТОЖЕНЫ 2026-10-23, «жилы единые»: природное поле —
// чистая функция терраина и сида, его не хранят; залежи шахт едут рядом
// FeatureNumber числами фич.)
// Клетка жилы на проводе — пара {индекс, остаток}; эту же пару пишут шрамы и
// слой разработки, поэтому ширина названа один раз на три ряда.
constexpr std::uint64_t kSparseCellBytes =
    sizeof(std::uint32_t) + sizeof(std::int32_t);

// ── Scars: ONE block — the wheat row's FIELD, sparse on the wire ────────
// v96: the scars are a ResourceGrid over the world, so the stream is
// deterministic by construction (a field walks itself in index order) and
// the sort the hash needed died with the hash. Same wire shape as deposits.
// v157: блок ОДИН, а не по одному на ряд реестра. Четыре ряда-носителя
// (Fauna/Trees/Livestock/Vein) шрама не держат, и их блоки ехали вечно
// пустыми — по 4 Б счёта в каждом файле и по 8 388 612 Б в потолке.
void scars_write(savefmt::Writer& w, const GameState& gs) {
    const ResourceGrid& scars = gs.wheatScars;
    if (!w.count(std::size_t(scars.liveCells), kMaxFieldCells)) return;
    scars.for_each_live([&](std::uint32_t idx, std::int32_t scar) {
        w.pod(idx);
        w.pod(scar);
    });
}
// A FIELD needs a world to be a field over (the deposits' own lesson): the
// reader has already filled the dimensions, so a block sizes the grid it is
// about to fill — and only when the FILE carries cells for it.
bool size_for_world(const GameState& gs, ResourceGrid& g) {
    if (g.live()) return true;
    if (gs.mapW <= 0 || gs.mapH <= 0) return false;
    field_set_allocate(g, gs.mapW, gs.mapH);
    return g.live();
}

bool scars_read(savefmt::Reader& r, GameState& gs) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    ResourceGrid& scars = gs.wheatScars;
    if (n > 0 && !size_for_world(gs, scars)) { r.ok = false; return false; }
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint32_t idx = 0;
        std::int32_t scar = 0;
        r.pod(idx);
        r.pod(scar);
        if (r.ok && scars.live()) {
            field_set(scars, idx, scar);
        }
    }
    return r.ok;
}
constexpr std::uint64_t kScarsMaxBytes =               // один ряд-шрам
    kBlockCountBytes + std::uint64_t(kMaxFieldCells) * kSparseCellBytes;

// ── FeatureNumber: THE one layer of S5, sparse on the wire (v96) ────────
// The number under a feature — hulls moored at a harbour today. One block,
// because there is one layer: «1 шахта в клетке — одно поле в клетке».
void feature_number_write_block(savefmt::Writer& w, const GameState& gs) {
    const ResourceGrid& g = gs.featureNumber;
    if (!w.count(std::size_t(g.liveCells), kMaxFieldCells)) return;
    g.for_each_live([&](std::uint32_t idx, std::int32_t value) {
        w.pod(idx);
        w.pod(value);
    });
}
bool feature_number_read_block(savefmt::Reader& r, GameState& gs) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    ResourceGrid& g = gs.featureNumber;
    if (n > 0 && !size_for_world(gs, g)) { r.ok = false; return false; }
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint32_t idx = 0;
        std::int32_t value = 0;
        r.pod(idx);
        r.pod(value);
        if (r.ok && g.live()) field_set(g, idx, value);
    }
    return r.ok;
}
constexpr std::uint64_t kFeatureNumberMaxBytes =
    kBlockCountBytes + std::uint64_t(kMaxFieldCells) * kSparseCellBytes;

// ── FaunaField: живое поголовье дикой природы (M-371, v149) ─────────────
// Носитель ряда Fauna — та же разреженная пара {индекс, значение}, что у
// поля чисел фич: данные генератора рождения, из ничего не выводятся.
void fauna_field_write_block(savefmt::Writer& w, const GameState& gs) {
    const ResourceGrid& g = gs.faunaField;
    if (!w.count(std::size_t(g.liveCells), kMaxFieldCells)) return;
    g.for_each_live([&](std::uint32_t idx, std::int32_t value) {
        w.pod(idx);
        w.pod(value);
    });
}
bool fauna_field_read_block(savefmt::Reader& r, GameState& gs) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    ResourceGrid& g = gs.faunaField;
    if (n > 0 && !size_for_world(gs, g)) { r.ok = false; return false; }
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint32_t idx = 0;
        std::int32_t value = 0;
        r.pod(idx);
        r.pod(value);
        if (r.ok && g.live()) field_set(g, idx, value);
    }
    return r.ok;
}
constexpr std::uint64_t kFaunaFieldMaxBytes =
    kBlockCountBytes + std::uint64_t(kMaxFieldCells) * kSparseCellBytes;

// (Ряд `Built` и пара `built_write`/`built_read` УНИЧТОЖЕНЫ в v130 вместе с
// вектором `builtFeatures`: писателей из геймплея у него было НОЛЬ, то есть
// ряд возил в файл свой же результат чтения — см. некролог в `world_fields.h`.)

struct WorldFieldRow {
    WorldField id;
    const char* name;
    void (*write)(savefmt::Writer&, const GameState&);
    bool (*read)(savefmt::Reader&, GameState&);
    // Худший ЛЕГАЛЬНЫЙ вес блока — колонка стоит здесь, рядом со своим
    // писателем, а не в save.cpp: кап ряда знает только ряд.
    std::uint64_t maxBytes;
};

constexpr WorldFieldRow kWorldFields[std::size_t(WorldField::Count)] = {
    {WorldField::Trees,     "trees",     trees_write,     trees_read,
     kTreesMaxBytes},
    {WorldField::Knowledge, "knowledge", knowledge_write, knowledge_read,
     kKnowledgeMaxBytes},
    {WorldField::Scars,     "scars",     scars_write,     scars_read,
     kScarsMaxBytes},
    {WorldField::FeatureNumber, "feature_number",
     feature_number_write_block, feature_number_read_block,
     kFeatureNumberMaxBytes},
    {WorldField::FaunaField, "fauna_field",
     fauna_field_write_block, fauna_field_read_block,
     kFaunaFieldMaxBytes},
};
static_assert(rows_in_enum_order(kWorldFields, &WorldFieldRow::id),
              "every WorldField needs its row — the table IS the system");

// СТЕНА МЕЖДУ ТАБЛИЦЕЙ И ЧИСЛОМ В ЗАГОЛОВКЕ. Новый ряд полей или другая
// ширина его записи двигают сумму — и красят сборку ЗДЕСЬ, требуя поправить
// kWorldFieldsMaxBytes вслух. Молча разойтись проза и правда не могут
// (AGENTS §8 п.6: лучшая форма свидетеля — нарушение, которое нельзя
// собрать).
constexpr std::uint64_t world_fields_max_bytes_sum() {
    std::uint64_t sum = 0;
    for (const WorldFieldRow& row : kWorldFields) sum += row.maxBytes;
    return sum;
}
static_assert(world_fields_max_bytes_sum() == kWorldFieldsMaxBytes,
              "потолок блока полей разошёлся со столбцом maxBytes таблицы "
              "kWorldFields — поправить число в world_fields.h");

} // namespace

void write_world_fields(savefmt::Writer& w, const GameState& gs) {
    for (const WorldFieldRow& row : kWorldFields) row.write(w, gs);
}

bool read_world_fields(savefmt::Reader& r, GameState& gs) {
    for (const WorldFieldRow& row : kWorldFields) {
        if (!row.read(r, gs)) return false;
    }
    return r.ok;
}

} // namespace sm
