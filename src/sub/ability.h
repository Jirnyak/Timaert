// В СУБМИРЕ ВСЁ — СПОСОБНОСТЬ, И У ВСЕГО ЕСТЬ РЕКАВЕРИ.
//
// Вердикт владельца 2026-09-19, дословно: «использование предметов тоже
// должно быть рекавери но не костыльно а через единую систему… у нас есть
// атака/спелл/использование — очевидно что это как „способность“ причём
// очень мощная… мы так реально сможем добавлять способности как в играх
// tome4 и тд… надо сделать единую систему и записать в канон что у нас в
// субмире всё через способности и поэтому у всего сразу есть recovery
// (0 рековери частный случай)». CANON S13.
//
// ЧТО ЭТО ЗА ФАЙЛ. Гейт занятости тела (`ecs::Combat::recoverySteps`) —
// ОДИН с 2026-09-09, и это было записано в канон. А вот СПРАШИВАЛИ его и
// ЗАРЯЖАЛИ по-разному в каждом месте: удар писал
// `steps_from_seconds(cooldown)` (секунды, уже поделённые строкой через
// project_combat), каст — `recovery_steps(row, attrs, skills, Spellcraft)`
// (база строки, поделённая здесь и сейчас), а зелья, интеракции и добыча не
// спрашивали его вовсе. Один гейт с тремя манерами обращения — это и есть
// «второй словарь» S26, просто спрятанный в глаголах, а не в данных: третий
// автор списал бы не у того соседа, и никто бы не заметил.
//
// ПОЭТОМУ ЗДЕСЬ ОДНА ДВЕРЬ, А НЕ ОДНА ТАБЛИЦА (вердикт формы, 2026-09-19):
// спелл-ряд, оружейный ряд и ряд предмета ОСТАЮТСЯ каждый у себя — никаких
// переездов ординалов, никакого бампа сейва, — но всякое действие субмира
// проходит через `try_ability` и платит по ОДНОМУ закону. Добавить
// способность (предмета, перка, ToME-образного активного умения) — значит
// назвать её базу и домен, а не написать четвёртую манеру.
#pragma once

#include "ecs/components.h"      // ecs::Combat — тот самый гейт
#include "macro/anketa.h"    // recovery_steps — дверь восстановления S14
#include "sub/objects.h"     // SubObjects — лист стал колонкой (кусок 2)
#include "sub/record.h"      // body_visual — колонка визуальной позиции (кусок 3)

#include <cmath>             // sqrt — интерполятор ниже

namespace sm::sub {

// СВОБОДНА ЛИ РУКА. Ноль шагов = тело ничем не занято. Тело без боевой
// строки (реквизит, безоружная фикстура теста) свободно всегда — это
// предельный случай закона, а не исключение из него.
inline bool body_is_free(const ecs::Combat* gate) {
    return gate == nullptr || gate->recoverySteps == 0u;
}

// СЛИВ ОБОИХ ЧАСОВ ЗАКОНА ВОССТАНОВЛЕНИЯ — одним проходом по КОЛОНКЕ
// (бывший ecs::sys::tick_combat_recovery; лист — колонка арены с куска 2,
// первый чистый DOD-проход субмира: ни сущностей, ни вьюх). Шаги приходят
// от МИРА (целый квант core/time.h, не float-секунды): в застое пошагового
// режима ни рука, ни броня не встают — встают ровно на тиках, которые
// игрок купил действием. Второй субъект (armorSteps) сливается Тем же
// шагом — отдельный проход был бы вторым законом восстановления (M-194).
// Мёртвые тела включены нарочно, как и прежде: слот жив, часы дотекают.
inline void tick_body_recovery(SubObjects& objs, std::uint32_t steps) {
    if (steps == 0u) return;
    for (int s = 0; s < kMaxBodyCrowd; ++s) {
        const std::uint16_t f = objs.flags[std::size_t(s)];
        if ((f & kObjAlive) == 0u || (f & kObjHasCombat) == 0u) continue;
        ecs::Combat& c = objs.combat[std::size_t(s)];
        c.recoverySteps = c.recoverySteps > steps ? c.recoverySteps - steps : 0u;
        c.armorSteps = c.armorSteps > steps
                           ? std::uint16_t(c.armorSteps - steps) : std::uint16_t{0};
    }
}

// ИНТЕРПОЛЯТОР СЦЕНЫ (бывший ecs::sys::tick_visual_interp; визуальная
// позиция — колонка арены с куска 3). Пока Position — компонента (умирает
// ломтями 4-5), проход ходит по сущностям; нулевая скорость колонки =
// интерполятор стоит, это значение, а не отсутствие.
template <class Registry>
inline void tick_body_visual_interp(Registry& reg, float dt) {
    auto view = reg.template view<ecs::Position, ecs::SubworldTag>();
    for (auto e : view) {
        ecs::VisualPos* v = body_visual(reg, e);
        if (v == nullptr || v->speed <= 0.0f) continue;
        const auto& p = view.template get<ecs::Position>(e);
        const float dx = p.x - v->vx, dy = p.y - v->vy;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < 0.001f) continue;
        const float step = v->speed * dt;
        if (step >= d) { v->vx = p.x; v->vy = p.y; }
        else { v->vx += dx / d * step; v->vy += dy / d * step; }
    }
}

// ЗАНЯТЬ РУКУ на длину, которую называет СТРОКА действия.
//
// `baseSeconds` — колонка строки (спелла, предмета, действия), а делится она
// на природную резвость (Spd-асимптота) и на генерик своего домена — ту же
// `recovery_steps`, которой платят удар и каст (S14 «один рычаг на ручку»).
// Генерик — это домен, а не вкус: руки (Армсмастер) у удара, выстрела,
// глотка и рычага; касты (Спеллкрафт) у магии.
//
// НОЛЬ — ЧЕСТНЫЙ ЧАСТНЫЙ СЛУЧАЙ, и он проходит здесь насквозь: строка с
// базой 0 не занимает тело вовсе (мир в пошаговом режиме не сдвинется), и
// это законно — так объявляются действия, которые времени не стоят.
// Ненулевая база всегда стоит хотя бы квант времени: пол в один шаг ставит
// сама `recovery_steps`, здесь его не переписывают.
inline void charge_ability(ecs::Combat* gate, float baseSeconds,
                           const Attributes& attributes, const Skills& skills,
                           SkillId generic) {
    if (gate == nullptr) return;
    if (baseSeconds <= 0.0f) return;           // ноль — значит ноль
    gate->recoverySteps = std::uint32_t(
        recovery_steps(baseSeconds, attributes, skills, generic));
}

// ТА САМАЯ ОДНА ДВЕРЬ: спросить гейт и, если рука свободна, занять её.
//
// Возвращает false РОВНО тогда, когда тело занято — и это единственная
// причина отказа, которую знает закон способностей. Всё остальное (хватает
// ли маны, есть ли что под прицелом, лежит ли зелье в сумке) — дело самой
// способности, и спрашивается ДО этой двери: отказ по своей причине не
// должен жечь рекавери.
inline bool try_ability(ecs::Combat* gate, float baseSeconds,
                        const Attributes& attributes, const Skills& skills,
                        SkillId generic) {
    if (!body_is_free(gate)) return false;
    charge_ability(gate, baseSeconds, attributes, skills, generic);
    return true;
}

// ДЛИНА ОДНОЙ РУБКИ. Не выдумана: у добычи уже ЕСТЬ свой темп — колонка
// `kGatherPerWorkerDay` (объектов за рабочий день, CANON S14.1 «темп —
// КОЛОНКА, а не формула»), и цена SP одного объекта уже считается из неё.
// Время берётся оттуда же: замах топора — это та же работа, что платит бар,
// и авторской секунды рядом с существующим темпом не заводится. Взмах руки
// (kHandSwingS) — нижняя граница: рубить быстрее, чем махать, нельзя.
inline constexpr float kHarvestActSeconds = 1.5f;
static_assert(kHarvestActSeconds >= 1.5f,
              "рубка не быстрее пустого замаха (kHandSwingS): у топора есть "
              "масса, и она не бывает меньше нуля");

} // namespace sm::sub
