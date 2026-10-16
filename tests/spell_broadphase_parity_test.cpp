// The spell broad phase is a STRUCTURAL move, and this test is its proof of
// identity: for the same scene and the same projectile, the grid-backed
// candidate enumeration must produce EXACTLY the hits of the full O(N·M)
// registry scan it replaced. The full scan is kept alive in the shipping code
// as the nullptr arm of for_each_spell_candidate, so the reference here is the
// real code path, not a copy.
//
// Four scenarios, one per scan that was replaced: the swept bolt (plus its
// muzzle stretch), the blast sphere, the beam, and the chain. Each runs on
// three identical worlds:
//   • Full  — no broad phase at all (the reference),
//   • Grid  — the bucket-grid callback, mirroring the engine's
//             spell_neighbors_callback over crowdPick_,
//   • Lossy — a callback that returns zero candidates. This one MUST DIFFER:
//             it is the negative control that proves the parity assertions
//             can catch a broad phase that loses bodies.
//
// МИР «Fallback» (колбэк, всегда отвечавший -1) СНЯТ ВМЕСТЕ С КОНТРАКТОМ
// (M-261): «полноту не обещаю» больше не выразимо в типе двери — счёт
// беззнаковый, — и свидетель, кормивший её -1, охранял мёртвый путь, а не
// закон мира (§8 п.5). Оба входа в тот путь были недостижимы по построению:
// кап сцены ОДИН на арену, толпу и этот буфер, а слот у каждого тела СВОЙ.
#include "check.h"
#include "ecs/world.h"
#include "scene_objects_fixture.h"  // арена: бары — колонки (кусок 2)

#include "sub/spell_effects.h"
#include "sub/movement.h"
#include "sub/body.h"
#include "ecs/components.h"
#include "macro/store.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using sm::ecs::Position;
using sm::ecs::Pools;
using sm::ecs::Projectile;

constexpr int kBodyHp = 100;

// Тело сцены — СЛОТ арены (ломоть D): сущностей у тел больше нет, рождение
// идёт слот-дверью фикстуры, место и бары — колонки того же слота.
void add_body(entt::registry& reg, float x, float y, float z) {
    const int s = sm::test::give_body(reg);
    sm::test::give_pos(reg, s, x, y, z);   // место — колонка арены (ломоть 5)
    sm::test::give_pools(reg, s, Pools{kBodyHp, kBodyHp});
}

// One deterministic scene per scenario: the acting bodies first, then a
// pseudo-random crowd far from the action so the broad phase has something
// real to exclude. The LCG is the test's own — no world seed, no clock.
void build_scene(entt::registry& reg, int scenario) {
    switch (scenario) {
    case 0:                                   // swept bolt + muzzle stretch
        add_body(reg, 96.0f, 100.0f, 0.5f);   // caster (owner of the bolt)
        add_body(reg, 120.0f, 100.5f, 1.0f);  // first victim on the path
        add_body(reg, 140.0f, 99.0f, 1.2f);   // behind the first — must survive
        add_body(reg, 120.0f, 108.0f, 1.0f);  // near the path, out of reach
        break;
    case 1:                                   // blast
        for (int i = 0; i < 8; ++i) {         // inside the 12-unit sphere
            const float a = float(i) * 0.7853982f;
            add_body(reg, 500.0f + std::cos(a) * 8.0f,
                     500.0f + std::sin(a) * 8.0f, 1.0f + float(i % 3));
        }
        // Inside the 2D circle, OUTSIDE the 3D sphere: the broad phase must
        // offer it and the exact check must refuse it.
        add_body(reg, 511.0f, 500.0f, 8.0f);
        add_body(reg, 516.0f, 500.0f, 1.0f);  // outside both
        break;
    case 2:                                   // beam
        add_body(reg, 615.0f, 600.5f, 4.0f);  // on the beam line
        add_body(reg, 610.0f, 606.0f, 4.0f);  // near, off the line
        add_body(reg, 650.0f, 600.0f, 4.0f);  // past the beam's end
        break;
    case 3:                                   // chain
        add_body(reg, 815.0f, 800.0f, 1.0f);  // struck by the bolt
        add_body(reg, 824.0f, 803.0f, 1.0f);  // hop 1 (9.5 away)
        add_body(reg, 836.0f, 801.0f, 1.0f);  // hop 2 (12.2 further)
        add_body(reg, 860.0f, 820.0f, 1.0f);  // beyond chainRadius — safe
        break;
    }
    std::uint32_t lcg = 0xC0FFEEu;
    for (int i = 0; i < 150; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        const float x = 1000.0f + float(lcg >> 20);          // 1000..5095
        lcg = lcg * 1664525u + 1013904223u;
        const float y = 1000.0f + float(lcg >> 20);
        add_body(reg, x, y, 1.0f);
    }
}

// ПОРЯДОК РОЖДЕНИЯ ЕСТЬ ВОСХОДЯЩИЙ ПОРЯДОК СЛОТОВ (ломоть D), и реверса
// больше нет: бегунок арены стартует с нуля, `alloc` отдаёт первый свободный
// слот от бегунка, а эта фикстура не освобождает ни одного — значит тела
// легли слотами 0,1,2,… ровно в том порядке, в каком их позвал `build_scene`,
// и сортировать нечего. Снаряд в счёт не идёт по той же колонке, что и
// прежде (баров у него нет: maxHp 0), да и рождается он ПОЗЖЕ этого обхода.
std::vector<int> bodies_in_creation_order(entt::registry& reg) {
    std::vector<int> v;
    sm::sub::SubObjects& objs = sm::test::arena_of(reg);
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        if (sm::sub::slot_pools(objs, s) == nullptr) continue;
        v.push_back(s);
    }
    return v;
}

// ── The grid broad phase under test — the engine callback's twin ───────────
// Same walk, same padding law (fattest body radius; the scene is static so
// the per-tick-drift term is zero), и та же ФОРМА ДВЕРИ: ссылка на массив
// длиной в кап и беззнаковый счёт, так что усечь выдачу нечем — ровно как у
// `spell_neighbors_callback`. Груз — СЛОТ арены (ступень (iv) ломтя 7).
struct GridBroadPhase {
    // One flat MiB in its own block — the rig cannot hold it by value.
    std::unique_ptr<sm::sub::BodyCrowd> units = sm::sub::make_body_crowd();
    sm::sub::UnitGrid pick;
    std::vector<std::uint16_t> slots;
    float pad = 0.0f;

    void build(entt::registry& reg) {
        units->clear();
        slots.clear();
        sm::sub::SubObjects& objs = sm::test::arena_of(reg);
        // СВИП ПЛОСКОЙ АРЕНЫ (ломоть D): жилец — бит kObjAlive, и это тот же
        // обход, которым сетку кормит `spell_neighbors_callback` движка.
        // Снарядные слоты сюда не попадают по той же колонке, что и прежде
        // (баров у снаряда нет: maxHp 0), а порядок выдачи стал ВОСХОДЯЩИМ
        // по слоту — тем же, что у эталонного `all_live_slots`, то есть
        // сетка сверяется с полным проходом в одном порядке, а не в обратном.
        for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
            if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u)
                continue;
            if (sm::sub::slot_pools(objs, s) == nullptr) continue;
            const Position& p = objs.pos[std::size_t(s)];
            sm::sub::BodyDesc d{};
            d.x = p.x; d.y = p.y; d.z = p.z;
            d.radius = sm::sub::slot_body_radius(objs, s);
            units->add(d);
            slots.push_back(std::uint16_t(s));
        }
        const sm::sub::MoveParams prm{};
        build_unit_grid(pick, *units, pick_cell_for(*units, prm), 256);
        pad = units->maxRadius;
    }

    static std::uint32_t fn(void* user, float x, float y, float r,
                            std::uint16_t (&out)[sm::kMaxSubObjects]) {
        auto* self = static_cast<GridBroadPhase*>(user);
        const auto& u = *self->units;
        const auto& g = self->pick;
        if (u.count <= 0) return 0;
        const float rr = r + self->pad;
        const float rr2 = rr * rr;
        const int c0 = g.col_of(x - rr), c1 = g.col_of(x + rr);
        const int r0 = g.row_of(y - rr), r1 = g.row_of(y + rr);
        std::uint32_t n = 0;
        for (int cy = r0; cy <= r1; ++cy) {
            for (int cx = c0; cx <= c1; ++cx) {
                const std::size_t ci = std::size_t(cy) * std::size_t(g.cols)
                                     + std::size_t(cx);
                for (std::uint32_t k = g.begin[ci]; k < g.begin[ci + 1u]; ++k) {
                    const std::size_t sj = std::size_t(g.items[k]);
                    const float dx = u.x[sj] - x, dy = u.y[sj] - y;
                    if (dx * dx + dy * dy > rr2) continue;
                    // Границ не проверяем по той же причине, что и движок:
                    // тел в сетке `u.count`, каждое в ОДНОЙ клетке, клетки
                    // обходятся по разу, а `out` длиной в кап — n ≤ кап.
                    out[n++] = self->slots[sj];
                }
            }
        }
        return n;
    }

    static std::uint32_t lossy_fn(void*, float, float, float,
                                  std::uint16_t (&)[sm::kMaxSubObjects]) {
        return 0;                           // loses every body — must be caught
    }
};

enum class Mode { Full, Grid, Lossy };

// Build the scene, fire the scenario's projectile, tick, and report every
// body's hp in creation order.
std::vector<float> run_scenario(int scenario, Mode mode) {
    sm::ecs::World w{};
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    auto& reg = w.reg;
    build_scene(reg, scenario);
    const auto bodies = bodies_in_creation_order(reg);
    sm::sub::SubObjects& objs = sm::test::arena_of(reg);

    const float dt = 1.0f / 64.0f;
    int ticks = 1;
    // Снаряд — ЧИСТЫЙ ЖИЛЕЦ АРЕНЫ (ступень (v)): рождается слот-дверью,
    // сущности нет; позиция — аргумент рождения.
    Projectile boltP{};
    Position boltAt{};
    switch (scenario) {
    case 0: {
        // Владелец — ссылка сцены (iii-б): пара {slot, id} тела bodies[0],
        // и слот у него теперь ПРЯМОЙ — перевода из сущности нет (ломоть D).
        const sm::sub::ObjRef owner = sm::sub::ref_of(objs, bodies[0]);
        boltAt = Position{98.5f, 100.0f, 1.0f};
        boltP = Projectile{
            .vx = 400.0f, .vy = 0.0f, .vz = 0.0f, .radius = 1.5f,
            .lifeTimer = 1.0f, .maxLifeTimer = 1.0f,
            .damage = 13, .blastRadius = 0.0f,
            .originX = 98.5f, .originY = 100.0f, .beamLength = 0.0f,
            .chainDecayPct = std::uint8_t(0), .ownerSlot = owner.slot,
            .chainRadius = 0.0f, .spellId = 1u, .ownerId = owner.id,
            .chainRemaining = std::int16_t{0}, .kind = Projectile::Bolt,
            .visualOnly = false,
            .explodeOnExpiry = false, .dmgType = std::uint8_t(0),
            .critical = false};
        ticks = 10;
        break;
    }
    case 1:
        boltAt = Position{500.0f, 500.0f, 1.0f};
        boltP = Projectile{
            .vx = 0.0f, .vy = 0.0f, .vz = 0.0f, .radius = 1.5f,
            .lifeTimer = 0.005f, .maxLifeTimer = 1.0f, // expires first tick
            .damage = 20, .blastRadius = 12.0f,
            .originX = 500.0f, .originY = 500.0f, .beamLength = 0.0f,
            .chainDecayPct = std::uint8_t(0), .ownerSlot = 0,
            .chainRadius = 0.0f, .spellId = 2u,
            .ownerId = 0u,   // «ничей» — id 0 (iii-б; прежний 0xFFFFFFFF умер)
            .chainRemaining = std::int16_t{0}, .kind = Projectile::Bolt,
            .visualOnly = false,
            .explodeOnExpiry = true, .dmgType = std::uint8_t(0),
            .critical = false};
        break;
    case 2: {
        // Direction (10, 0, 2) normalised inside; entity sits at the beam
        // midpoint, origin Z therefore 1.0 (see apply_spell_beam).
        const float len3 = std::sqrt(10.0f * 10.0f + 2.0f * 2.0f);
        const float nx = 10.0f / len3, nz = 2.0f / len3;
        boltAt = Position{600.0f + nx * 20.0f, 600.0f, 1.0f + nz * 20.0f};
        boltP = Projectile{
            .vx = 10.0f, .vy = 0.0f, .vz = 2.0f, .radius = 1.2f,
            .lifeTimer = 0.005f, .maxLifeTimer = 1.0f,
            .damage = 15, .blastRadius = 0.0f,
            .originX = 600.0f, .originY = 600.0f, .beamLength = 40.0f,
            .chainDecayPct = std::uint8_t(0), .ownerSlot = 0,
            .chainRadius = 0.0f, .spellId = 3u,
            .ownerId = 0u,   // «ничей» — id 0 (iii-б)
            .chainRemaining = std::int16_t{0}, .kind = Projectile::Beam,
            .visualOnly = false,
            .explodeOnExpiry = false, .dmgType = std::uint8_t(0),
            .critical = false};
        break;
    }
    case 3:
        boltAt = Position{800.0f, 800.0f, 1.0f};
        boltP = Projectile{
            .vx = 400.0f, .vy = 0.0f, .vz = 0.0f, .radius = 1.5f,
            .lifeTimer = 1.0f, .maxLifeTimer = 1.0f,
            .damage = 16, .blastRadius = 0.0f,
            .originX = 800.0f, .originY = 800.0f, .beamLength = 0.0f,
            .chainDecayPct = std::uint8_t(50), .ownerSlot = 0,
            .chainRadius = 15.0f, .spellId = 4u,
            .ownerId = 0u,   // «ничей» — id 0 (iii-б)
            .chainRemaining = std::int16_t{3}, .kind = Projectile::Bolt,
            .visualOnly = false,
            .explodeOnExpiry = false, .dmgType = std::uint8_t(0),
            .critical = false};
        ticks = 10;
        break;
    }
    sm::test::give_projectile(reg, boltP, boltAt);

    GridBroadPhase grid{};
    grid.build(reg);

    // ЭТАЛОН — ПЕРЕЧИСЛИТЕЛЬ ФИКСТУРЫ, А НЕ ПОЛНЫЙ СКАН ВНУТРИ ДВЕРИ
    // (M-261): ветки «сетки не дали» у двери больше нет, потому что она была
    // проходом по всему капу внутри тика. Семантика эталона НЕ изменилась —
    // «спроси у всех живых слотов» есть ровно то, что делал снесённый скан,
    // только теперь это код ПРИБОРА, а не симуляции (§8 п.8).
    sm::sub::SpellNeighborsFn fn = &sm::test::all_live_slots;
    void* user = &objs;
    switch (mode) {
    case Mode::Full: break;
    case Mode::Grid:  fn = &GridBroadPhase::fn; user = &grid; break;
    case Mode::Lossy: fn = &GridBroadPhase::lossy_fn; break;
    }

    for (int t = 0; t < ticks; ++t) {
        sm::sub::tick_spell_projectiles(w, nullptr, dt, fn, user);
    }

    std::vector<float> hp;
    hp.reserve(bodies.size());
    for (const int s : bodies) {
        hp.push_back(float(sm::sub::slot_pools(objs, s)->hp));
    }
    return hp;
}

const char* scenario_name(int s) {
    switch (s) {
    case 0: return "sweep";
    case 1: return "blast";
    case 2: return "beam";
    default: return "chain";
    }
}

void test_scenario(int s) {
    const auto full = run_scenario(s, Mode::Full);
    const auto grid = run_scenario(s, Mode::Grid);
    const auto lossy = run_scenario(s, Mode::Lossy);

    int damaged = 0;
    for (float h : full) {
        if (h < kBodyHp) ++damaged;
    }
    CHECK(damaged > 0, "the scenario draws blood (else parity proves nothing)");

    CHECK(full.size() == grid.size() && full.size() == lossy.size(),
          "same bodies in every world");
    bool gridSame = true, lossySame = true;
    for (std::size_t i = 0; i < full.size(); ++i) {
        if (full[i] != grid[i]) gridSame = false;
        if (full[i] != lossy[i]) lossySame = false;
    }
    std::fprintf(stderr,
                 "[spell_broadphase] %s: %d damaged, grid %s, lossy %s\n",
                 scenario_name(s), damaged,
                 gridSame ? "==" : "!=", lossySame ? "==" : "!=");
    CHECK(gridSame, "grid broad phase hits EXACTLY what the full scan hits");
    // Negative control: a broad phase that loses bodies must be VISIBLE to
    // the comparison above, or it guards nothing.
    CHECK(!lossySame, "a lossy broad phase must change outcomes (control)");
}

// The blast scene plants a body inside the 2D query circle but outside the
// 3D blast sphere — assert the exact phase, not the broad phase, decides.
void test_superset_is_filtered() {
    const auto full = run_scenario(1, Mode::Grid);
    // Creation order: 8 victims, then the 2D-in/3D-out body, then the safe one.
    CHECK(full[8] == kBodyHp,
          "a candidate the exact 3D check refuses is not damaged");
    CHECK(full[9] == kBodyHp, "a body outside both circles is untouched");
    for (int i = 0; i < 8; ++i) {
        CHECK(full[std::size_t(i)] < kBodyHp, "every body in the sphere bled");
    }
}

} // namespace

int main() {
    for (int s = 0; s < 4; ++s) test_scenario(s);
    test_superset_is_filtered();
    return sm::test::report("spell_broadphase_parity_test");
}
