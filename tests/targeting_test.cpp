// Unit tests for sm::sub::aim_target — the pure subworld targeting primitive
// (Increment 5 possession groundwork). Builds a bare scene arena (ломоть D:
// тело сцены есть СЛОТ, сущности у него нет вовсе), populates candidates, and
// pins the selection contract: nearest live enemy within the range AND the
// forward cone, excluding the player's own side and the shooter.
//
// Assertions go through tests/check.h — CHECK + sm::test::report.

#include "check.h"
#include "scene_objects_fixture.h"  // give_flag — биты маски (ломоть 1б)

#include "sub/targeting.h"

#include <cmath>

using namespace sm;

// A live, targetable subworld enemy at (x,y,z). Возврат — СЛОТ арены
// (ломоть D): рождение тела есть выдача слота, -1 = кап. Груз — САМА АРЕНА
// (ломоть E): реестра-посредника больше не существует.
static int make_enemy(sub::SubObjects& objs, float x, float y,
                      float z = 0.0f) {
    const int s = sm::test::give_body(objs);
    sm::test::give_pos(objs, s, x, y, z);  // место — колонка арены (ломоть 5)
    sm::test::give_pools(objs, s, ecs::Pools{10, 10});
    sm::test::give_kind(objs, s,
        ecs::NPCKind{std::uint16_t(4), std::uint16_t(0)}); // any kind
    return s;
}

// cos of a half-angle in degrees, for cone tests.
static float cos_deg(float deg) {
    return std::cos(deg * 3.14159265358979f / 180.0f);
}

// The forward argument is the normalised 3D look vector; the flat-ground
// tests face +x as (1, 0, 0). kFaceX (a yaw) died with the 2D signature.
static constexpr float kFull  = -1.0f;       // 360° cone
static constexpr float kCone30 = 0;          // placeholder, set in main via cos_deg

int main() {
    const float cone30 = cos_deg(30.0f); // ±30° cone

    // ── nearest within a forward cone ─────────────────────────────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        const int a = make_enemy(objs, 105, 100);   // ahead, dist 5
        make_enemy(objs, 110, 100);                 // ahead, dist 10 (farther)
        make_enemy(objs, 95, 100);                  // behind, dist 5 (out of cone)
        const int picked = sub::aim_target(objs, 100, 100, 0, 1, 0, 0,50.0f, cone30);
        CHECK(picked == a, "picks nearest enemy inside the forward cone");
    }

    // ── range gate ────────────────────────────────────────────────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        const int a = make_enemy(objs, 105, 100);   // dist 5
        make_enemy(objs, 110, 100);                 // dist 10
        CHECK(sub::aim_target(objs, 100, 100, 0, 1, 0, 0,8.0f, cone30) == a,
              "range 8: near (5) selected, far (10) excluded");
        CHECK(sub::aim_target(objs, 100, 100, 0, 1, 0, 0,3.0f, cone30) == -1,
              "range 3: everything out of range -> null");
    }

    // ── cone gate: a target directly behind ───────────────────────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        const int behind = make_enemy(objs, 95, 100); // dist 5, bearing 180°
        CHECK(sub::aim_target(objs, 100, 100, 0, 1, 0, 0,50.0f, cone30) == -1,
              "narrow cone: target behind the shooter is not selected");
        CHECK(sub::aim_target(objs, 100, 100, 0, 1, 0, 0,50.0f, kFull) == behind,
              "full circle: behind target IS selected (reduces to melee)");
    }

    // ── full circle picks global nearest regardless of bearing ────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        make_enemy(objs, 105, 100);                  // ahead, dist 5
        const int near = make_enemy(objs, 100, 103); // side, dist 3 (nearest)
        CHECK(sub::aim_target(objs,
                              100, 100, 0, 1, 0, 0,50.0f, kFull) == near,
              "full circle: nearest overall selected irrespective of facing");
    }

    // ── player-side exclusion ─────────────────────────────────────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        const int a = make_enemy(objs, 106, 100);     // ahead, dist 6
        const int soldier = make_enemy(objs, 101, 100); // ahead, dist 1
        sm::test::give_flag(objs, soldier,
                            sub::kObjPlayerSoldier);  // player's own -> skip
        const int ptag = make_enemy(objs, 102, 100); // ahead, dist 2
        sm::test::make_avatar(objs, ptag);           // the player body -> skip
        CHECK(sub::aim_target(objs,
                              100, 100, 0, 1, 0, 0,50.0f, cone30) == a,
              "player-side entities excluded even when nearer");
    }

    // ── dead exclusion ────────────────────────────────────────────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        const int a = make_enemy(objs, 106, 100);   // ahead, dist 6
        const int corpse = make_enemy(objs, 101, 100); // ahead, dist 1
        sm::test::give_flag(objs, corpse, sub::kObjDead);
        CHECK(sub::aim_target(objs, 100, 100, 0, 1, 0, 0,50.0f, cone30) == a,
              "Dead entities excluded even when nearer");
        // Zero-HP but not yet tagged Dead is also excluded.
        const int downed = make_enemy(objs, 102, 100);
        (*sm::sub::slot_pools(objs, downed)).hp = 0.0f;
        CHECK(sub::aim_target(objs, 100, 100, 0, 1, 0, 0,50.0f, cone30) == a,
              "zero-HP entities excluded even when nearer");
    }

    // ── shooter self-exclusion ────────────────────────────────────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        const int self = make_enemy(objs, 100, 100); // co-located "body"
        const int a = make_enemy(objs, 105, 100);
        CHECK(sub::aim_target(objs,
                              100, 100, 0, 1, 0, 0,50.0f, kFull, self) == a,
              "shooter entity is never its own target");
    }

    // ── co-located enemy is reachable (no NaN bearing) ────────────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        const int onTop = make_enemy(objs, 100, 100); // exactly at shooter
        CHECK(sub::aim_target(objs,
                              100, 100, 0, 1, 0, 0,50.0f, cone30) == onTop,
              "co-located enemy is selected (treated as in-front)");
    }

    // ── empty world ───────────────────────────────────────────────────────
    {
        auto arena_ = sm::test::make_scene_arena();
        CHECK(sub::aim_target(*arena_,
                              100, 100, 0, 1, 0, 0,50.0f, kFull) == -1,
              "no candidates -> null");
    }

    // ── THE THIRD DIMENSION (CANON S13 «всё в бою трёхмерно», session Е) ──
    // The old law measured cone and distance in XY: a body far overhead sat
    // "in the cone" of a level shot, and looking up bought nothing. The aim
    // point is the target's EYES (body_eye_m over its feet — the same door
    // an NPC missile aims at), so every bearing below carries that offset.
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        // XY-distance 3 (dead ahead), but 4 up (plus eye height): the 3D
        // bearing is ~62° off a level look — far outside the ±30° cone the
        // XY law would have passed.
        const int high = make_enemy(objs, 103, 100, 4);
        CHECK(sub::aim_target(objs, 100, 100, 0, 1, 0, 0, 50.0f, cone30) == -1,
              "a level look does not select a body far overhead");
        // Aim UP the actual 3-4-5 bearing and the same body is the target.
        CHECK(sub::aim_target(objs, 100, 100, 0, 0.6f, 0, 0.8f, 50.0f, cone30)
                  == high,
              "looking up finds it: the cone follows the 3D look vector");
    }
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        // XY-distance 3 fits a 4.0 range; the true 3-4-5 distance is 5.
        make_enemy(objs, 103, 100, 4);
        CHECK(sub::aim_target(objs,
                              100, 100, 0, 1, 0, 0, 4.0f, kFull) == -1,
              "range is 3D: the XY shadow of a high body does not reach");
    }

    // ── melee_pick_target: hostiles first, nearest-any as fallback ────────
    // Выбор цели — слотами арены (ступень (iv) ломтя 7): груз оракула и
    // возврат — слот, -1 = никого.
    {
        auto arena_ = sm::test::make_scene_arena();
        sub::SubObjects& objs = *arena_;
        const int peasant = make_enemy(objs, 102, 100); // dist 2, NEUTRAL
        int bandit = make_enemy(objs, 104, 100);        // dist 4, HOSTILE
        auto hostileIsBandit = [](void* user, int s) {
            return s == *static_cast<int*>(user);
        };
        // The OLD rule (no oracle): nearest body of any stripe — the peasant
        // catches the blow meant for the bandit behind him. Kept as the
        // documented fallback when nothing hostile is in reach.
        const int oldPick = sub::melee_pick_target(
            objs, 100, 100, 0, 25.0f, nullptr, nullptr,
            &sm::test::all_live_slots, &objs);
        CHECK(oldPick == peasant,
              "fallback (no hostile oracle) is nearest-any — the old rule");
        // The NEW rule: the hostile wins even though it stands farther.
        const int pick = sub::melee_pick_target(
            objs, 100, 100, 0, 25.0f, hostileIsBandit, &bandit,
            &sm::test::all_live_slots, &objs);
        CHECK(pick == bandit,
              "a hostile in reach beats a nearer neutral");
        // No hostile in reach -> nearest-any fallback (striking a neutral
        // deliberately stays possible; the reputation price handles it).
        int far = make_enemy(objs, 200, 200);
        const int pick2 = sub::melee_pick_target(
            objs, 100, 100, 0, 25.0f, hostileIsBandit, &far,
            &sm::test::all_live_slots, &objs);
        CHECK(pick2 == peasant,
              "with no hostile in reach the swing falls back to nearest-any");
        // Range gate still absolute: nothing in reach -> -1.
        const int pick3 = sub::melee_pick_target(
            objs, 0, 0, 0, 25.0f, nullptr, nullptr,
            &sm::test::all_live_slots, &objs);
        CHECK(pick3 == -1, "outside reach nothing is picked");
    }

    (void)kCone30;
    return sm::test::report("targeting_test");
}
