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
        // ONE insert of the whole contiguous u16 run — byte-for-byte the
        // stream the old per-element pod() loop produced (pod() is a plain
        // memcpy of the value), minus a million vector inserts per save.
        if (!w.ok) return;
        const auto* bytes =
            reinterpret_cast<const std::uint8_t*>(st.treeCounts->data());
        w.bytes.insert(w.bytes.end(), bytes,
                       bytes + st.treeCounts->size() * sizeof(std::uint16_t));
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
    // Only the LIVE cells ride (v56): the scarcity baseline (virginUnits) is
    // derived from terrain + seed when the load path rebuilds the layer, so
    // it needs no bytes here — and the v55 stored counter died with that.
    for (std::size_t k = 0; k < std::size_t(kDepositKindCount); ++k) {
        if (!st.deposits) { w.count(0, kMaxFieldCells); continue; }
        // SPARSE ON THE WIRE, dense in the world: the file carries the live
        // cells because that is a FILE FORMAT decision, and the live layer is
        // a field because that is a MODEL decision (CANON S5, amended
        // 2026-09-16). Writing the field out sparsely costs one walk and
        // spares the save a megabyte of zeroes per kind.
        const ResourceGrid& g = st.deposits->cells[k];
        if (!w.count(std::size_t(g.liveCells), kMaxFieldCells)) continue;
        // A field walks itself in index order, so the stream is deterministic
        // by construction — the sort that stood here was needed only because a
        // hash iterates in an unspecified order and the payload is checksummed.
        g.for_each_live([&](std::uint32_t idx, std::int32_t remaining) {
            w.pod(idx);
            w.pod(remaining);
        });
    }
}
bool deposits_read(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    // A FIELD needs a world to be a field over, and the wire carries only cell
    // indices — so the staging layer is sized from the world state the reader
    // has already filled. (The hash this replaced needed no dimensions, which
    // is exactly the property that let it pretend the world was a bag of
    // keys.) Sizing here rather than at the call site keeps "a field is always
    // a field over THIS world" true for every reader there will ever be.
    if (st.deposits && st.gs && st.gs->mapW > 0 && st.gs->mapH > 0
        && !st.deposits->cells[0].live()) {
        allocate_deposit_fields(*st.deposits, st.gs->mapW, st.gs->mapH);
    }
    for (std::size_t k = 0; k < std::size_t(kDepositKindCount); ++k) {
        std::uint32_t n = 0;
        if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
        if (!st.deposits) { r.ok = false; return false; }
        ResourceGrid& g = st.deposits->cells[k];
        for (std::uint32_t i = 0; i < n && r.ok; ++i) {
            std::uint32_t idx = 0;
            std::int32_t remaining = 0;
            r.pod(idx);
            r.pod(remaining);
            // The load store is a staging layer the boot overlays through
            // restore_deposit_cells, and its grid may not be sized yet — a
            // file's own index is the only width it can be trusted about, so
            // the write goes through the grid's coordinates once it has some.
            if (r.ok && g.live()) g.write(g.x_of(idx), g.y_of(idx), remaining);
        }
    }
    return r.ok;
}

// ── Scars: one block per resource row — a FIELD, sparse on the wire ─────
// v96: the row's scars are a ResourceGrid over the world, so the stream is
// deterministic by construction (a field walks itself in index order) and
// the sort the hash needed died with the hash. Same wire shape as deposits.
void scars_write(savefmt::Writer& w, const WorldFieldStores& st) {
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        if (!st.gs) { w.count(0, kMaxFieldCells); continue; }
        const ResourceGrid& scars = st.gs->resourceScarCells[f];
        if (!w.count(std::size_t(scars.liveCells), kMaxFieldCells)) continue;
        scars.for_each_live([&](std::uint32_t idx, std::int32_t scar) {
            w.pod(idx);
            w.pod(scar);
        });
    }
}
// A FIELD needs a world to be a field over (the deposits' own lesson): the
// reader has already filled the dimensions, so a block sizes the grid it is
// about to fill. Only the rows the FILE carries cells for get sized, which is
// exactly the registry's rule — a carrier row writes no cells, so it grows no
// scar field here either, and the load path needs no access to the table.
bool size_for_world(const GameState& gs, ResourceGrid& g) {
    if (g.live()) return true;
    if (gs.mapW <= 0 || gs.mapH <= 0) return false;
    g.allocate(gs.mapW, gs.mapH, 0);
    return g.live();
}

bool scars_read(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        std::uint32_t n = 0;
        if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
        if (!st.gs) { r.ok = false; return false; }
        ResourceGrid& scars = st.gs->resourceScarCells[f];
        if (n > 0 && !size_for_world(*st.gs, scars)) { r.ok = false; return false; }
        for (std::uint32_t i = 0; i < n && r.ok; ++i) {
            std::uint32_t idx = 0;
            std::int32_t scar = 0;
            r.pod(idx);
            r.pod(scar);
            if (r.ok && scars.live()) {
                scars.write(scars.x_of(idx), scars.y_of(idx), scar);
            }
        }
    }
    return r.ok;
}

// ── Worked: THE one layer of S5, sparse on the wire (v96) ───────────────
// The number under a feature — hulls moored at a harbour today. One block,
// because there is one layer: «1 шахта в клетке — одно поле в клетке».
void worked_write_block(savefmt::Writer& w, const WorldFieldStores& st) {
    if (!st.gs) { w.count(0, kMaxFieldCells); return; }
    const ResourceGrid& g = st.gs->worked;
    if (!w.count(std::size_t(g.liveCells), kMaxFieldCells)) return;
    g.for_each_live([&](std::uint32_t idx, std::int32_t value) {
        w.pod(idx);
        w.pod(value);
    });
}
bool worked_read_block(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    if (!st.gs) { r.ok = false; return false; }
    ResourceGrid& g = st.gs->worked;
    if (n > 0 && !size_for_world(*st.gs, g)) { r.ok = false; return false; }
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint32_t idx = 0;
        std::int32_t value = 0;
        r.pod(idx);
        r.pod(value);
        if (r.ok && g.live()) g.write(g.x_of(idx), g.y_of(idx), value);
    }
    return r.ok;
}

// ── Built: features squads made — ploughed fields… (since v71) ──────────
// Verbatim, append-order: the list IS the history of works, and the load
// re-stamps it in the same order the crews built it.
void built_write(savefmt::Writer& w, const WorldFieldStores& st) {
    if (!st.gs) { w.count(0, kMaxFieldCells); return; }
    const auto& built = st.gs->builtFeatures;
    if (w.count(built.size(), kMaxFieldCells)) {
        for (const BuiltFeature& b : built) {
            w.pod(b.x);
            w.pod(b.y);
            w.pod(b.ft);
        }
    }
}
bool built_read(savefmt::Reader& r, const WorldFieldStoresMut& st) {
    std::uint32_t n = 0;
    if (!savefmt::read_count(r, n, kMaxFieldCells)) return false;
    if (!st.gs) { r.ok = false; return false; }
    auto& built = st.gs->builtFeatures;
    built.clear();
    built.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        BuiltFeature b{};
        r.pod(b.x);
        r.pod(b.y);
        r.pod(b.ft);
        if (r.ok) built.push_back(b);
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
    {WorldField::Built,     "built",     built_write,     built_read},
    {WorldField::Worked,    "worked",    worked_write_block, worked_read_block},
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
