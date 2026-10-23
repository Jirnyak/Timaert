#include "check.h"
#include "macro/deal.h"
#include "scene_objects_fixture.h"  // арена: флаг солдата — бит маски
#include "tables/faction.h"
#include "tables/npc.h"
#include "macro/world_row.h"
#include "ecs/components.h"
#include "sub/ai.h"
#include "sub/spawn.h"
#include "macro/store.h"
#include "macro/macro_stock.h"  // займ контейнера — носитель идентичности солдата

#include <cstdio>
#include <vector>

namespace {

struct FixedRng {
    float values[8] = {0.10f, 0.72f, 0.92f, 0.40f,
                       0.20f, 0.82f, 0.55f, 0.05f};
    int idx = 0;
    float operator()() {
        const float v = values[idx % 8];
        ++idx;
        return v;
    }
};

} // namespace

int main() {
    const sm::SoldierRecord lowLevel = sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Peasant), -400, 7u);
    const sm::SoldierRecord highLevel = sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1000000, 8u);
    const bool ok1 = !(lowLevel.level != 1
                       || highLevel.level != sm::kMaxSoldierLevel
                       || sm::soldier_level_factor(1000000) <= 0);
    CHECK(ok1, "soldier level normalization failed");
    if (!ok1) return 1;

    // Единый контейнер (M-71): армия и гарнизон — области существ. Найм —
    // ОДНА сделка дверью deal (ломоть 2 M-72): hire_npc со скретч-кошельком
    // мёртв; ЦЕНА — закон звонящего (вердикт п.3), и что цена головы ЕСТЬ
    // hire_price_for — закон цены, его держит barter_test. Здесь охраняется
    // ПЕРЕЕЗД КОНКРЕТНОЙ ЗАПИСИ: именная душа едет собой, старейшая первой.
    auto hireStore_ = sm::make_macro_store();
    sm::Inventory& player = hireStore_->inventory[1].inv;
    sm::Inventory& garrison = hireStore_->inventory[2].inv;
    sm::creatures_push(garrison, sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 3, 42u));
    sm::creatures_push(garrison, sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Peasant), 1, 43u));

    const int expectedPrice =
        sm::hire_price_for(std::uint16_t(sm::NPCType::Guard), 3);
    sm::DealLine recruit[1];
    recruit[0] = sm::DealLine{};
    recruit[0].want = 1;
    recruit[0].def = sm::world_row_of_creature(sm::NPCType::Guard);
    recruit[0].exact = 1;
    const sm::DealFact hired_ = sm::deal(*hireStore_, 1, 2, {}, recruit);
    const bool ok2 = hired_.settled && hired_.unitsBA == 1
                     && recruit[0].done == 1;
    CHECK(ok2, "the recruit line settles with the fact of one head");
    if (!ok2) return 1;
    const sm::ItemRef* hired = &player.slots[sm::kMaxInventorySlots - 1];
    const bool ok3 = !(sm::creature_count(player) != 1
                       || sm::creature_of_world_row(hired->def) != sm::NPCType::Guard
                       || hired->entityId != 42u || sm::creature_count(garrison) != 1);
    CHECK(ok3, "the deal did not move the concrete soldier record");
    if (!ok3) return 1;

    // Строка рода, которого в гарнизоне нет, не везёт НИЧЕГО — «нанять
    // отсутствующего» невыразимо фактом (0), а не веткой найма.
    sm::DealLine absent[1];
    absent[0] = sm::DealLine{};
    absent[0].want = 1;
    absent[0].def = sm::world_row_of_creature(sm::NPCType::Merchant);
    const sm::DealFact denied = sm::deal(*hireStore_, 1, 2, {}, absent);
    const bool ok4 = !(denied.settled || sm::creature_count(player) != 1
                       || sm::creature_count(garrison) != 1);
    CHECK(ok4, "an absent row ships nothing");
    if (!ok4) return 1;

    sm::Inventory duplicateIds{};
    sm::creatures_push(duplicateIds, sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 3, 77u));
    sm::creatures_push(duplicateIds, sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 4, 77u));
    sm::creatures_push(duplicateIds, sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Peasant), 1, 78u));
    int with77 = 0;
    for (const sm::CreatureRef h : sm::creatures_range(duplicateIds)) {
        if (h.entityId == 77u) ++with77;
    }
    const bool ok5 = !(with77 != 2);
    CHECK(ok5, "fixture sanity: two souls share id 77");
    if (!ok5) return 1;
    const bool ok6 = !(!sm::creatures_remove_one_by_entity_id(duplicateIds, 77u)
                       || sm::creature_count(duplicateIds) != 2
                       || sm::creatures_remove_one_by_entity_id(duplicateIds, 999u));
    CHECK(ok6, "soldier death removal did not remove exactly one record");
    if (!ok6) return 1;
    with77 = 0;
    for (const sm::CreatureRef h : sm::creatures_range(duplicateIds)) {
        if (h.entityId == 77u) ++with77;
    }
    const bool ok7 = !(with77 != 1);
    CHECK(ok7, "exactly one of the two id-77 souls must remain");
    if (!ok7) return 1;

    // (garrison_soldier_id_base died with §42 Инк 7 — garrison souls draw
    // from THE one macro ordinal issuer now, like every other soldier.)
    const bool ok8 =
        !(sm::npc_hire_price_base(sm::NPCType::Guard) != expectedPrice);
    CHECK(ok8, "preview hire price is unstable");
    if (!ok8) return 1;

    // Upkeep is MAINTENANCE, not a deal (owner 2026-09-17, CANON S14/S19.2):
    // the CHA discount died as a player-special — the law is the plain sum
    // of the soldier rows' own prices, whoever's creatures it is.
    const int baseUpkeep = sm::calculate_squad_upkeep(player);
    int rowSum = 0;
    for (const sm::CreatureRef h : sm::creatures_range(player))
        rowSum += sm::soldier_upkeep(h.kind, h.level);
    const bool ok9 = !(baseUpkeep <= 0 || baseUpkeep != rowSum);
    CHECK(ok9, "squad upkeep must be the plain soldier-row sum");
    if (!ok9) return 1;

    // ПАСТВА ВСТАЁТ В КОНТЕЙНЕР ОДНОЙ СТРОКОЙ (2026-09-22). Здесь проверялся
    // `generate_garrison` с броском монетки на каждую душу (60 % Guard /
    // 40 % Peasant); он вырезан вместе со стражей, и состав перестал быть
    // жребием — значит `FixedRng` этой проверке больше не нужен вовсе.
    sm::Inventory flock{};
    const int raised = sm::raise_flock_into_container(flock, 9);
    const bool ok10 = !(raised != 9 || sm::creature_count(flock) != 9);
    CHECK(ok10, "паства не встала в контейнер полностью");
    if (!ok10) return 1;
    // ОДИН РОД — ОДИН СЛОТ: генерики стоят стопкой (CANON S4), поэтому
    // девять душ это ОДНА строка области, а не девять.
    const bool ok11 = !(sm::creature_slot_count(flock) != 1);
    CHECK(ok11, "девять крестьян обязаны стоять одним стаком");
    if (!ok11) return 1;
    {
        const sm::ItemRef& s0 =
            flock.slots[sm::kMaxInventorySlots - 1];
        const bool ok12 = !(sm::creature_of_world_row(s0.def) != sm::NPCType::Peasant
                            || s0.entityId != 0 || s0.count != 9);
        CHECK(ok12, "в контейнере места стоят только крестьяне-генерики");
        if (!ok12) return 1;
    }

    const bool ok13 = !(sm::npc_xp_reward(sm::NPCType::Guard, 4)
                        <= sm::npc_xp_reward(sm::NPCType::Guard, 1));
    CHECK(ok13, "NPC XP reward does not scale by level");
    if (!ok13) return 1;

    // (Проверка self-append умерла с creatures_add в ломоть 2 M-72: копия
    // области минтит головы и дверью мира не является; закон слияния
    // генерик-стаков держит add_ref и ok11/ok12 выше.)

    auto worldStore_ = sm::make_macro_store();
    // Арена сцены — предусловие рождения тел (§8 п.11); 38.3 МиБ, потому
    // фабрика-uptr. Store едет в tick_npc_ai ЯВНЫМ аргументом (ломоть E).
    auto worldArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *worldArena_;   // слоты телам — иначе бит некуда класть
    std::vector<std::uint8_t> emptyTiles;
    const std::uint16_t playerFaction =
        std::uint16_t(sm::faction_index(sm::kPlayerFactionId));
    sm::sub::spawn_player_squad(objs, player, emptyTiles, 512.0f, 512.0f, 99u,
                                playerFaction);
    int projected = 0;
    // СВИП ПЛОСКОЙ АРЕНЫ (ломоть D): жильца сцены называет бит kObjAlive, а
    // не вьюха по ObjectSlot. Своей строки отсева снарядов цикл не требует —
    // их отсекают ТЕ ЖЕ колонки тел, что и прежде (у снарядного слота бары
    // maxHp 0, лист без бита kObjHasCombat, квитанция amount 0).
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        // Бары и лист — колонки арены (кусок 2): полнота тела спрашивается
        // дверьми, не составом entt-компонент.
        if (sm::sub::slot_pools(objs, s) == nullptr
            || sm::sub::slot_combat(objs, s) == nullptr) continue;
        const auto* loanP = sm::sub::slot_debt(objs, s);
        if (loanP == nullptr) continue;
        const auto& loan = *loanP;
        if (loan.stock != std::uint8_t(sm::MacroStock::Creatures)) continue;
        ++projected;
        const bool ok15 =
            !(!sm::sub::slot_flag(objs, s, sm::sub::kObjPlayerSoldier));
        CHECK(ok15, "a projected soldier lacks the player-side bit");
        if (!ok15) return 1;
        const auto* kind = sm::sub::slot_kind(objs, s);
        const auto* aiCol = sm::sub::slot_ai(objs, s);
        const bool ok16 = !(aiCol == nullptr);
        CHECK(ok16, "soldier body lost its brain");
        if (!ok16) return 1;
        const auto& ai = *aiCol;
        const bool ok17 = !(loan.detail != 42
                            || loan.detailKind != std::uint16_t(sm::NPCType::Guard)
                            || kind == nullptr
                            || kind->type != std::uint16_t(sm::NPCType::Guard)
                            || ai.kind != sm::ecs::SubworldAi::Combat);
        CHECK(ok17, "subworld projection lost soldier identity or AI contract");
        if (!ok17) return 1;
        // A squad member fights for whoever raised the squad, whatever his kind:
        // the faction is the OWNER's, and it is real data on the body — not a
        // tag the battle pass has to special-case. Anything else (the old
        // hardcoded "empire") would make your own troops foreign subjects.
        const bool ok18 = !(kind->factionIdx != playerFaction);
        CHECK(ok18, "squad member does not wear its owner's faction");
        if (!ok18) return 1;
    }
    const bool ok19 = !(projected != 1);
    CHECK(ok19,
          "subworld projection did not create exactly one soldier entity");
    if (!ok19) return 1;

    // Вторая сцена — ВТОРАЯ арена (кусок 1, §8 п.11: предусловие рождает сам
    // свидетель), иначе тела кривого прогона считались бы вместе с первым.
    // Store ей не нужен: дверь спавна его не просит.
    auto malformedArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& malformedObjs = *malformedArena_;
    std::vector<std::uint8_t> malformedTiles(1, 0u);
    sm::sub::spawn_player_squad(malformedObjs, player, malformedTiles,
                                512.0f, 512.0f, 100u, playerFaction);
    int malformedProjected = 0;
    // Тот же свип арены (ломоть D): квитанция — колонка слота, и «квитанции
    // нет» = amount 0, поэтому снарядный слот сюда не попадает.
    for (int ms = 0; ms < int(sm::kMaxSubObjects); ++ms) {
        if ((malformedObjs.flags[std::size_t(ms)] & sm::sub::kObjAlive) == 0u)
            continue;
        const auto* d = sm::sub::slot_debt(malformedObjs, ms);
        if (d != nullptr
            && d->stock == std::uint8_t(sm::MacroStock::Creatures))
            ++malformedProjected;
    }
    const bool ok20 = !(malformedProjected != 1);
    CHECK(ok20, "malformed tile buffer blocked squad projection");
    if (!ok20) return 1;

    const int hostile = sm::test::give_body(objs);
    sm::test::give_pos(objs, hostile, 100.0f, 100.0f, 0.0f);
    sm::test::give_combat(objs, hostile, sm::ecs::Combat{
        sm::Dice{5, 1}, std::int16_t(0), std::int16_t(100),
        std::uint8_t(0), std::uint8_t(sm::DamageType::Blunt),
        20.0f, 3.0f, 1.0f, 0u, sm::ecs::Combat::Melee, /*armorSteps*/std::uint16_t{0}});
    sm::test::give_ai(objs, hostile,
                      sm::ecs::SubworldAi{sm::ecs::SubworldAi::Combat,
                                          0.0f, 4.0f, 4.0f, 8.0f, 1.0f});
    sm::sub::tick_npc_ai(objs, worldStore_.get(), 140.0f, 100.0f, 0.0f, 0.5f);
    const auto& combatPos = *sm::sub::slot_pos(objs, hostile);
    const auto& combatAi = *sm::sub::slot_ai(objs, hostile);
    // Ownership contract, TIGHTENED. tick_npc_ai must not touch a combat body at
    // ALL — neither its position nor its velocity. The mass-battle pass
    // (sub/movement.h) owns both, and it READS the stored velocity as the body's
    // current momentum to apply an acceleration limit. The previous expectation
    // here was that tick_npc_ai zeroed vx/vy; that is incompatible with carrying
    // momentum across frames (a body restarted from rest every tick is capped at
    // one acceleration step, i.e. ~2 units/s instead of its real 35), so the
    // zeroing is gone and the assertion is now "left strictly untouched".
    const bool ok21 = !(combatPos.x != 100.0f || combatPos.y != 100.0f
                        || combatAi.vx != 4.0f || combatAi.vy != 4.0f);
    CHECK(ok21, "tick_npc_ai must leave ecs::Combat actors untouched");
    if (!ok21) return 1;

    const int fallback = sm::test::give_body(objs);
    sm::test::give_pos(objs, fallback, 100.0f, 100.0f, 0.0f);
    sm::test::give_ai(objs, fallback,
                      sm::ecs::SubworldAi{sm::ecs::SubworldAi::Combat,
                                          0.0f, 0.0f, 0.0f, 8.0f, 1.0f});
    sm::sub::tick_npc_ai(objs, worldStore_.get(), 140.0f, 100.0f, 0.0f, 0.5f);
    const auto& fallbackPos = *sm::sub::slot_pos(objs, fallback);
    // A body with combat AI but NO ecs::Combat has no damage, no speed and no
    // reach — it cannot fight. The old code still walked it at the player
    // scalar ("degrade to chase-only movement"), which was a hidden global
    // player attractor and, in shipping, unreachable: every spawn path that
    // emplaces SubworldAi::Combat also emplaces ecs::Combat (sub/spawn.cpp
    // fauna + squad projection). That path is deleted, so such a body is now
    // inert — nothing invents stats for it.
    const bool ok22 = !(fallbackPos.x != 100.0f || fallbackPos.y != 100.0f);
    CHECK(ok22, "statless combat-AI body must be inert, not chase the player");
    if (!ok22) return 1;

    // ── The brain writes INTENT, never a position ──────────────────────────
    // One owner of legs: tick_npc_ai for a Wander mind decides wantVx/Vy and
    // must leave Position strictly alone (the battle steering pass executes).
    // And a STANDING mind must still be able to change it: the decision seed
    // folds in SubworldAi.seq, because entity⊕position alone froze a standing
    // body — its position stopped changing, so it re-rolled the same "stand"
    // forever, and deer petrified on their first idle roll.
    const int wanderer = sm::test::give_body(objs);
    sm::test::give_pos(objs, wanderer, 200.0f, 200.0f, 0.0f);
    sm::test::give_ai(objs, wanderer,
                      sm::ecs::SubworldAi{sm::ecs::SubworldAi::Wander,
                                          0.0f, 0.0f, 0.0f, 8.0f, 0.6f});
    bool decidedToWalk = false;
    bool decidedToStand = false;
    for (int i = 0; i < 400; ++i) {
        sm::sub::tick_npc_ai(objs, worldStore_.get(), 140.0f, 100.0f, 0.0f, 0.5f);
        const auto& wai = *sm::sub::slot_ai(objs, wanderer);
        if (wai.wantVx != 0.0f || wai.wantVy != 0.0f) decidedToWalk = true;
        else decidedToStand = true;
    }
    const auto& wpos = *sm::sub::slot_pos(objs, wanderer);
    const bool ok23 = !(wpos.x != 200.0f || wpos.y != 200.0f);
    CHECK(ok23, "a wander brain must never write Position");
    if (!ok23) return 1;
    const bool ok24 = !(!decidedToWalk || !decidedToStand);
    CHECK(ok24, "a standing mind stopped deciding (seq missing from seed)");
    if (!ok24) return 1;

    std::printf("OK combat_squad_test hired=%d garrison=%d upkeep=%d generated=%d projected=%d malformed_tiles=%d ai_owner=1 unique_ids=1\n",
                sm::creature_count(player), sm::creature_count(garrison),
                baseUpkeep, sm::creature_count(flock),
                projected, malformedProjected);
    return sm::test::report("combat_squad_test");
}
