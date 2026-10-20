// ТИП ОБЪЕКТА — ТРЕТЬЯ СТУПЕНЬ ЛЕСТНИЦЫ ОБЪЕКТЫ → АРХЕТИПЫ → ТИПЫ → СТРОКИ
// (наряд M-295; замысел и дословные вердикты — CANON S29).
//
// Вердикт владельца 2026-10-20, дословно: «ну всё таки я бы разделял типы и
// строки это принципиально давай сразу так сделаем объекты архетипы типы
// строки СРАЗУ ТАК потому что уже очевидно ТАК ПРИДЁТСЯ»; и о наполнении:
// «у нас есть оружие архетип а есть типы кинжалы меч дубины и тд и также есть
// архетип гуманоиды а есть типы гоблины бандиты крестьянин троль».
//
// ── ЧЕМ ЭТО НЕ ЯВЛЯЕТСЯ: ТАБЛИЦЕЙ-СПУТНИКОМ ПО КЛЮЧУ СТРОКИ ───────────────
// DOD п.9 запрещает «у двух таблиц один и тот же ключ», и соблазн был ровно
// такой: строк 91, и у каждой сегодня ровно один тип, то есть напиши тип на
// строку — и получишь 91 строку-зеркало, каждая колонка которой выводится из
// каталога той же биекцией. Поэтому КЛЮЧ ЗДЕСЬ СВОЙ: типов 59, они
// ГРУППИРУЮТ строки, и группировка настоящая уже сегодня — двенадцать монет
// суть один тип `coin`, четыре руды — один `ore`, два зелья — один
// `consumable`. Биекции нет ни в одной половине.
//
// ── ВНИЗ — ОТРЕЗОК, И ОН ВЫВЕДЕН, А НЕ АВТОРЕН ────────────────────────────
// «Все строки типа» есть НЕПРЕРЫВНЫЙ ДИАПАЗОН ординалов, а не фильтр по
// колонке. Числа этого диапазона здесь НЕ СТОЯТ сознательно: авторенное
// `rowCount` было бы вторым спеллингом того, что уже сказано авторской
// таблицей строк, то есть парой ручек, которым дано разъехаться. Вместо него
// отрезок ВЫВОДИТСЯ из самих строк там, где авторская таблица видна
// компилятору (`kCatalog@src/tables/items.cpp` для вещей,
// `kNpcTypeDefs@src/tables/npc.h` для существ), и непрерывность прибита
// `static_assert`-ом в обоих местах. Выведенное не может разъехаться с
// источником ПО ПОСТРОЕНИЮ (DOD п.9).
//
// ПЕР-ТИПОВОЙ ДВЕРИ ОТРЕЗКА ЗДЕСЬ НЕТ, И ЭТО СКАЗАНО ВСЛУХ: её единственный
// названный читатель — таблица объект-модулей, которая ключуется ТИПОМ, а она
// наряд M-296 и в дереве её ещё нет. Дверь без читателя есть колонка-сирота
// (DOD п.9), поэтому место названо, а дверь придёт вместе с читателем.
// Отрезок АРХЕТИПА двери имеет — у него читателей много
// (`tables/world_row.h`).
//
// ── ПОРЯДОК ТИПОВ — ПРЕДПОСЫЛКА ВСЕГО ВЫШЕ ────────────────────────────────
// Типы идут ПО АРХЕТИПУ (под `static_assert` внизу файла), а строки идут по
// типам. Из этих двух фактов и следует, что отрезок архетипа — диапазон:
// сломай любой, и «все строки архетипа» станут фильтром.
#pragma once
#include <cstddef>
#include <cstdint>

#include "tables/object_archetype.h"

namespace sm {

// ТАКСОНОМИЯ НАЗВАНА ВЛАДЕЛЬЦЕМ 2026-10-20 (вердикт «да берём», одна правка —
// `potion` обобщён до `consumable`, и одно уточнение — «да культисты
// отдельный тип … они точно не маги»).
enum class ObjectType : std::uint8_t {
    // ── ВЕЩИ ──────────────────────────────────────────────────────────────
    // Weapon: «кинжалы меч дубины» — его собственный пример. Типы с одной
    // строкой здесь НЕ дефект формы: оружие растёт видами первым же ростом
    // контента, а особенность кинжала есть модуль ТИПА, не колонка строки.
    Dagger = 0,
    Sword,
    Spear,
    Axe,
    Mace,
    Staff,
    Bow,
    // Armor
    BodyArmor,
    // Potion → ОДИН тип, и он шире архетипа по имени сознательно (вердикт
    // владельца: «potion обобщить до консумабле типа там будут свитки и тд всё
    // что влияет на ресуры анкеты - типа могут бонусы к атриутам бафы
    // восстановление - то есть по смыслу они будут давтаь эффекты спелов при
    // потреблнние ну буквально свитки жезлы пошны»). Архетип отвечает «что
    // потребляют ЗА ЭФФЕКТ», тип — «чем именно это выпито, прочтено или
    // разряжено».
    Consumable,
    // Food
    Ration,
    // Material
    Ore,
    Timber,
    Stone,
    Clay,
    Fibre,
    Bone,
    Hide,
    Herb,
    // Goods — торговая корзина; у вещи одна роль, поэтому всё здесь и только
    // здесь есть товар комфорта лестницы нужд.
    Cloth,
    Bricks,
    Tools,
    Furniture,
    Jewelry,
    Carving,
    Statue,
    // Currency — ОДИН тип на все двенадцать монет четырёх народов: монета
    // отличается номиналом и чеканкой, то есть КОЛОНКАМИ СТРОКИ, а не
    // механикой. Сильнейшая группировка каталога, 12 строк в одном типе.
    Coin,
    // Vehicle
    Wagon,
    // Misc — свалка, и тип у неё один, как и строка.
    Gem,

    // ── ВОПЛОЩАЕМЫЕ ──────────────────────────────────────────────────────
    // Folk — «бандиты крестьянин» из его же примера.
    Peasant,
    Merchant,
    // Разбойник и дорожный засадник — ОДИН тип: они дерутся одним телом, одним
    // листом и одним лутом, а различает их колонка зрения (засадник видит всю
    // дорогу). Ровно то, для чего тип и нужен: две строки одного типа.
    Bandit,
    Guard,
    // Ведьма и сорка — один тип: обе кастуют своим спеллом из одной таблицы.
    Mage,
    // КУЛЬТИСТ ОТДЕЛЬНЫМ ТИПОМ — ВЕРДИКТ ВЛАДЕЛЬЦА, дословно: «да культисты
    // отдельный тип», «они точно не маги». У него своя фракция и ареал
    // подземелья, а не кафедра ведьмы.
    Cultist,
    Adventurer,
    Taxman,
    // Monster — ось РАЗУМ, не руки (поправка владельца: «монстры они обладают
    // разумом (огры троли гоблины драконы и тд)»).
    Goblinoid,
    Giant,
    Harpy,
    Dragon,
    // Animal — «дефолт животные нашего мира кролики волки».
    Rodent,
    Deer,
    Canine,
    Bear,
    Boar,
    Reptile,
    Bird,
    Amphibian,
    Bat,
    Arachnid,
    // Livestock — «скот и ваще любые домашние животные».
    Horse,
    // Овца и коза — один тип мелкого скота.
    Smallstock,
    // Void — «для всякой хтони и демонов».
    Skeleton,
    Zombie,
    Wraith,
    Golem,
    Demon,
    Elemental,
    Lich,

    Count,   // не значение; выведенный счёт (ЗАКОН СЛОВАРЯ п.3)
};

// «ТИПА НЕТ» — ПОСЛЕДНЕЕ ЗНАЧЕНИЕ ТИПА, А НЕ СВАЛКА И НЕ −1.
//
// ШРАМ, КУПИВШИЙ ЭТО ИМЯ: `catalog_type_of@src/tables/items.cpp` отдавала
// `ItemType::Misc` для id, которого в каталоге НЕТ ВОВСЕ, — то есть один знак
// отвечал на ДВА вопроса мира («свалочная вещь» и «строки не существует»), и
// второй ответ был немым. С архетипами это стало бы немым `default:` (ЗАКОН
// СЛОВАРЯ п.4): «Misc» начало бы значить «модуля нет». Развод обязателен, и
// форма его задана DOD «Законом узкого индекса»: нет элемента — ПОСЛЕДНЕЕ
// значение типа, никогда −1 (владелец: «я не люблю −1»).
inline constexpr ObjectType kObjectTypeNone = ObjectType(0xFFu);
static_assert(int(ObjectType::Count) < 0xFF,
              "сентинел «типа нет» занимает последнее значение u8 — счёт типов "
              "обязан остаться ниже него");

struct ObjectTypeDef {
    // Стабильный машинный id — то, чем контент НАЗЫВАЕТ тип. Строка живёт на
    // границе мира и только там (ЗАКОН СЛОВАРЯ п.1): внутри симуляции ходит
    // ординал.
    const char*     id;
    // Вверх — КОЛОНКА: тип несёт архетип. Наследование есть ровно это.
    ObjectArchetype arch;
};

inline constexpr ObjectTypeDef kObjectTypes[std::size_t(ObjectType::Count)] = {
    // ── вещи ──────────────────────────────────────────────────────────────
    {"dagger",     ObjectArchetype::Weapon},
    {"sword",      ObjectArchetype::Weapon},
    {"spear",      ObjectArchetype::Weapon},
    {"axe",        ObjectArchetype::Weapon},
    {"mace",       ObjectArchetype::Weapon},
    {"staff",      ObjectArchetype::Weapon},
    {"bow",        ObjectArchetype::Weapon},
    {"body_armor", ObjectArchetype::Armor},
    {"consumable", ObjectArchetype::Potion},
    {"ration",     ObjectArchetype::Food},
    {"ore",        ObjectArchetype::Material},
    {"timber",     ObjectArchetype::Material},
    {"stone",      ObjectArchetype::Material},
    {"clay",       ObjectArchetype::Material},
    {"fibre",      ObjectArchetype::Material},
    {"bone",       ObjectArchetype::Material},
    {"hide",       ObjectArchetype::Material},
    {"herb",       ObjectArchetype::Material},
    {"cloth",      ObjectArchetype::Goods},
    {"bricks",     ObjectArchetype::Goods},
    {"tools",      ObjectArchetype::Goods},
    {"furniture",  ObjectArchetype::Goods},
    {"jewelry",    ObjectArchetype::Goods},
    {"carving",    ObjectArchetype::Goods},
    {"statue",     ObjectArchetype::Goods},
    {"coin",       ObjectArchetype::Currency},
    {"wagon",      ObjectArchetype::Vehicle},
    {"gem",        ObjectArchetype::Misc},
    // ── воплощаемые ───────────────────────────────────────────────────────
    {"peasant",    ObjectArchetype::Folk},
    {"merchant",   ObjectArchetype::Folk},
    {"bandit",     ObjectArchetype::Folk},
    {"guard",      ObjectArchetype::Folk},
    {"mage",       ObjectArchetype::Folk},
    {"cultist",    ObjectArchetype::Folk},
    {"adventurer", ObjectArchetype::Folk},
    {"taxman",     ObjectArchetype::Folk},
    {"goblinoid",  ObjectArchetype::Monster},
    {"giant",      ObjectArchetype::Monster},
    {"harpy",      ObjectArchetype::Monster},
    {"dragon",     ObjectArchetype::Monster},
    {"rodent",     ObjectArchetype::Animal},
    {"deer",       ObjectArchetype::Animal},
    {"canine",     ObjectArchetype::Animal},
    {"bear",       ObjectArchetype::Animal},
    {"boar",       ObjectArchetype::Animal},
    {"reptile",    ObjectArchetype::Animal},
    {"bird",       ObjectArchetype::Animal},
    {"amphibian",  ObjectArchetype::Animal},
    {"bat",        ObjectArchetype::Animal},
    {"arachnid",   ObjectArchetype::Animal},
    {"horse",      ObjectArchetype::Livestock},
    {"smallstock", ObjectArchetype::Livestock},
    {"skeleton",   ObjectArchetype::Void},
    {"zombie",     ObjectArchetype::Void},
    {"wraith",     ObjectArchetype::Void},
    {"golem",      ObjectArchetype::Void},
    {"demon",      ObjectArchetype::Void},
    {"elemental",  ObjectArchetype::Void},
    {"lich",       ObjectArchetype::Void},
};

// ВВЕРХ ПО ЛЕСТНИЦЕ: тип → архетип. Одна дверь, чтобы читатели спрашивали
// одним голосом. Сентинел и всякий ординал вне оси отвечают свалкой — это
// fail-closed В ТОЧКЕ ЧТЕНИЯ, а не молчаливый «почти правильный» архетип:
// `Misc` значит «ни одна система этой вещи не служит», что для несуществующего
// типа честнее всего, и воплощаемым он не является.
inline constexpr ObjectArchetype object_type_archetype(ObjectType t) {
    return std::size_t(t) < std::size_t(ObjectType::Count)
        ? kObjectTypes[std::size_t(t)].arch
        : ObjectArchetype::Misc;
}

inline constexpr const char* object_type_id(ObjectType t) {
    return std::size_t(t) < std::size_t(ObjectType::Count)
        ? kObjectTypes[std::size_t(t)].id
        : "?";
}

// ── СТЕНА ПОД КОМПИЛЯТОРОМ ────────────────────────────────────────────────
// ТИПЫ ОТСОРТИРОВАНЫ ПО АРХЕТИПУ — без этого «все строки архетипа» перестают
// быть диапазоном, потому что строки идут по типам. Тип, вставленный не в свою
// группу, красит сборку здесь.
constexpr bool object_types_sorted_by_archetype() {
    for (std::size_t i = 1; i < std::size_t(ObjectType::Count); ++i) {
        if (std::uint8_t(kObjectTypes[i].arch)
            < std::uint8_t(kObjectTypes[i - 1].arch)) {
            return false;
        }
    }
    return true;
}
static_assert(object_types_sorted_by_archetype(),
              "типы идут по архетипу: строки идут по типам, поэтому отрезок "
              "архетипа есть ДИАПАЗОН только при отсортированных типах");

// У КАЖДОГО АРХЕТИПА ЕСТЬ ХОТЬ ОДИН ТИП. Архетип без типов — строка оси, о
// которой мир не может высказаться ни одной вещью: либо он преждевременен,
// либо лишний (DOD п.9).
constexpr bool every_archetype_has_a_type() {
    for (std::size_t a = 0; a < std::size_t(ObjectArchetype::Count); ++a) {
        bool found = false;
        for (const ObjectTypeDef& d : kObjectTypes) {
            if (std::uint8_t(d.arch) == std::uint8_t(a)) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}
static_assert(every_archetype_has_a_type(),
              "у каждого архетипа оси обязан быть хотя бы один тип — иначе это "
              "строка, о которой мир не умеет высказаться");

// ИДЕНТИФИКАТОРЫ УНИКАЛЬНЫ: id есть КЛЮЧ контента, а двойной ключ разрешается
// молча и в пользу первого.
constexpr bool object_type_ids_unique() {
    for (std::size_t i = 0; i < std::size_t(ObjectType::Count); ++i) {
        for (std::size_t j = i + 1; j < std::size_t(ObjectType::Count); ++j) {
            const char* a = kObjectTypes[i].id;
            const char* b = kObjectTypes[j].id;
            std::size_t k = 0;
            while (a[k] != '\0' && a[k] == b[k]) ++k;
            if (a[k] == '\0' && b[k] == '\0') return false;
        }
    }
    return true;
}
static_assert(object_type_ids_unique(), "id типа — ключ контента, он уникален");

static_assert(int(ObjectType::Count) == 59,
              "59 типов на 91 строку мира — таксономия владельца 2026-10-20. "
              "Счёт пинится затем, чтобы рост контента был ВИДЕН в дифе, а не "
              "чтобы запретить его: новый тип правит это число одной цифрой");

} // namespace sm
