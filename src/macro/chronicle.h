// THE WORLD'S CHRONICLE — what happened, as flat records the world can be
// ASKED about.
//
// Owner's rulings, 2026-08-27 (CANON.md S20.1). The event bus is the nervous
// system of the WORLD, not a channel of notifications for the interface: the
// world changes and grows, quests run, the story moves, owners change, towns
// revolt — one maximally universal and CONTEXTUAL system, built once.
//
// ── THE TEST THIS FILE EXISTS TO PASS ────────────────────────────────────
// The owner's own example. A witcher-like squad walks the villages and takes
// contracts on monsters; the monsters are actors on the same bus, and a
// monster squad that kept killing peasants nearby LEFT TRACES. The witcher
// goes and finds it himself. For that, something must be able to ask the
// world:
//
//     what happened near this village in the last thirty days?
//
// Every decision below follows from that one question:
//   · it is asked BY PLACE and BY TIME    ⇒ facts must be indexable, and a
//     string is not an index;
//   · the witcher may arrive a game week later ⇒ facts must survive the SAVE;
//   · every squad on the map will ask it  ⇒ the scan must be an ARRAY walk,
//     not a chase through heap pointers.
// A `std::string` inside the record breaks all three at once. That is why a
// fact is a POD — not to be fast, but because the living world is impossible
// otherwise. Words are DERIVED at the moment of display, from the same tables
// that already hold them (the idiom the skill tooltip already uses).
//
// ── WHO ──────────────────────────────────────────────────────────────────
// Owner: «сквады это акторные элементы макромира, основную историю творят они
// (но другие энтити тоже — ландмарки и сами клетки, ну и фракции)». So a
// subject is a KIND plus an ordinal in that kind's own id space — squads
// first, and the rest able to speak by the same record.
#pragma once

#include "bus/fact.h"
#include "core/time.h"
#include "tables/fact_kinds.h"

#include "core/caps.h"   // kWorldCellCap — кап индексных клеток выведен из него

#include <cstddef>
#include <cstdint>

namespace sm {

// (FactSubject, FactKind и строка WorldFact ПЕРЕЕХАЛИ в bus/fact.h ломтём 3
// эпика фрейма: факт — род 5 канонического фрейма, его форма живёт ниже
// обоих миров. ТАБЛИЦА ДЕЯНИЙ — FactKindDef, kFactKinds, fact_kind_def и
// выведенные из неё законы славы (renown_for_deed, kRenownToBeNamed) —
// ПЕРЕЕХАЛА в tables/fact_kinds.h ломтём 5 M-171: каталог живёт по обе
// стороны границы, летопись — ярусы и двери ниже — осталась макро-слоем.)

// ── TWO TIERS, ONE RECORD (owner, 2026-08-27) ────────────────────────────
//
// Dwarf Fortress keeps every historical event of every year in memory, and
// that is its known illness: a long world generates into gigabytes and then
// crawls on its own past — late versions had to add historical pruning
// because unbounded did not work. The half DF gets right is the DISTINCTION
// it draws between an event and a legend, and that is the half taken here.
//
// So: ONE record type, two places it lives.
// So: ONE tier — the RING, what the world is ASKED. Indexed by cell, sized to
// the longest `interestDays` above, and it FORGETS. This is the witcher's
// tier, and today it is the only one.
//
// ── ЗДЕСЬ ЖИЛ ВТОРОЙ ЯРУС — АННАЛЫ, «ЧТО МИР ПОМНИТ НАВСЕГДА», И ОН СНЕСЁН
// ЦЕЛИКОМ (M-184, вердикт владельца 2026-10-30: «да сноси оба», довод его же
// — «эти системы ещё не вписаны в новую системность»). Снос, а не уборка
// потолка: с ярусом ушли `std::vector<WorldFact> annals` (кап 1M × 32 Б =
// 32 МиБ), громкий `annalsFull`, дверь запроса `chronicle_annals_of`, закон
// отбора «новость, выдыхающаяся за неделю, не претендует на вечность»
// (`kChronicleWeatherDays`), свой блок сейва и своя строка переписи штабелей.
//
// ЧТО ЭТО ЗАКРЫЛО СВЕРХ СНОСА: у яруса был СВОЙ блок сейва, а §10 п.2 говорит
// «новый род данных мира — это новый род ПАКЕТА, а не новый блок сейва».
// Ярус, чья долговечность держалась собственным блоком, был ровно таким
// носителем.
//
// ЗАГЛУШКИ НА ЕГО МЕСТЕ НЕТ СОЗНАТЕЛЬНО (§1: игра не играбельна, временные
// костыли не нужны; вердикт владельца того же дня — «ничего, дыра стоит
// открытой»): мир СЕГОДНЯ не помнит ничего дольше кольца, и это названо
// вслух рядом M-184 реестра, а не прикрыто «пока-памятью».

// The ring's size is a FORMULA with one unmeasured term, and it is written
// down rather than hidden: it must hold `longest interestDays × the world's
// facts per day`. The longest span is DERIVED from the table
// (`kChronicleLongestInterestDays`, tables/fact_kinds.h) — not recited here; the
// world's daily rate has never been measured, which is exactly why
// `Chronicle::factsPeak` ships as part of the contract — the PEAK day, because
// that is the term the formula names (`factsToday` alone answered a different
// question and could not calibrate anything; M-254). 2^16 records is 2 MB
// — by house rule not an argument (CANON S26) — and covers 64 days at a
// thousand facts a day. Retune it from the counter, not from feel.
inline constexpr std::uint32_t kChronicleFacts = 1u << 16;

// The index is COARSE, like the squad index it borrows its shape from: "what
// happened near here" is a neighbourhood question, so a fact belongs to an
// index cell rather than to a map cell, and a query touches 3×3 of them.
inline constexpr int kChronicleCellSize = 8;
// Кап индексных клеток — ВЫВОД, а не число: мир не больше капа клеток
// (`kWorldCellCap@src/core/caps.h`), индексная клетка — квадрат 8×8, значит
// индексных клеток не больше капа, делённого на площадь квадрата (16 384).
inline constexpr std::size_t kChronicleIndexCells =
    kWorldCellCap / (std::size_t(kChronicleCellSize)
                     * std::size_t(kChronicleCellSize));
static_assert(kWorldSideCap % std::size_t(kChronicleCellSize) == 0u,
              "сторона капа мира кратна стороне индексной клетки — иначе "
              "округление сетки вверх вышло бы за кап индексных клеток");

// A participant reference is a KIND, and nothing else.
//
// ── ЗДЕСЬ ЖИЛ БИТ «ИМЕНОВАН» (`kFactSubjectNamed` = 0x80), И ОН УМЕР ВМЕСТЕ С
// АННАЛАМИ, А НЕ ЗАОДНО С НИМИ (M-184, DOD п.9). Бит ставили обе двери записи
// дела (`record_deed_filed@src/macro/squad.h`), а ЧИТАЛ его РОВНО ОДИН гейт —
// отбор в анналы. Ярус снесён, читателей у бита стало НОЛЬ, то есть он стал
// колонкой-сиротой того самого рода, который §DOD п.9 запрещает: «данные на
// будущее» видит следующий, принимает за закон и пишет под них код. Поэтому
// снят и он, и его аксессор `fact_subject_marked_named`, и производный
// предикат `subject_is_named`.
//
// ЧТО НЕ ТРОНУТО И ПОЧЕМУ: `renown_is_named@src/tables/fact_kinds.h` ЖИВА —
// она отвечает на ДРУГОЙ вопрос («стал ли этот фигурой»). ЧИТАТЕЛЬ У НЕЁ
// ОДИН, И ЭТО ПЕРЕСЧЁТ 2026-11-08: `record_battle_facts@src/macro/squad.h`
// (решает, называть ли врага объектом факта смерти). Здесь стояло «читателей
// трое помимо летописи: `squad_is_named@src/app/main.cpp` и две двери
// `src/macro/squad.h`» — завышение втрое: вторая дверь `squad.h` не
// существовала, а `squad_is_named` сама была мёртвой (ноль вызовов во всём
// дереве) и снесена той же ревизией. Умер НОСИТЕЛЬ бита в записи, а не
// закон славы, и это по-прежнему верно.
inline constexpr std::uint8_t fact_subject_kind(std::uint8_t packed) {
    return std::uint8_t(packed & 0x7Fu);
}
inline constexpr std::uint8_t fact_subject(FactSubject kind) {
    return std::uint8_t(kind);
}

// ── ЛЕТОПИСЬ — ПЛОСКАЯ, В ТЕЛЕ СОСТОЯНИЯ МИРА (ломоть 4 сейв-эпика, M-180,
// 2026-11-10) ────────────────────────────────────────────────────────────
// Здесь стояли два `std::vector` — кольцо и головы цепочек, оба и так
// выделялись целиком под свой кап дверью `chronicle_init`. Теперь это массивы
// в теле структуры: у летописи есть `sizeof`, heap-члена нет, байты снимаются
// одним куском. ЗАКОН ПАМЯТИ ЛЕТОПИСИ ЭТИМ ШАГОМ НЕ ТРОНУТ — это по-прежнему
// кольцо на `kChronicleFacts` записей, которое забывает; дописываемый лог с
// громким стопом и цепочки по субъекту — предмет ряда M-423 реестра, и он
// сменит эту раскладку, а не ляжет рядом с ней.
struct Chronicle {
    std::uint32_t nextSeq = 1;            // 0 is "no fact", so counting starts at 1
    int cols = 0, rows = 0;
    // Facts written on the current day, and the day it is counting — the
    // instrument that turns kChronicleFacts from a feeling into a measurement.
    std::int32_t countingDay = -1;
    std::uint32_t factsToday = 0;
    // САМЫЙ ПЛОДОВИТЫЙ ДЕНЬ ЗА ЖИЗНЬ МИРА — И БЕЗ НЕГО ПРИБОР НЕ ОТВЕЧАЛ НА
    // СВОЙ ЖЕ ВОПРОС (M-254, 2026-10-12). Формула размера кольца, записанная
    // над `kChronicleFacts`, требует «самый длинный interestDays × факты
    // мира ЗА ДЕНЬ», то есть ПИК. `factsToday` обнуляется на смене суток
    // (chronicle.cpp), поэтому отвечал только «сколько набежало в тот день, в
    // который кто-то посмотрел» — а это не величина формулы. Перепись назвала
    // это контуром, который короче своей прозы: читателей в симуляции ноль,
    // живым его держали сейв и printf смоука.
    //
    // ЧИТАТЕЛЬ ЭТОЙ КОЛОНКИ НАЗВАН И ОН ЧЕЛОВЕК (DOD п.9): сценарий
    // `chronicle_rate` печатает её в лог, и по ней калибруется
    // `kChronicleFacts`. Персистентна — пик за ОДИН прогон отвечает на
    // вопрос слабее, чем пик за жизнь мира, а длинные прогоны и есть мера.
    std::uint32_t factsPeak = 0;
    // Newest fact of each index cell; живые — первые cols × rows, хвост нулём.
    std::uint32_t cellHead[kChronicleIndexCells] = {};
    WorldFact     ring[kChronicleFacts] = {};   // written round

    // Скроена ли летопись под мир: сетка индексных клеток названа. Кольцо и
    // головы лежат в теле — «пустого кольца» больше не бывает.
    bool ready() const { return cols > 0 && rows > 0; }
    // The oldest sequence still in the ring. Everything below it has been
    // overwritten, which is exactly how a chain knows where to stop.
    std::uint32_t oldest_seq() const {
        return nextSeq > kChronicleFacts ? nextSeq - kChronicleFacts : 1u;
    }
};

// СТОРОЖ РАЗМЕРА (AGENTS DOD п.10). Вывод, а не замер: шесть чисел шапки по
// 4 Б (`nextSeq`, `cols`, `rows`, `countingDay`, `factsToday`, `factsPeak`),
// головы цепочек по 4 Б на индексную клетку капа и кольцо записей; выравнивание
// 4, хвоста нет. (До ломтя 4 сейв-эпика здесь стояло 72 Б — два заголовка
// вектора и та же шапка, а память летописи жила в куче мимо `sizeof`.) Новый
// ярус памяти мира двинет это число и потребует строки переписи штабелей.
static_assert(sizeof(Chronicle)
                  == 6u * 4u + kChronicleIndexCells * sizeof(std::uint32_t)
                         + std::size_t(kChronicleFacts) * sizeof(WorldFact),
              "летопись: шапка + головы цепочек + кольцо, без хвоста; новый "
              "ярус = новая строка переписи штабелей");

// Size it for a map. Safe to call again; a resize forgets, which is correct —
// a chronicle of another world is not this world's past. Мир вне капа клеток
// летописи не получает: она остаётся не скроенной, и двери ниже молчат.
void chronicle_init(Chronicle& c, int mapW, int mapH);

// Write one fact. Returns its sequence number (0 if the chronicle has no
// world yet). Every field is a number, so this is a store and two links.
std::uint32_t chronicle_record(Chronicle& c, const WorldFact& fact);

// (The place-fact door lives in state.h `record_rooted_fact` — it must
// resolve the place's RENOWN to mark figure-ness and to pay the deed, and a
// chronicle of ordinals must not learn where renown lives.)

// ── The question the witcher asks ────────────────────────────────────────
//
// Walks the index cells around (x, y), newest first, and hands every fact no
// older than `sinceDay` to `visit`. Returns how many it offered.
//
// The chains SELF-TRUNCATE: a link is followed only while it points at a slot
// that still holds the fact it linked to, which the sequence number proves.
// Eviction is strictly oldest-first, so the first dead link is the end of the
// live part of the chain — no unlinking, no bookkeeping, and a ring that
// wrapped away half a village's past simply stops earlier.
using FactVisitor = void (*)(void* user, const WorldFact& fact);
int chronicle_near(const Chronicle& c, int x, int y, int radiusCells,
                   std::int32_t sinceDay, FactVisitor visit, void* user);

// The same walk over the RING, newest first — for the player's journal, which
// is a view on the past rather than a place with a past of its own.
int chronicle_recent(const Chronicle& c, std::int32_t sinceDay, int limit,
                     FactVisitor visit, void* user);

// (`chronicle_rebuild_links` — пересборка цепочек по клетке из записей кольца —
//  УМЕРЛА с проводом сейва, M-439. Она существовала для загрузки: файл вёз
//  записи без ссылок, и цепочки собирались заново. Сейв теперь есть прямой
//  дамп состояния, головы цепочек и ссылки едут в нём теми же байтами, какими
//  лежат в памяти, — пересобирать нечего, и звонящих у двери не осталось.)

// ── ASKING IN THE TABLE'S OWN WORDS ──────────────────────────────────────
//
// "Recent" is not one number for the whole game: a killing is news for a
// season and a sale is stale in a week, and the kind's row already says so
// (`interestDays`). This asks the question the way the world means it —
// "what of THIS sort happened near here while it was still news" — so a
// reader never restates a window the table already owns.
int chronicle_near_kind(const Chronicle& c, int x, int y, int radiusCells,
                        FactKind kind, std::int32_t today,
                        FactVisitor visit, void* user);

// ── SAYING IT IN WORDS ───────────────────────────────────────────────────
//
// THE law (CANON S20.1): a fact is numbers, and the words are DERIVED at the
// moment of display. That is what makes the player's journal a VIEW on the
// world's memory rather than eight thousand sentences in the save file, and
// it is what makes localisation possible for free — the same fact says itself
// in another language by changing the tables, not the past.
//
// The chronicle does not know how the world NAMES things (a squad's name
// lives on an ECS component, a settlement's in GameState), and it must not
// learn: it is a macro-layer record of ordinals. So the caller lends it three
// resolvers. This is the whole of the "readers of facts need to see the
// world" problem, in its smallest honest form — and it is the same lending
// the witcher will do when he goes looking for the village.
struct FactNaming {
    // Each returns a display name for an ordinal, or nullptr if it cannot say
    // — in which case the sentence falls back to what it does know.
    const char* (*squad)(void* user, std::uint32_t ordinal) = nullptr;
    const char* (*rooted)(void* user, std::uint32_t id) = nullptr;
    const char* (*faction)(void* user, std::uint32_t index) = nullptr;
    void* user = nullptr;
};

// Write the fact as one line into `out` (always NUL-terminated). Returns the
// length written. A naming that resolves nothing still produces a true
// sentence — "a band killed someone here" — because a chronicle that could
// only speak about things it has names for would be silent about most of the
// world.
int fact_sentence(const WorldFact& f, const FactNaming& naming,
                  char* out, int cap);

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла: заголовок пользуется контуром
// только в блоке ниже, а вставленная наверху строка сдвинула бы всё содержимое
// и сгноила бы ссылки документов в этот файл (§13 п.5).
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА (M-180, ломоть 4 сейв-эпика) ───────────────────
// Судит КОМПИЛЯТОР, а не ревью: структура состояния мира, в которую заложили
// вектор, строку, карту, умный указатель, функтор или виртуальный метод, отсюда
// НЕ СОБЕРЁТСЯ, и сообщение назовёт тип поимённо (`core/row_law.h`, наряд
// M-169). Новая структура в этом файле обязана появиться и в этом списке — за
// полнотой списка следит `arch_guard_test` (файл стоит в его `kRowHeaders`).
TIMAERT_ROW(sm::Chronicle);
TIMAERT_ROW(sm::FactNaming);

// ── СТЕНА «БЕЗ ДЫР» (`TIMAERT_PACKED@src/core/row_law.h`, сейв-эпик, В2) ─────
// Каждый байт этих строк принадлежит названному члену: они лежат в кусках
// памяти, которые снимает прямой дамп (CRAFT §10). Новый член — новая строка
// списка; дыра выравнивания или забытый член не собираются.
TIMAERT_PACKED(sm::Chronicle, &sm::Chronicle::nextSeq, &sm::Chronicle::cols,
    &sm::Chronicle::rows, &sm::Chronicle::countingDay,
    &sm::Chronicle::factsToday, &sm::Chronicle::factsPeak,
    &sm::Chronicle::cellHead, &sm::Chronicle::ring);
