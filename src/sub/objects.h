// SubObjects — ЕДИНЫЙ МАССИВ ОБЪЕКТОВ СЦЕНЫ (M-150; вердикты владельца
// 2026-10-05: «общий кап ОБЪЕКТОВ субмира», «арена — агностичный 3д куб, в
// котором агностичные объекты»).
//
// ЭТО ПОСТОЯННЫЙ НОСИТЕЛЬ, НЕ ПЕР-ТИКОВЫЙ СБОР: BodyCrowd (movement.h) и
// сетки пересобираются каждый драйв и индексов не хранят — здесь же слот
// живёт столько, сколько живёт объект, зануляется и переиспользуется
// (ЗАКОН ГЛАДКОЙ ПАМЯТИ: род — колонка/маска, пустота оплачена).
//
// ССЫЛКА НА ОБЪЕКТ — {СЛОТ, ID}: ЕДИНОЕ ПРАВИЛО ОБОИХ МИРОВ (вердикт
// владельца 2026-10-05, дословно: «можно хранить не только номер слота
// потому что это опасно а ещё ИНДЕКС … ID то есть не индекс а ID и тогда
// тривиально мой дом в слоте айди такое слот такое - проверка ой там уже
// не то айди - дома нет»; «если можно единым правилом одним как можно
// больше то это лучше если в субмире и для макромира будет одинаковая
// система то это хорошо»). Слот — ТОЛЬКО адрес O(1), ID — ТОЛЬКО
// идентичность: растущий счётчик рождений ЭТОГО стора, эмиссия с 1,
// 0 навсегда «никто» (ЗАКОН НУЛЯ-ОРДИНАЛА). Ссылка валидна, пока ID в
// слоте совпадает с её ID; протухшая отваливается сама. Пер-слотного
// поколения-суррогата НЕТ — у макро ту же роль играет spawnId (M-220
// убивает колонку generation ровно этим правилом).
//
// КАП: пока kMaxBodyCrowd (16384) — тела и есть первые жители массива;
// ломоть 6 меняет его ОДНИМ именем на kMaxSubObjects = 65536 (2^16 =
// мощность u16-индекса, «решить на века», вердикт 2026-10-05).
//
// МИГРАЦИЯ (ломти 1а..7): колонки заезжают сюда из entt-компонент
// поштучно; мост objects_attach/objects_of — тот же ctx-приём, что у
// MacroStore, и умирает вместе с реестром (ломоть 7). Слот объекта на
// теле — транзитный компонент ecs::ObjectSlot; его снимет смерть реестра.
#pragma once

#include <array>
#include <cstdint>

#include "ecs/components.h" // ecs::NPCKind — тип колонки kind (кусок 1)
#include "sub/movement.h"   // kMaxBodyCrowd — кап до ломтя 6

namespace sm::sub {

// ── Маска рода и состояния объекта ──────────────────────────────────────
// Бит = бывший entt-тег; «живой слот» — отдельный бит, потому что ID
// хранит идентичность, а не занятость (слот умершего жив трупом, ломоть 5).
inline constexpr std::uint16_t kObjAlive = 1u << 0;
// Теги тел, умершие компонентами в ломте 1б (2026-10-05):
inline constexpr std::uint16_t kObjDead          = 1u << 1; // бывший ecs::Dead
inline constexpr std::uint16_t kObjPlayerSoldier = 1u << 2; // PlayerSoldierTag
inline constexpr std::uint16_t kObjTempHostile   = 1u << 3; // TempHostileToPlayer
inline constexpr std::uint16_t kObjFlying        = 1u << 4; // Flying
// «Несёт ли боевой лист» (кусок 2): у листа естественного нуля НЕТ — пустой
// лист есть ЗАКОННОЕ значение (ЗАКОН АНКЕТЫ п.4: сквад без личности), и
// сентинел в любом его поле был бы числом внутри области значений. Бит
// ставит ТОЛЬКО дверь set_body_combat (record.h) — рассинхрон бита и
// колонки невыразим второй дверью записи.
inline constexpr std::uint16_t kObjHasCombat     = 1u << 5;
// Кусок 3 — мозг и два ЛЕНИВЫХ состояния (бывшие компоненты-присутствия):
// у мозга нуля-сентинела нет (Wander = 0 — законный род), бит ставит
// дверь set_body_ai; «идёт домой» и «в воздухе» — состояния, что прежде
// выражались самим наличием компоненты (GoingHome / Airborne), теперь —
// бит + колонка, ставят/снимают только двери record.h.
inline constexpr std::uint16_t kObjHasAi         = 1u << 6;
inline constexpr std::uint16_t kObjGoingHome     = 1u << 7;
inline constexpr std::uint16_t kObjAirborne      = 1u << 8;

// Событие «в этом тике по телу попали» (колонка damageFx) — биты:
inline constexpr std::uint8_t kDmgFxPending = 1u << 0;
inline constexpr std::uint8_t kDmgFxLethal  = 1u << 1;
inline constexpr std::uint8_t kDmgFxBlocked = 1u << 2;

// «Никто не бил» у lastHitBy: ПОСЛЕДНЕЕ значение типа (ЗАКОН УЗКОГО
// ИНДЕКСА — ноль здесь ЗАКОННЫЙ ид), и оно же integral entt-null на время
// миграции (атакер пока носит entt-ид тела; ссылка {слот, ID} сменит его
// в ломте 7).
inline constexpr std::uint32_t kObjNoAttacker = 0xFFFFFFFFu;

// «Рода нет» у колонки kind: ноль — законный род (NPCType::Peasant = 0),
// поэтому «нет» — ПОСЛЕДНЕЕ значение u16 (ЗАКОН УЗКОГО ИНДЕКСА). Enum
// NPCType шириной u8, так что столкнуться с живым родом это значение не
// может по типу; valid_npc_kind() отвергает его без спецслучая. Носят его
// слоты без рода: игрок и голые фикстуры — прежнее «компоненты NPCKind
// нет» тела.
inline constexpr std::uint16_t kObjNoKind = 0xFFFFu;

struct SubObjects {
    // Идентичность жильца: ID рождения (счётчик стора, с 1; 0 = «никто»).
    // u32 — за жизнь сцены не заворачивается (4 млрд рождений недостижимы:
    // 16к тел на тик входа — это 262 тысячи полных сцен).
    std::array<std::uint32_t, std::size_t(kMaxBodyCrowd)> id{};
    // Род и состояние — МАСКА, не компоненты (ЗАКОН СТРОКИ КАТАЛОГА:
    // смена архетипа в тике невыразима по построению).
    std::array<std::uint16_t, std::size_t(kMaxBodyCrowd)> flags{};
    // ── FX-колонки (ломоть 1а): бывшие DamageFx / LastHit. Вспышка тела
    // (hitFlash) СНЕСЕНА вердиктом владельца 2026-10-05 («пока не нужна
    // снесём … минимизировать число колонок») — она писалась и гасла, но
    // визуального читателя не имела никогда; возврат после предемо =
    // колонка + тинт тел в рендере.
    std::array<std::uint32_t, std::size_t(kMaxBodyCrowd)> lastHitBy{};
    // Событие «в этом тике по телу попали»: бит 0 = pending, бит 1 =
    // lethal, бит 2 = blocked. Дренируется одним проходом за тик.
    std::array<std::uint8_t, std::size_t(kMaxBodyCrowd)> damageFx{};
    // ── Род и уровень тела (кусок 1 ломтя 2): бывшие ecs::NPCKind /
    // ecs::NpcLevel. Род — факт головы при воплощении; .type == kObjNoKind
    // значит «рода нет» (игрок, голая фикстура). Уровень безуровневого — 0.
    // Лицо (NpcCharacter) колонкой НЕ стало: потребляется один раз при
    // рождении (рост тела от bodyShape) и после не читается никем —
    // прецедент вспышки («минимизировать число колонок»); вернётся
    // колонкой вместе с читателем (вариация спрайтов).
    std::array<ecs::NPCKind, std::size_t(kMaxBodyCrowd)> kind{};
    std::array<std::int16_t, std::size_t(kMaxBodyCrowd)> level{};
    // ── Боевая пара (кусок 2 ломтя 2): бывшие ecs::Pools / ecs::Combat /
    // ecs::MissileAttack. «Баров нет» = maxHp 0 — коллизия невыразима:
    // даже мёртвое тело хранит максимум, тела с барами и maxHp 0 не бывает
    // по построению. Лист — колонка + бит kObjHasCombat (см. маску выше).
    // Снарядные параметры: «нет» = speed 0 — дверь рождения коэрсит
    // авторский ноль в 200 (maybe_emplace_missile_attack), ноль недостижим
    // у настоящего стрелка. У зеркальных тел pools — КОПИЯ записи store
    // (зеркальный закон: mirror_bodies_from_record переливает каждый тик;
    // рана ложится на ЗАПИСЬ дверью pools_of, store-первой).
    std::array<ecs::Pools, std::size_t(kMaxBodyCrowd)> pools{};
    std::array<ecs::Combat, std::size_t(kMaxBodyCrowd)> combat{};
    std::array<ecs::MissileAttack, std::size_t(kMaxBodyCrowd)> missile{};
    // ── Движение/думка (кусок 3 ломтя 2): бывшие ecs::SubworldAi /
    // ecs::VisualPos / ecs::GoingHome / ecs::Airborne. Мозг — колонка +
    // бит kObjHasAi (Wander = 0 законен, сентинела нет). Визуальная
    // позиция — безусловная колонка слота: нулевая скорость = интерполятор
    // стоит (законное авторское значение смоук-фикстур), биту нечего
    // охранять. «Домой» и «в воздухе» — бит + колонка (см. маску).
    std::array<ecs::SubworldAi, std::size_t(kMaxBodyCrowd)> ai{};
    std::array<ecs::VisualPos, std::size_t(kMaxBodyCrowd)> visual{};
    std::array<ecs::GoingHome, std::size_t(kMaxBodyCrowd)> goHome{};
    std::array<float, std::size_t(kMaxBodyCrowd)> airborneVz{};

    int count = 0;        // живых слотов (для приборов, не для обхода)
    int cursor = 0;       // бегунок выдачи — слоты переиспользуются по кругу
    std::uint32_t nextId = 1;   // эмитент ID рождений; 0 навсегда «никто»

    // ── АКТИВНОЕ ТЕЛО — ССЫЛКА СЦЕНЫ {слот, ID} (вердикт владельца
    // 2026-10-05: «активность — ссылка, не свойство тела»; тела арены
    // агностичны, «игрокость» = куда смотрит ввод). ID 0 = ссылки нет.
    // Переключение тела (одержимость сейчас, сквады фракции потом) —
    // перезапись ОДНОЙ ссылки; протухшая (тело умерло, слот перерождён)
    // отваливается проверкой ID, как любая ссылка на объект. Симметрия с
    // макро: там «чей сквад активен» — скаляр мира playerFlagBits.
    std::uint16_t avatarSlot = 0;
    std::uint32_t avatarId   = 0;
    // ТРАНЗИТ миграции: entt-ид того же тела (адрес для реестра, пока он
    // жив; умирает в ломте 7 вместе с реестром). Истина — пара выше.
    std::uint32_t avatarEnttBits = 0xFFFFFFFFu;

    // Родить слот: первый свободный от бегунка. -1 = кап («кап стоит на
    // воплощённом»: звонящий отказывается честно, как BodyCrowd::add).
    int alloc() {
        for (int step = 0; step < kMaxBodyCrowd; ++step) {
            const int s = (cursor + step) & (kMaxBodyCrowd - 1);
            if (flags[std::size_t(s)] & kObjAlive) continue;
            cursor = (s + 1) & (kMaxBodyCrowd - 1);
            id[std::size_t(s)] = nextId++;
            flags[std::size_t(s)] = kObjAlive;
            lastHitBy[std::size_t(s)] = kObjNoAttacker;
            damageFx[std::size_t(s)] = 0u;
            kind[std::size_t(s)] = ecs::NPCKind{kObjNoKind, 0u};
            level[std::size_t(s)] = 0;
            pools[std::size_t(s)] = ecs::Pools{};
            combat[std::size_t(s)] = ecs::Combat{};
            missile[std::size_t(s)] = ecs::MissileAttack{};
            ai[std::size_t(s)] = ecs::SubworldAi{};
            visual[std::size_t(s)] = ecs::VisualPos{};
            goHome[std::size_t(s)] = ecs::GoingHome{};
            airborneVz[std::size_t(s)] = 0.0f;
            ++count;
            return s;
        }
        return -1;
    }

    // Занулить слот (смерть объекта как ЗАПИСИ; труп ломтя 5 слот НЕ
    // освобождает — он гасит kObjAlive-роль маской, оставаясь жильцом).
    void free(int slot) {
        if (slot < 0 || slot >= kMaxBodyCrowd) return;
        if (!(flags[std::size_t(slot)] & kObjAlive)) return;
        flags[std::size_t(slot)] = 0u;
        --count;
    }
};
// 16384 × (4+2+4+1+4+2+36+28+12+40+12+8+4) Б колонок + служебные: цена
// названа и закреплена. 157 Б/слот × 16384 ≈ 2.45 МиБ — профиль один у
// пустой и полной сцены (ЗАКОН СТАБИЛЬНОСТИ).
static_assert(sizeof(SubObjects) == std::size_t(kMaxBodyCrowd) * 157 + 24,
              "массив объектов сцены: 157 Б/слот (кусок 3: +ai 40, "
              "+visual 12, +goHome 8, +airborneVz 4) + служебные");

} // namespace sm::sub
