#include "macro/world_fields.h"

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
constexpr std::uint32_t kMaxFieldCells = 1u << 20;

// ── Trees: the dense u16 carrier, whole (since v36) ─────────────────────
void trees_write(savefmt::Writer& w, const WorldFieldStores& st) {
    if (!st.treeCounts) { w.count(0, kMaxFieldCells); return; }
    if (w.count(st.treeCounts->size(), kMaxFieldCells)) {
        for (const std::uint16_t c : *st.treeCounts) w.pod(c);
    }
}
bool trees_read(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    if (!st.treeCounts) { r.ok = false; return false; }
    st.treeCounts->clear();
    st.treeCounts->resize(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) r.pod((*st.treeCounts)[i]);
    return r.ok;
}

// ── Knowledge: the explored map, whole (since v40) ──────────────────────
// Visible (2) is a session projection of where the player stands — it decays
// to Explored on write, and a load recomputes sight from the restored
// position. Zero cells means the layer was never built (a partial state some
// tests save): the world simply stays dark, because an absent grid answers
// Unknown (fail closed). A non-zero count must cover the loaded map exactly.
void knowledge_write(savefmt::Writer& w, const WorldFieldStores& st) {
    if (!st.gs) { w.count(0, kMaxFieldCells); return; }
    const auto& data = st.gs->knowledge.data;
    if (w.count(data.size(), kMaxFieldCells)) {
        for (const std::uint8_t v : data)
            w.pod(std::uint8_t(v >= kKnowledgeExplored ? kKnowledgeExplored
                                                       : kKnowledgeUnknown));
    }
}
bool knowledge_read(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    if (n == 0) return true;
    if (!st.gs) { r.ok = false; return false; }
    GameState& s = *st.gs;
    std::size_t expected = 0;
    if (!FeatureLayer::cell_count_for(s.mapW, s.mapH, expected)
        || std::size_t(n) != expected) {
        r.ok = false;
        return false;
    }
    s.knowledge.width = s.mapW;
    s.knowledge.height = s.mapH;
    s.knowledge.data.assign(expected, kKnowledgeUnknown);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint8_t v = 0;
        r.pod(v);
        s.knowledge.data[i] = v >= kKnowledgeExplored ? kKnowledgeExplored
                                                      : kKnowledgeUnknown;
    }
    ++s.knowledge.revision;
    return r.ok;
}

// ── Deposits: sparse carrier cells, one block per kind (since v37) ──────
// Sorted by cell index: the map's iteration order is unspecified and the
// payload is checksummed, so the byte stream must be deterministic.
void deposits_write(savefmt::Writer& w, const WorldFieldStores& st) {
    for (std::size_t k = 0; k < std::size_t(kDepositKindCount); ++k) {
        if (!st.deposits) {
            w.pod(std::uint32_t(0));   // v55: the annihilation counter
            w.count(0, kMaxFieldCells);
            continue;
        }
        // v55: the scarcity baseline the dry cells used to carry implicitly.
        w.pod(st.deposits->drainedCells[k]);
        const auto& cellsOfKind = st.deposits->cells[k];
        if (!w.count(cellsOfKind.size(), kMaxFieldCells)) continue;
        std::vector<std::pair<std::uint32_t, std::int32_t>> cells(
            cellsOfKind.begin(), cellsOfKind.end());
        std::sort(cells.begin(), cells.end());
        for (const auto& [idx, remaining] : cells) {
            w.pod(idx);
            w.pod(remaining);
        }
    }
}
bool deposits_read(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    for (std::size_t k = 0; k < std::size_t(kDepositKindCount); ++k) {
        std::uint32_t drained = 0;   // v55
        r.pod(drained);
        std::uint32_t n = 0;
        if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
        if (!st.deposits) { r.ok = false; return false; }
        st.deposits->drainedCells[k] = drained;
        auto& cellsOfKind = st.deposits->cells[k];
        cellsOfKind.clear();
        cellsOfKind.reserve(n);
        for (std::uint32_t i = 0; i < n && r.ok; ++i) {
            std::uint32_t idx = 0;
            std::int32_t remaining = 0;
            r.pod(idx);
            r.pod(remaining);
            if (r.ok) cellsOfKind[idx] = remaining;
        }
    }
    return r.ok;
}

// ── Scars: one generic sparse block per resource row (since v35) ────────
void scars_write(savefmt::Writer& w, const WorldFieldStores& st) {
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        if (!st.gs) { w.count(0, kMaxFieldCells); continue; }
        const auto& scars = st.gs->resourceScars[f];
        if (!w.count(scars.size(), kMaxFieldCells)) continue;
        std::vector<std::pair<std::uint32_t, std::uint16_t>> cells(
            scars.begin(), scars.end());
        std::sort(cells.begin(), cells.end());
        for (const auto& [idx, scar] : cells) {
            w.pod(idx);
            w.pod(scar);
        }
    }
}
bool scars_read(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        std::uint32_t n = 0;
        if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
        if (!st.gs) { r.ok = false; return false; }
        auto& scars = st.gs->resourceScars[f];
        scars.clear();
        scars.reserve(n);
        for (std::uint32_t i = 0; i < n && r.ok; ++i) {
            std::uint32_t idx = 0;
            std::uint16_t scar = 0;
            r.pod(idx);
            r.pod(scar);
            if (r.ok) scars[idx] = scar;
        }
    }
    return r.ok;
}

struct WorldFieldRow {
    WorldField id;
    const char* name;
    void (*write)(savefmt::Writer&, const WorldFieldStores&);
    bool (*read)(savefmt::Reader&, const WorldFieldStoresMut&);
};

constexpr WorldFieldRow kWorldFields[std::size_t(WorldField::Count)] = {
    {WorldField::Trees,     "trees",     trees_write,     trees_read},
    {WorldField::Knowledge, "knowledge", knowledge_write, knowledge_read},
    {WorldField::Deposits,  "deposits",  deposits_write,  deposits_read},
    {WorldField::Scars,     "scars",     scars_write,     scars_read},
};
static_assert(rows_in_enum_order(kWorldFields, &WorldFieldRow::id),
              "every WorldField needs its row — the table IS the system");

} // namespace

void write_world_fields(savefmt::Writer& w, const WorldFieldStores& st) {
    for (const WorldFieldRow& row : kWorldFields) row.write(w, st);
}

bool read_world_fields(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    for (const WorldFieldRow& row : kWorldFields) {
        if (!row.read(r, st)) return false;
    }
    return r.ok;
}

const char* world_field_id(WorldField f) {
    const auto i = std::size_t(f);
    return i < std::size_t(WorldField::Count) ? kWorldFields[i].name : "?";
}

} // namespace sm
