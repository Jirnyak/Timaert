// РЕПУТАЦИЮ ДВИГАЕТ ЛЮБОЙ ВОПЛОЩЁННЫЙ АВАТАР, И ТОЛЬКО АВАТАР.
//
// Вердикты владельца 2026-11-03, дословно: «да это надо обобщить … потому что
// игрок ничем не особенен»; «любой воплощённый аватар сквада (игрок частный
// случай) будет менять репутацию в матрицы фракий (его и чужая)»; «именно
// аватаоры а не их инвентарь - ты понял?»; и об охвате — «то есть должен
// пристствовать всегда именно аватар сквада».
//
// ЧТО СУДИТСЯ. Жнец субмира (`SubworldEngine::resolve_subworld_deaths`) и путь
// удара (`SubworldEngine::push_player_hit_log`) СОСТАВЛЯЮТ поступок из ТРЁХ
// дверей, и прибор судит каждую порознь плюс их композицию:
//   1. КТО АКТОР  — `slot_macro_record@src/sub/record.h`: бэклинк `MacroOrigin`
//      несут РОВНО аватары, расписку займа — головы инвентаря;
//   2. ЧЬЁ ЗНАМЯ — `squad_faction_slot@src/bus/store.h`: ординал колонки
//      записи, и «записи нет» названо `kNoFactionSlot`;
//   3. ЧТО ЕДЕТ   — `add_faction_standing@src/macro/factions.h`: парная запись
//      по ординалам, три отказа-значения.
// Жнец приватен, поэтому прогнать его из TU нечем; зато композиция этих трёх
// дверей И ЕСТЬ закон, и каждое её звено здесь утверждено числом.
//
// ДВА ПЛЕЧА, и порознь они оба лгут. «Пара поехала» в одиночку зелено и у
// прежнего игрок-захардкоженного закона — поэтому плечо 1 утверждает ВТОРОЕ:
// пара ИГРОКА при чужом поступке НЕ едет. «Голова не двигает» в одиночку
// зелено и у дохлой расписки — поэтому плечо 2 утверждает, что лестница опыта
// ту же расписку ПРИНИМАЕТ.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ПРОГНАНО 2026-11-03, числа фактические, 30
// проверок в прогоне):
//  - M1 `add_faction_standing` пишет `kPlayerFactionSlot` вместо `actor`, то
//    есть прежний закон «платит всегда игрок» → КРАСНЫЙ, 5 из 30: «НОВЫЙ
//    ЗАКОН», «…и симметрично», «СТАРЫЙ ЗАКОН МЁРТВ», «второй павший стоит
//    столько же», «КОНТРОЛЬ ЖИВ»;
//  - M3 `squad_faction_slot` без valid-гарды (`FactionSlot(st.kind[h.slot]
//    .factionIdx)` в лоб) → КРАСНЫЙ, 2 из 30: «нет записи = kNoFactionSlot» и
//    НЕГАТИВНЫЙ КОНТРОЛЬ (ординал 0 есть ЖИВАЯ строка wildlife, и пара
//    wildlife↔timaert поехала бы от руки головы инвентаря);
//  - M4 ординальный `kill_is_no_crime` отвечает `false` на незнакомый ординал
//    вместо `true` → КРАСНЫЙ, 1 из 30: «сентинел fail-open»;
//  - M5 `slot_macro_record` принимает лестницу расписки (= `slot_squad_of`) →
//    КРАСНЫЙ, 3 из 30: «ГОЛОВА ИНВЕНТАРЯ НЕ АКТОР», негативный контроль и
//    «КОНТРОЛЬ ЖИВ»;
//  - ДОЛЖНА МОЛЧАТЬ: `kKillRepPenalty` −1 → −7 И `kHitRepPenalty` −1 → −5
//    (БАЛАНС, ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ) → ЗЕЛЁНЫЙ 30/30 без правки
//    прибора: ожидания выведены из самих констант, значит закон судит ФОРМУ,
//    а не калибровку.
//
// И ОДНА МУТАЦИЯ, ЗЕЛЁНАЯ ПО ПОСТРОЕНИЮ, — ЗАПИСАНА ИМЕННО ПОТОМУ, ЧТО
// ЗЕЛЁНАЯ (§8 п.6). Снос отказа `actor == owner` ИЗ `add_faction_standing`
// (M2) дал ЗЕЛЁНЫЙ 30/30, и снос `|| a == b` из `set_relation` (M2b) — тоже:
// у диагонального закона ДВА гейта, и они кроют друг друга, так что ни один
// по отдельности не наблюдаем. Снос ОБОИХ (M2c) → КРАСНЫЙ, 2 из 30 («сам
// себе — не отношение» и «игрок сам себе»), то есть утверждения прибора
// закон ВИДЯТ, а невидим ровно дубль гейта. Дыра здесь не в свидетеле, а в
// коде: один из двух отказов лишний (наряд требовал его в двери поступка —
// см. отчёт).
//
// ЧЕГО ЭТОТ ПРИБОР НЕ УТВЕРЖДАЕТ, И ЭТО СКАЗАНО ВСЛУХ (§8 п.7): он не гоняет
// сам жнец — `SubworldEngine::resolve_subworld_deaths` приватен, фикстуре
// недоступен, — поэтому подмена в ЖНЕЦЕ двери актора (`slot_macro_record` →
// `slot_squad_of`) прибором НЕ видна. Видна она станет, когда у жнеца
// появится прогоняемая форма; до тех пор три двери поступка утверждены
// порознь и в композиции.
//
// И ВТОРОГО он не гоняет НАРОЧНО: МАКРО-авторезолв
// (`settle_player_auto_battle@src/macro/squad.h`, переведённый на ту же
// ординальную дверь 2026-11-03 вердиктом «Обобщить: платит фракция
// сквада-участника») судится СВОИМ прибором —
// `test_the_auto_resolve_pays_with_the_participants_banner@tests/auto_resolve_speaks_test.cpp`.
// Это не дыра, а граница замыкания: здесь линкуются РОВНО три двери поступка
// (см. комментарий таргета в `CMakeLists.txt`), и притащить сюда весь
// авторезолв значило бы оплатить его сборкой в приборе, который про него не
// спрашивает.
#include "check.h"
#include "scene_objects_fixture.h"   // арена сцены фикстурам (M-150)

#include "bus/store.h"
#include "macro/factions.h"
#include "macro/squad.h"       // squad_faction_slot — ядро «чьё знамя»
#include "macro/state.h"
#include "sub/spawn.h"         // stamp_macro_debt — квитанция ТОЙ ЖЕ дверью
#include "tables/faction.h"
#include "tables/stock_key.h"

#include <cstdint>
#include <memory>

namespace {

// Три строки реестра, снятые НА СБОРКЕ (`faction_ordinal` — тот же
// единственный список, спрошенный на другом этапе). Ни одна из них не игрок:
// ровно в этом весь предмет прибора, и держит это компилятор.
constexpr sm::FactionSlot kEmpire  = sm::faction_ordinal("empire");
constexpr sm::FactionSlot kTimaert = sm::faction_ordinal("timaert");
constexpr sm::FactionSlot kBandits = sm::faction_ordinal("bandits");
static_assert(kEmpire >= 0 && kTimaert >= 0 && kBandits >= 0,
              "прибор судит ЖИВЫЕ строки реестра, а не выдуманные ординалы");
static_assert(kEmpire != sm::kPlayerFactionSlot
                  && kTimaert != sm::kPlayerFactionSlot
                  && kBandits != sm::kPlayerFactionSlot,
              "актор и жертва прибора — НЕ игрок: иначе обобщение нечем "
              "отличить от прежнего закона");
static_assert(kEmpire != kTimaert, "пара прибора обязана быть парой");

// НЕЗАВИСИМЫЙ ЯКОРЬ — СЫРОЙ БАЙТ МАТРИЦЫ, А НЕ `relation_of` (§8 п.6, шестая
// форма слепоты: сверка двух вычислений с ОДНОЙ формулой слепа к самой
// формуле — именно так 42 сверки прошли поверх живого дефекта). Поэтому ниже
// нет ни одного чтения через дверь отношений: ожидание читает память.
int raw(const sm::FactionState& f, sm::FactionSlot a, sm::FactionSlot b) {
    return int(f.rows[std::size_t(a)].rel[std::size_t(b)]);
}

// Матрица как её рождает мир: реестровые строки заняты, диагональ — верх
// шкалы, всё прочее нейтрально.
sm::FactionState born_matrix() {
    sm::FactionState f{};
    sm::claim_registry_rows(f);
    return f;
}

// Запись сквада под НЕ-игровым знаменем — ДВЕРЬЮ РОЖДЕНИЯ, как в игре
// (§8 п.11: предусловие рождает сам свидетель). Ординал выдаёт дверь.
sm::MacroHandle birth_under(sm::MacroStore& st, sm::FactionSlot faction) {
    const sm::MacroHandle h = sm::store_birth(st);
    st.kind[h.slot] = sm::ecs::NPCKind{0u, std::uint16_t(faction)};
    return h;
}

// ── ПЛЕЧО 1: ОДИН ЗАКОН ПОСТУПКА НА ВЕСЬ МИР ─────────────────────────────
void test_any_faction_pair_moves_and_the_player_stops_paying() {
    using namespace sm;
    FactionState f = born_matrix();

    CHECK(raw(f, kEmpire, kTimaert) == 0 && raw(f, kPlayerFactionSlot, kTimaert) == 0,
          "исходно обе пары нейтральны: сдвиг ниже будет ВИДЕН, а не угадан");

    add_faction_standing(f, kEmpire, kTimaert, kKillRepPenalty);

    CHECK(raw(f, kEmpire, kTimaert) == kKillRepPenalty,
          "НОВЫЙ ЗАКОН: аватар НЕ-игровой фракции, убивший члена другой, "
          "двигает ИХ пару — репутация больше не частная собственность игрока");
    CHECK(raw(f, kTimaert, kEmpire) == kKillRepPenalty,
          "…и симметрично: одна дверь пишет обе строки (невзаимного отношения "
          "народов не бывает)");
    CHECK(raw(f, kPlayerFactionSlot, kTimaert) == 0,
          "СТАРЫЙ ЗАКОН МЁРТВ: игрок НЕ платит за чужой поступок — прежде "
          "актором подставлялась его строка, чей бы аватар ни ударил");
    CHECK(raw(f, kPlayerFactionSlot, kEmpire) == 0,
          "и ни одна другая пара игрока тоже не поехала");

    // Ход копится, а не защёлкивается: цена смерти платится за КАЖДОГО.
    add_faction_standing(f, kEmpire, kTimaert, kKillRepPenalty);
    CHECK(raw(f, kEmpire, kTimaert) == 2 * kKillRepPenalty,
          "второй павший стоит столько же: дверь СДВИГАЕТ отношение, а не "
          "назначает ему значение");

    // ── ТРИ ОТКАЗА — ЗНАЧЕНИЯ, А НЕ ЗАЩИТА ───────────────────────────────
    const int before = raw(f, kEmpire, kTimaert);
    add_faction_standing(f, kEmpire, kTimaert, 0);
    CHECK(raw(f, kEmpire, kTimaert) == before,
          "нулевой сдвиг не поступок: пара не тронута");

    add_faction_standing(f, kEmpire, kEmpire, kKillRepPenalty);
    CHECK(raw(f, kEmpire, kEmpire) == kRelationMax,
          "сам себе — НЕ отношение: диагональ стоит на верху шкалы, и "
          "одержимый лорд, ударивший земляка, не ссорит фракцию с собой");

    const int bandits0 = raw(f, kBandits, kTimaert);
    add_faction_standing(f, kNoFactionSlot, kTimaert, kKillRepPenalty);
    add_faction_standing(f, kTimaert, kNoFactionSlot, kKillRepPenalty);
    add_faction_standing(f, FactionSlot(kNoFaction), kTimaert, kKillRepPenalty);
    add_faction_standing(f, kMaxFactions, kTimaert, kKillRepPenalty);
    add_faction_standing(f, kTimaert, kMaxFactions, kKillRepPenalty);
    CHECK(raw(f, kBandits, kTimaert) == bandits0
              && raw(f, kEmpire, kTimaert) == before,
          "ординал, не называющий никого (kNoFactionSlot, сентинел kNoFaction, "
          "слот за потолком) отношений не имеет — и не рассыпается в соседнюю "
          "строку матрицы");
}

// ── ИГРОК — ЧАСТНЫЙ СЛУЧАЙ ОБЩЕЙ ДВЕРИ, А НЕ ВТОРОЙ ЗАКОН ────────────────
void test_the_player_door_is_the_general_door() {
    using namespace sm;
    CHECK(kPlayerFactionSlot == faction_index(kPlayerFactionId),
          "ординал игрока, снятый НА СБОРКЕ, равен снятому в рантайме: один "
          "словарь, два этапа");

    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.factions = born_matrix();
    FactionState mirror = born_matrix();

    add_player_reputation(gs, "empire", kHitRepPenalty);
    add_faction_standing(mirror, kPlayerFactionSlot, kEmpire, kHitRepPenalty);
    CHECK(raw(gs.factions, kPlayerFactionSlot, kEmpire)
              == raw(mirror, kPlayerFactionSlot, kEmpire),
          "частный случай совпал с общим: строковая дверь игрока есть ГРАНИЦА "
          "над ординальной, а не вторая арифметика рядом");
    CHECK(raw(gs.factions, kPlayerFactionSlot, kEmpire) == kHitRepPenalty,
          "…и это именно цена удара, а не ноль с обеих сторон (сверка двух "
          "НЕ-движений тоже сошлась бы)");

    // Прежние отказы строковой двери целы, хотя ни один не живёт в ней самой.
    const int kept = raw(gs.factions, kPlayerFactionSlot, kEmpire);
    add_player_reputation(gs, nullptr, kHitRepPenalty);
    add_player_reputation(gs, "", kHitRepPenalty);
    add_player_reputation(gs, "no_such_faction", kHitRepPenalty);
    add_player_reputation(gs, "empire", 0);
    CHECK(raw(gs.factions, kPlayerFactionSlot, kEmpire) == kept,
          "пустое, незнакомое имя и нулевой сдвиг отвергнуты — отказы уехали "
          "в ординальную дверь и ни один не потерян");
    add_player_reputation(gs, kPlayerFactionId, kHitRepPenalty);
    CHECK(raw(gs.factions, kPlayerFactionSlot, kPlayerFactionSlot)
              == kRelationMax,
          "игрок сам себе — та же диагональ: strcmp с его именем заменён "
          "равенством ЧИСЕЛ, и запрет цел");
}

// ── ПЛЕЧО 2: АКТОР — АВАТАР, А НЕ ГОЛОВА ЕГО ИНВЕНТАРЯ ───────────────────
void test_the_actor_is_the_avatar_never_its_inventory() {
    using namespace sm;
    auto store = make_macro_store();
    MacroStore& st = *store;
    auto arena = test::make_scene_arena();
    sub::SubObjects& objs = *arena;

    const MacroHandle lord = birth_under(st, kEmpire);
    CHECK(st.valid(lord) && lord.id != 0,
          "запись-актор родилась и назвала свой ординал");
    CHECK(squad_faction_slot(st, lord) == kEmpire,
          "её знамя читается ОРДИНАЛОМ колонки, без имени посередине");

    // ТЕЛО-АВАТАР: бэклинк, ровно как ставит дверь tracked-рождения
    // (`sub/spawn.cpp`) и дверь входа игрока (`spawn_player_entity`).
    const int avatarBody = test::give_body(objs);
    test::give_origin(objs, avatarBody, lord);
    test::make_avatar(objs, avatarBody);

    // ГОЛОВА ИНВЕНТАРЯ: расписка займа ТОГО ЖЕ сквада и НИ ОДНОГО бэклинка —
    // производное тело, «одно из многих, сделанное видимым».
    const int head = test::give_body(objs);
    CHECK(avatarBody >= 0 && head >= 0 && avatarBody != head,
          "арена выдала два РАЗНЫХ слота: оба тела существуют");
    sub::stamp_macro_debt(objs, head, MacroStock::Creatures,
                          MacroStockKey{std::int32_t(st.spawnId[lord.slot].index),
                                        0, 0, -1, 0, 1});

    CHECK(sub::slot_macro_record(objs, avatarBody, st) == lord,
          "АВАТАР НАЗЫВАЕТ СВОЮ ЗАПИСЬ: дверь бэклинка отдаёт того самого "
          "лорда, чьей проекцией стоит тело");
    CHECK(sub::slot_macro_record(objs, sub::avatar_slot(objs), st) == lord,
          "…и ровно это выражение берёт путь удара: актором едет запись "
          "активного тела сцены, кем бы она ни была");

    CHECK(sub::slot_macro_record(objs, head, st).id == 0,
          "ГОЛОВА ИНВЕНТАРЯ НЕ АКТОР: бэклинка у неё нет, значит «чей это "
          "аватар» отвечается «ничей» — «именно аватаоры а не их инвентарь»");
    CHECK(sub::slot_squad_of(objs, head, st) == lord,
          "а ДЕТЕКТОР НЕ ПУСТОЙ: расписка настоящая — лестница опыта её "
          "принимает и качает тот же сквад; две двери, два разных вопроса");

    // ЛОВУШКА НУЛЯ НАЗВАНА И ЗАКРЫТА. Сперва — что она РЕАЛЬНА: нулевой
    // factionIdx есть ЖИВАЯ строка реестра, а не «никого».
    CHECK(squad_kind(st, MacroHandle{}).factionIdx == 0
              && faction_def_by_index(0) != nullptr,
          "ловушка реальна: `squad_kind` на пустом хэндле отдаёт factionIdx 0, "
          "и ноль реестра — ЖИВАЯ строка, а не сентинел");
    CHECK(squad_faction_slot(st, MacroHandle{}) == kNoFactionSlot,
          "поэтому дверь знамени отвечает «записи нет» НАЗВАННЫМ сентинелом, "
          "а не чужим знаменем");

    // КОМПОЗИЦИЯ — та самая, что складывает жнец: голова → пустая запись →
    // kNoFactionSlot → пара не едет.
    FactionState f = born_matrix();
    const MacroHandle headActor = sub::slot_macro_record(objs, head, st);
    add_faction_standing(f, squad_faction_slot(st, headActor), kTimaert,
                         kKillRepPenalty);
    CHECK(raw(f, kEmpire, kTimaert) == 0 && raw(f, 0, kTimaert) == 0,
          "НЕГАТИВНЫЙ КОНТРОЛЬ: убийство рукой головы инвентаря не двигает НИ "
          "пару её сквада, НИ пару нулевой строки реестра");

    // Та же композиция от АВАТАРА обязана поехать — иначе контроль выше
    // доказывал бы лишь, что в этом приборе не едет ничто.
    const MacroHandle avatarActor =
        sub::slot_macro_record(objs, avatarBody, st);
    add_faction_standing(f, squad_faction_slot(st, avatarActor), kTimaert,
                         kKillRepPenalty);
    CHECK(raw(f, kEmpire, kTimaert) == kKillRepPenalty,
          "КОНТРОЛЬ ЖИВ: та же композиция от АВАТАРА двигает пару его фракции "
          "— разница ровно в роде тела, а не в том, что прибор нем");
}

// ── ГЕЙТ ПРЕСТУПЛЕНИЯ — КОЛОНКА СТРОКИ ЖЕРТВЫ, СПРОШЕННАЯ ЧИСЛОМ ─────────
void test_the_crime_gate_is_a_column_asked_by_ordinal() {
    using namespace sm;
    int rows = 0, disagree = 0, lawless = 0, lawful = 0;
    for (int i = 0; i < kFactionCount; ++i) {
        ++rows;
        const bool byOrdinal = kill_is_no_crime(std::uint16_t(i));
        if (byOrdinal != kill_is_no_crime(kFactionDefs[i].id)) ++disagree;
        // НЕЗАВИСИМЫЙ ИСТОЧНИК — сама колонка строки, не вторая дверь.
        if (byOrdinal != kFactionDefs[i].killIsNoCrime) ++disagree;
        if (kFactionDefs[i].killIsNoCrime) ++lawless; else ++lawful;
    }
    CHECK(rows == kFactionCount, "каждая строка реестра была спрошена");
    CHECK(disagree == 0,
          "ординальная форма гейта отвечает ровно то, что КОЛОНКА строки, и то "
          "же, что строковая граница: один закон, две формы");
    CHECK(lawless > 0 && lawful > 0,
          "прибор НЕ вакуумный: мир правда держит и беззаконные строки, и "
          "те, чья смерть есть преступление");

    CHECK(kill_is_no_crime(kNoFaction)
              && kill_is_no_crime(std::uint16_t(kFactionCount))
              && kill_is_no_crime(std::uint16_t(kMaxFactions)),
          "сентинел и всякий байт за реестром — fail-open: тело, чьей фракции "
          "реестр не знает, есть никто, а смерть никого никого не касается");
    CHECK(!kill_is_no_crime(std::uint16_t(kEmpire)),
          "а смерть подданного державы мир помнит — иначе гейт съедал бы "
          "весь закон молча");
}

} // namespace

int main() {
    test_any_faction_pair_moves_and_the_player_stops_paying();
    test_the_player_door_is_the_general_door();
    test_the_actor_is_the_avatar_never_its_inventory();
    test_the_crime_gate_is_a_column_asked_by_ordinal();
    return sm::test::report("avatar_standing_test");
}
