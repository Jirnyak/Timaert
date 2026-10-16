// The melee reach law (damage-door track Inc 4, owner ruling 2026-08-27):
// reach is ONE number (the attacker's attackRange — a mob's row states it, a
// man's spear will modify it through the equipment increment), and the blow
// lands on the target's SURFACE: in reach ⇔ dist − body_radius(target) ≤
// range. The same law the NPC strike walks (battle.cpp reach + radius),
// finally read by the player's own swing.
//
// Pinned, with negative controls:
//   * a troll is struck from farther away than a frog, because a troll is
//     wider — same range, same distance, different answer, no branches;
//   * hostiles-first survives the radius law: a hostile within reach beats a
//     nearer neutral, the neutral fallback (the legal crime) survives;
//   * the grid arm and the full-scan arm agree on the same scene, and a grid
//     that LOSES the body loses the swing — the control proving that the
//     agreement above is checkable at all. Проверка «−1 роняет в полный
//     скан» снята с самим контрактом (M-261): отрицательного счёта у двери
//     больше нет, и охранять было нечего, кроме мёртвого пути.

#include "check.h"
#include "scene_objects_fixture.h"  // give_flag — биты маски (ломоть 1б)
#include "sub/targeting.h"
#include "sub/body.h"
#include "tables/npc.h"

#include <cstdio>

namespace {

using sm::NPCType;
using sm::sub::melee_pick_target;

// Тело фикстуры И ЕСТЬ СЛОТ арены (ломоть D: сущности у тел сцены нет вовсе),
// и тем же слотом оно судится: груз и возврат выбора цели — слот арены.
// Груз — САМА АРЕНА (ломоть E): реестра-посредника больше не существует.
int body(sm::sub::SubObjects& objs, NPCType type, float x, float y) {
    const int s = sm::test::give_body(objs);
    sm::test::give_pos(objs, s, x, y, 0.0f);  // место — колонка арены (ломоть 5)
    sm::test::give_pools(objs, s, sm::ecs::Pools{30, 30});
    sm::test::give_kind(objs, s,
        sm::ecs::NPCKind{std::uint16_t(type), std::uint16_t{0}});
    return s;
}

bool never_hostile(void*, int) { return false; }

void test_wide_body_is_struck_from_farther() {
    const float range = 5.0f;
    const float trollR = sm::npc_body_radius(sm::npc_def(NPCType::Troll));
    const float frogR = sm::npc_body_radius(sm::npc_def(NPCType::Frog));
    CHECK(trollR > frogR + 0.5f,
          "the fixture's premise: a troll IS much wider than a frog");
    // Both stand at the same centre distance — inside the troll's surface
    // reach, outside the frog's.
    const float d = range + (trollR + frogR) * 0.5f;

    {
        auto arena_ = sm::test::make_scene_arena();
        sm::sub::SubObjects& objs = *arena_;
        const int troll = body(objs, NPCType::Troll, d, 0.0f);
        CHECK(melee_pick_target(objs, 0, 0, 0, range,
                                &never_hostile, nullptr,
                                &sm::test::all_live_slots, &objs)
                  == troll,
              "the troll's flank is in reach at a distance its centre is not");
    }
    {
        auto arena_ = sm::test::make_scene_arena();
        sm::sub::SubObjects& objs = *arena_;
        body(objs, NPCType::Frog, d, 0.0f);
        CHECK(melee_pick_target(objs, 0, 0, 0, range,
                                &never_hostile, nullptr,
                                &sm::test::all_live_slots, &objs)
                  == -1,
              "the frog at the same distance is out of reach — the width is "
              "the whole difference");
    }
}

void test_hostiles_first_by_surface_gap() {
    auto arena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *arena_;
    const int nearFrog = body(objs, NPCType::Frog, 2.0f, 0.0f);
    const int farTroll = body(objs, NPCType::Troll, 5.5f, 0.0f);
    struct Ctx { int hostile; } ctx{farTroll};
    const auto oracle = [](void* user, int s) {
        return s == static_cast<Ctx*>(user)->hostile;
    };
    CHECK(melee_pick_target(objs, 0, 0, 0, 5.0f, oracle, &ctx,
                             &sm::test::all_live_slots, &objs) == farTroll,
          "a hostile in surface reach beats a nearer neutral");
    CHECK(melee_pick_target(objs, 0, 0, 0, 5.0f, &never_hostile, nullptr,
                             &sm::test::all_live_slots, &objs)
              == nearFrog,
          "with no hostile in reach the swing falls back to the nearest "
          "neutral — the deliberate crime stays possible");
}

// The grid arm must agree with the full scan it replaces. Проверка «а если
// сетка вернёт -1» СНЯТА вместе с самим контрактом (M-261): отрицательного
// счёта у двери больше нет в ТИПЕ, и свидетель, звавший его, охранял путь, а
// не закон (§8 п.5). Негативный контроль остаётся и усилен: сетка, ПОТЕРЯВШАЯ
// тело, обязана менять исход — иначе утверждение о паритете ничего не стоит.
void test_grid_arm_parity() {
    auto arena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *arena_;
    const int troll = body(objs, NPCType::Troll, 5.5f, 0.0f);
    body(objs, NPCType::Frog, 9.0f, 0.0f);   // out of reach either way

    struct Feed { std::uint16_t ids[8]; std::uint32_t n; };
    Feed feed{};
    feed.ids[feed.n++] = std::uint16_t(troll);
    const auto superset = [](void* user, float, float, float,
                             std::uint16_t (&out)[sm::kMaxSubObjects]) {
        auto* f = static_cast<Feed*>(user);
        for (std::uint32_t i = 0; i < f->n; ++i) out[i] = f->ids[i];
        return f->n;
    };
    // Negative control: a grid that loses the troll must NOT answer the troll.
    const auto lossy = [](void*, float, float, float,
                          std::uint16_t (&)[sm::kMaxSubObjects]) {
        return std::uint32_t{0};
    };

    const int fullScan =
        melee_pick_target(objs, 0, 0, 0, 5.0f, &never_hostile, nullptr,
                          &sm::test::all_live_slots, &objs);
    CHECK(fullScan == troll, "the reference arm reaches the troll");
    CHECK(melee_pick_target(objs, 0, 0, 0, 5.0f, &never_hostile, nullptr,
                            superset, &feed)
              == fullScan,
          "the grid arm answers exactly what the full scan answers");
    CHECK(melee_pick_target(objs, 0, 0, 0, 5.0f, &never_hostile, nullptr,
                            lossy, nullptr)
              == -1,
          "a grid that loses the body loses the swing (control: the parity "
          "assertion above can fail)");
}

void test_player_side_and_dead_are_invisible() {
    auto arena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *arena_;
    const int soldier = sm::test::give_body(objs);
    sm::test::give_pos(objs, soldier, 2.0f, 0.0f, 0.0f);
    sm::test::give_pools(objs, soldier, sm::ecs::Pools{30, 30});
    sm::test::give_kind(objs, soldier,
        sm::ecs::NPCKind{std::uint16_t(NPCType::Guard), std::uint16_t{0}});
    sm::test::give_flag(objs, soldier, sm::sub::kObjPlayerSoldier);
    const int corpse = sm::test::give_body(objs);
    sm::test::give_pos(objs, corpse, 2.5f, 0.0f, 0.0f);
    sm::test::give_pools(objs, corpse, sm::ecs::Pools{30, 30});
    sm::test::give_kind(objs, corpse,
        sm::ecs::NPCKind{std::uint16_t(NPCType::Frog), std::uint16_t{0}});
    sm::test::give_flag(objs, corpse, sm::sub::kObjDead);
    CHECK(melee_pick_target(objs, 0, 0, 0, 5.0f,
                            &never_hostile, nullptr,
                            &sm::test::all_live_slots, &objs)
              == -1,
          "the swing sees neither the player's own soldier nor a corpse");
}

} // namespace

int main() {
    test_wide_body_is_struck_from_farther();
    test_hostiles_first_by_surface_gap();
    test_grid_arm_parity();
    test_player_side_and_dead_are_invisible();
    return sm::test::report("melee_reach_test");
}
