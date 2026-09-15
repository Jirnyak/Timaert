// THE resource fields of the world — the owner's causality made structure:
//
//   клеточный мир → рельеф → климат → РЕСУРСЫ → и только потом заселение.
//
// A resource field is a per-cell quantity over the whole map whose BASELINE
// is a pure function of terrain/climate + the world seed. Features and
// landmarks NEVER enter a baseline: settlement is downstream of resources
// (a village sits by good land; good land does not appear because a village
// sat down). What settlement does is decide WHERE a resource gets embodied
// (ploughed parcels manifest the wheat a cell could always grow).
//
// STORAGE IS ONE SHAPE FOR EVERY ROW (owner, 2026-09-16, CANON S5 amended):
// a flat array over the world's cells — ResourceGrid below. There is no second
// dialect. What differs between rows is only WHAT the cell holds, and the
// registry already says which:
//   · a row with NO baseline is its own stock (trees, veins): the cell IS the
//     live count, and it may grow past its virgin derivation — the baseline
//     was an initial condition, not an attractor (untouched land thickens
//     into чащобы).
//   · a row WITH a baseline is capacity minus what play took (wheat, fauna):
//     the cell holds the SCAR, and the world tends back to the baseline. A
//     scar subtracts correctly from any embodied yield a subworld invents;
//     a remaining-count could not, which is why these two rows carry one.
//
// THE DIALECT THAT DIED, and why it is worth a paragraph: three of the seven
// rows used to live in `unordered_map<cellIdx, value>`, blessed by a line of
// S5 that allowed "sparse storage as an implementation". A hash keeps the
// values and throws away the CONNECTEDNESS of the world — it answers "what is
// at exactly this key" and nothing else — so the day geology was asked "is
// there a vein near this cell" the answer became a scan of all 69 624 veins,
// per cell, 4.3 ms of a 6.8 ms seam crossing (problems.md §52). The array
// costs 25 MB more and IS the torus: index arithmetic is adjacency.
//
// The macro_stock ledger stays THE gameplay read/write door — its rows call
// through here.
#pragma once

#include "macro/seasons.h"
#include "core/torus.h"   // wrapi — a field is indexed by the TORUS
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sm {

// A profession works ITS OWN village's ground: half the live worlds' village
// spacing (~21-23 cells, derive_city_spacing/2) rounded to po2 — a work trip
// stays inside the home hinterland, never the neighbour's. The spawn side
// reads the same number: ore inside this reach raises the profession, and the
// man it raises can actually walk to the ore.
//
// It lives HERE, with the resource rows, rather than with the AI that walks
// it, because three modules now measure by it: the gatherer's errand
// (npc_ai.h), the street crowd's trade gate (cell_facts.h depositsNear) and
// the reach field that answers the second in O(1) (deposit_layer.h). A number
// two modules must agree on belongs below both.
inline constexpr int kGathererReach = 16;

struct GameState;
struct TerrainData;
struct TreeLayer;
struct MacroWorld;

enum class ResourceFieldId : std::uint8_t {
    Wheat = 0,   // standing-wheat potential — fertility (climate G channel)
    Fauna,       // wild headcount — biome capacity (macro/fauna.h)
    Trees,       // forest of a cell — carrier row (macro/tree_layer.h grid)
    Clay,        // alluvial pits — carrier row (macro/deposit_layer.h)
    Iron,        // finite mountain veins — carrier row (deposit_layer.h)
    Stone,       // quasi-infinite quarries — carrier row (deposit_layer.h)
    Silver,      // the mint metal — finite veins (deposit_layer.h)
    Count,
};

// The wheat row's SCALE: the fertility channel (0..255) maps onto this many
// stands in a cell. It lives here, at the row's own door, because everyone
// who prices fertility must price it the same way — the field stamp's
// ploughable bar and the settlement score's arable term both derive from
// this number rather than each re-deriving one of their own.
constexpr int kMaxWheatStandsPerCell = 4096;   // po2 scale of the estimate

// ── The ONE growth/diffusion law (owner, R2 track) ────────────────────────
// Every field is BORN from time and context through the same walker; the
// nuances are the row's data, never a second system. The map is walked in
// EPOCH slices: a cell is due once per kGrowthEpochDays, and one game day
// processes cells/32 of the domain — smooth long-term dynamics need no
// full-map day and no per-tick work.
//
// kGrowthEpochDays = 32: the year is 128 days = 4 seasons × 32 — the same
// month the path grid is re-baked on (Session 21), and exactly the old
// wheat/fauna healing period, so their law (+1 per visit) is preserved by
// construction.
// THE season (owner, 2026-08-24: «все росты раз в сезон» — one rhythm for
// everything that grows: forest, beasts, wheat, iron, and the towns quote
// their population rate by the same period in econ_day.h). The epoch used
// to be its own literal 32 that merely HAPPENED to equal the season.
constexpr int kGrowthEpochDays = kDaysPerSeason;

inline bool growth_cell_due(std::uint32_t cellIdx, int day) {
    return int(cellIdx % std::uint32_t(kGrowthEpochDays))
        == day % kGrowthEpochDays;
}

// What ground the walker covers for a row:
//   None        — the field does not grow (stone, clay: quasi-static).
//   CarrierGrid — every cell of the dense carrier (trees: чащобы thicken,
//                 forests spread into neighbours).
//   OwnScars    — only cells play has scarred (wheat, fauna: capacity is
//                 the baseline, so unscarred cells have nowhere to grow).
//   Geology     — lump birth on the HOST row's cells that lack this row
//                 (iron: the scarcer the world's iron, the likelier a stone
//                 quarry turns out to hold a vein — the W2c rule as a row).
enum class GrowthDomain : std::uint8_t { None, CarrierGrid, OwnScars, Geology };

struct ResourceFieldDef {
    const char* id;
    // The pure baseline: world context → capacity of one WRAPPED cell.
    // Sparse rows read baseline − scar and clamp the scar to it; a carrier
    // row's baseline is its initial condition only (null: the carrier was
    // filled by worldgen and lives its own life from there).
    int (*baseline)(const MacroWorld& w, int x, int y);
    // The growth law. growthAt = units born at a DUE cell this visit (for
    // Geology: the lump a fresh vein opens with). Pure and deterministic —
    // context in, delta out; the walker owns dueness and the write.
    GrowthDomain growthDomain;
    int (*growthAt)(const MacroWorld& w, int x, int y);
    ResourceFieldId growthHost;   // Geology only: whose cells host the birth
    // Carrier hooks — both set = the row's live state is a dense structure
    // outside the scar maps (trees: the TreeLayer grid the map renders).
    // Read is the current count; apply clamps to the row's own cap, writes
    // the carrier and bumps its revision. Sparse rows leave both null.
    int  (*carrierRead)(const MacroWorld& w, int x, int y);
    void (*carrierApply)(MacroWorld& w, int x, int y, int delta);
    // HOW FAR THIS ROW REACHES, in cells — the radius of the derived field
    // that answers "is there any of this near here". 0 = the row is never
    // asked about a neighbourhood and pays for no reach field.
    //
    // It is a COLUMN because the question is not geology's: the street crowd
    // gates a miner on ore within a gatherer's reach today, and the day
    // somebody gates a woodcutter on forest within his, the answer already
    // exists. That was the shape of the original defect — the reach question
    // was written as a one-off scan inside the context assembler instead of
    // as a property of the row being asked about.
    int reachCells;
};

// The registry row (macro_stock.cpp owns the table).
const ResourceFieldDef& resource_field_def(ResourceFieldId f);

// The current count of a cell. Sparse rows: capacity − scar, floored at
// zero, fail-closed without terrain. Carrier rows: the carrier's cell.
int resource_field_read(const MacroWorld& w, ResourceFieldId f, int x, int y);

// Move the field: negative spends, positive returns. Sparse rows clamp the
// scar to [0, baseline] and a healed cell self-erases; carrier rows clamp
// to their own cap and bump their carrier's revision.
void resource_field_apply(MacroWorld& w, ResourceFieldId f, int x, int y,
                          int delta);

// The raw scar of a cell (0 = unscarred) — what subworld generation
// subtracts from its own embodied yield (CellContext.cropHarvested).
// Sparse-dialect rows only; a carrier row has no scar and reads 0.
int resource_field_scar(const GameState& gs, ResourceFieldId f,
                        std::uint32_t cellIdx);

// One daily step for EVERY field — THE growth/diffusion door: each row's
// due slice of its domain is visited, growthAt prices the birth, and the
// write goes through the row's own dialect (scar heal / carrier apply /
// genesis). Replaces both the old scar-heal walk and the bespoke iron
// discovery.
void resource_fields_daily_growth(MacroWorld& w, int day);


// ── THE FIELD OF ONE ROW ─────────────────────────────────────────────────
// CANON S5, as amended by the owner on 2026-09-16: every resource row owns
// ONE field over the world, and it is a flat array over the cells — the shape
// the tree layer has always had. There is no second storage dialect.
//
// WHY THE SHAPE IS THE POINT, not the speed. A flat array over the torus IS
// the connected world: `wrap(y)*w + wrap(x)`, so a neighbouring cell is
// neighbouring memory and "near" is arithmetic. A hash of cell indices keeps
// the values and throws the connectedness away — it answers "what is at
// exactly this key" and nothing else, so every question about a neighbourhood
// has to be rebuilt as a scan by whoever asks it next. That is not a
// hypothetical: geology was a hash from its first commit (81379bf6, whose own
// message claimed "the proven tree-layer discipline"), and the day someone
// asked it "is there a vein near this cell" the answer became 69 624 distance
// tests per cell — 4.3 ms of a 6.8 ms seam crossing, for one byte
// (problems.md §52).
//
// THE COST OF BEING RIGHT: seven rows × 1024² × 4 bytes = 28 MB, against a
// hash that held ~3-4 MB. Twenty-five megabytes, in a world whose own DOD law
// calls 48 MB "ни о чём" and which carries 256 inventory slots on each of
// 16 384 entities. Memory compression is a GATED decision here, never a
// default (AGENTS.md DOD rule 8).
//
// WHAT THE CELLS HOLD is the row's OWN state — the part that is not derivable
// — and the registry already says which that is: a row with no `baseline` is
// its own stock (trees, veins), a row with one is a baseline minus what play
// took, so its field holds the SCAR. One shape, one meaning per row, and the
// `baseline` column is the reader that was already there.
struct ResourceGrid {
    int width = 0, height = 0;
    std::vector<std::int32_t> cells;
    // How far this row reaches, copied from its registry column at build. The
    // grid keeps its OWN derived field in step (see write below), so no door
    // can forget to — which is the whole reason the radius lives here and not
    // at the call site that stamps.
    int reachCells = 0;
    // Live cells, maintained by write(). Never counted: a row is asked "how
    // many veins are left" often enough that an O(cells) answer would be a
    // second §52 waiting to happen.
    std::int32_t liveCells = 0;
    // DERIVED, never saved: how many live cells of this row lie within
    // `reachCells` of each cell. Counts rather than flags because the discs
    // overlap — a cell reached by three veins must survive two running dry.
    // Empty for a row that declares no reach.
    std::vector<std::uint16_t> reach;
    // Bumped on every write, for consumers that mirror the field (the map
    // renderer's tree texture). Never serialized.
    std::uint32_t revision = 0;

    bool live() const {
        return width > 0 && height > 0
            && cells.size() == std::size_t(width) * std::size_t(height);
    }
    std::uint32_t index(int x, int y) const {
        return std::uint32_t(wrapi(y, height)) * std::uint32_t(width)
             + std::uint32_t(wrapi(x, width));
    }
    std::int32_t at(int x, int y) const {
        return live() ? cells[index(x, y)] : 0;
    }
    // "Is a live cell of this row within the row's reach of here?" — the
    // O(1) answer the scan used to compute. Fail-closed on a row with no
    // reach field: "nothing near" grants nothing.
    bool near(int x, int y) const {
        if (!live() || reach.size() != cells.size()) return false;
        return reach[index(x, y)] != 0u;
    }

    // THE ONE WRITE. Everything a cell's value implies — the live count, the
    // reach discs, the revision — moves with it, here, because a derived field
    // kept in step by its CALLERS is a field that drifts the first time
    // somebody adds a fifth caller. (It had four, and a hand-run negative
    // control found that removing the stamp from any one of them left the
    // world silently wrong: ore gone, trade still granted.)
    void write(int x, int y, std::int32_t value) {
        if (!live()) return;
        const std::uint32_t i = index(x, y);
        const std::int32_t was = cells[i];
        if (was == value) return;
        cells[i] = value;
        if (was == 0 && value != 0) { ++liveCells; stamp(x, y, +1); }
        else if (was != 0 && value == 0) { --liveCells; stamp(x, y, -1); }
        ++revision;
    }

    // Every cell that holds something, as (index, value). O(cells) by nature —
    // a dense field has no shorter honest way to enumerate itself, and the
    // callers that cannot afford it (the nearest-vein errand) must ask a
    // NEIGHBOURHOOD question instead of enumerating the world.
    template <class F>
    void for_each_live(F&& f) const {
        if (!live()) return;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (cells[i] != 0) f(std::uint32_t(i), cells[i]);
        }
    }

    // Size the field to a world, zeroed, with its reach field iff it reaches.
    void allocate(int w, int h, int reachRadiusCells) {
        width = w;
        height = h;
        reachCells = reachRadiusCells;
        liveCells = 0;
        revision = 0;
        cells.assign(std::size_t(w) * std::size_t(h), 0);
        if (reachRadiusCells > 0) {
            reach.assign(cells.size(), std::uint16_t(0));
        } else {
            reach.clear();
        }
    }

private:
    // One cell's contribution to the reach field: the disc of cells from which
    // this row could be worked here.
    //
    // THE DISC IS THE QUESTION'S OWN SHAPE. The gate this replaces measured
    // `torus_dist_sq <= r²` — a circle — so the stamp walks a circle, its rows'
    // half-widths taken from the same inequality. A square would have been
    // separable and faster and would grant the corner cells something they
    // cannot reach: a stamp is an implementation of a question, never a
    // licence to answer a different one.
    void stamp(int x, int y, int delta) {
        if (reachCells <= 0 || reach.size() != cells.size()) return;
        const int R = reachCells;
        for (int dy = -R; dy <= R; ++dy) {
            const int span = int(std::sqrt(double(R * R - dy * dy)));
            for (int dx = -span; dx <= span; ++dx) {
                std::uint16_t& c = reach[index(x + dx, y + dy)];
                // A count can only be walked down by a cell that walked it up,
                // so the floor is a statement about write() rather than a
                // clamp. Guarded anyway: an underflow grants forever.
                if (delta > 0) ++c;
                else if (c > 0u) --c;
            }
        }
    }
};

// All seven fields, one place. Adding a resource — gold, coal, anything — is a
// row of the registry, and its field appears with it; there is no new code and
// no new container to remember.
struct ResourceFields {
    ResourceGrid rows[std::size_t(ResourceFieldId::Count)];

    ResourceGrid& operator[](ResourceFieldId f) {
        return rows[std::size_t(f) < std::size_t(ResourceFieldId::Count)
                        ? std::size_t(f) : 0];
    }
    const ResourceGrid& operator[](ResourceFieldId f) const {
        return rows[std::size_t(f) < std::size_t(ResourceFieldId::Count)
                        ? std::size_t(f) : 0];
    }
};

} // namespace sm
