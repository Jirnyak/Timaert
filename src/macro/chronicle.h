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
#include "core/macro_handle.h"   // MacroHandle — «чья история» спрашивается хэндлом

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sm {

// (FactSubject, FactKind и строка WorldFact ПЕРЕЕХАЛИ в bus/fact.h ломтём 3
// эпика фрейма: факт — род 5 канонического фрейма, его форма живёт ниже
// обоих миров. ТАБЛИЦА ДЕЯНИЙ — FactKindDef, kFactKinds, fact_kind_def и
// выведенные из неё законы славы (renown_for_deed, kRenownToBeNamed) —
// ПЕРЕЕХАЛА в tables/fact_kinds.h ломтём 5 M-171: каталог живёт по обе
// стороны границы, летопись — ярусы и двери ниже — осталась макро-слоем.)

// ── ОДИН ДОПИСЫВАЕМЫЙ ЛОГ, И ОН НЕ ЗАБЫВАЕТ (M-423, вердикты владельца
// 2026-11-08; CANON S20.1 «ХРАНЕНИЕ: ОДИН ЛОГ, ТРИ ЦЕПОЧКИ») ────────────────
//
// Dwarf Fortress keeps every historical event of every year in memory, and
// that is its known illness: a long world generates into gigabytes and then
// crawls on its own past. Диагноз владельца точнее расхожего: DF ползёт не от
// ОБЪЁМА истории, а от её ФОРМЫ — событие там есть объект переменной длины со
// строками и ссылками. Здесь запись плоская, 28 байт, и ответ даётся обходом
// массива; поэтому отсечение истории не нужно вовсе.
//
// ЗДЕСЬ СТОЯЛО КОЛЬЦО НА 2^16 ЗАПИСЕЙ, И ОНО ЗАБЫВАЛО: новая запись ложилась
// поверх старейшей. Заворот терял прошлое МОЛЧА и по одной записи — мир,
// который забыл, был неотличим от мира, в котором этого не было, — и протухал
// ссылки, из-за чего память игрока держала 32 МиБ КОПИЙ записей. Кольцо снято
// целиком вместе с тремя счётчиками темпа дня (они калибровали размер кольца
// и другого читателя не имели) и с доказательством живости ссылки по номеру.
//
// ЕЩЁ РАНЬШЕ ЗДЕСЬ ЖИЛ ВТОРОЙ ЯРУС — АННАЛЫ, «что мир помнит навсегда», с
// отбором по именованности; снесён M-184 (вердикт 2026-10-30 «да сноси оба»).
// Вечную память мир получил не вторым ярусом, а тем, что первый перестал
// забывать: отбора, фильтра и яруса «рядом» не существует, пишется ВСЁ.

// КАП ЛОГА — ОДНО ЧИСЛО В ЗАПИСЯХ, НАЗНАЧЕННОЕ ПРОЕКТОМ (ЗАКОН КАПА), а не
// выведенное из замера темпа: 2^22 записей под ориентир «эпоха в 128 игровых
// лет». Нормы «фактов в день» не существует — дни неоднородны, — поэтому кап
// не в днях: бурная эпоха короче календарно, тихая длиннее. Цена памяти:
// 2^22 × 28 Б = 112 МиБ записей в теле состояния мира (едут в сейв) плюс
// 2^22 × 12 Б = 48 МиБ выводных ссылок вне его (`ChronicleLinks` ниже) —
// вместе те самые 40 Б на запись и 160 МиБ, что названы в CANON S20.1.
// Поднять до 2^23 — одна эта строка.
inline constexpr std::uint32_t kChronicleFactsCap = 1u << 22;

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

// ── ЛЕТОПИСЬ — ПЛОСКИЙ ЛОГ В ТЕЛЕ СОСТОЯНИЯ МИРА ───────────────────────────
// Запись с номером `seq` лежит в `facts[seq - 1]` и не двигается никогда:
// номер и есть адрес. Живые записи — первые `nextSeq - 1`, хвост стоит нулём.
// В состоянии лежит только ИСТИНА — сами записи; всё, что из них выводится
// (три цепочки-индекса), живёт вне состояния и в сейв не едет.
struct Chronicle {
    std::uint32_t nextSeq = 1;            // 0 is "no fact", so counting starts at 1
    int cols = 0, rows = 0;               // сетка индексных клеток этого мира
    // СКОЛЬКО ФАКТОВ МИР НЕ СМОГ ЗАПИСАТЬ ПОСЛЕ КОНЦА ЭПОХИ. Ноль, пока лог не
    // исчерпан. Это и есть громкость стопа в самом состоянии мира: отказ
    // записи не теряется молча, он сосчитан и едет в сейв. Читатели названы:
    // панель истории игрока (метка «эпоха кончилась») и свидетель
    // `tests/chronicle_test.cpp`.
    std::uint32_t refused = 0;
    WorldFact     facts[kChronicleFactsCap] = {};   // дописываемый лог

    // Скроена ли летопись под мир: сетка индексных клеток названа.
    bool ready() const { return cols > 0 && rows > 0; }
    // Сколько записей в логе.
    std::uint32_t count() const { return nextSeq - 1u; }
    // ЭПОХА КОНЧИЛАСЬ: лог исчерпан, и новое не пишется (CANON S20.1 — «эпоха
    // кончается по исчерпанию летописи, а не по календарю»). Вывод из счёта,
    // своего байта у ответа нет. Механика конца эпохи ОТКРЫТА и придёт строкой;
    // до неё предел есть громкий стоп, и переименовывать потом нечего.
    bool epoch_over() const { return count() >= kChronicleFactsCap; }
};

// СТОРОЖ РАЗМЕРА (AGENTS DOD п.10). Вывод, а не замер: четыре числа шапки по
// 4 Б (`nextSeq`, `cols`, `rows`, `refused`) и лог записей под капом;
// выравнивание 4, хвоста нет.
static_assert(sizeof(Chronicle)
                  == 4u * 4u
                         + std::size_t(kChronicleFactsCap) * sizeof(WorldFact),
              "летопись: шапка + лог записей под капом, без хвоста");

// Size it for a map. Safe to call again; a resize forgets, which is correct —
// a chronicle of another world is not this world's past. Мир вне капа клеток
// летописи не получает: она остаётся не скроенной, и двери ниже молчат.
void chronicle_init(Chronicle& c, int mapW, int mapH);

// Write one fact. Returns its sequence number, or 0 when nothing was written:
// the chronicle has no world yet, the kind is not a kind, or THE EPOCH IS OVER.
// Every field is a number, so this is one store — the door knows nothing of
// the indexes, and no writer of facts knows they exist.
//
// КОНЕЦ ЛОГА ЕСТЬ ГРОМКИЙ СТОП, И НИКОГДА ЗАВОРОТ — инвариант, а не настройка.
// Исчерпанный лог НЕ ТРОГАЕТ ни одной прежней записи: теряется только новое, и
// теряется громко — счётчиком `refused` в состоянии и строкой в stderr на
// первом отказе (образец — отказ рождения в `store_birth@src/bus/store.h`).
std::uint32_t chronicle_record(Chronicle& c, const WorldFact& fact);

// Запись по её номеру, или nullptr, если такого номера в логе нет.
inline const WorldFact* chronicle_fact(const Chronicle& c, std::uint32_t seq) {
    return seq != 0u && seq < c.nextSeq ? &c.facts[seq - 1u] : nullptr;
}

// (The place-fact door lives in squad.h `record_rooted_fact` — it must
// resolve the place's RENOWN to pay the deed, and a chronicle of ordinals
// must not learn where renown lives.)

// ── ТРИ ЦЕПОЧКИ — ЭТО ИНДЕКСЫ, А НЕ ХРАНИЛИЩА (CANON S20.1) ────────────────
//
// Каждая отвечает своему вопросу:
//   · по КЛЕТКЕ    — «что случилось рядом» (ведьмак у деревни);
//   · по СУБЪЕКТУ  — «что я сделал»;
//   · по ОБЪЕКТУ   — «что сделали со мной».
// Форма у всех одна: голова — номер НОВЕЙШЕГО факта цепочки, ссылка — номер
// следующего, более старого. Обход идёт новейшими вперёд и обрывается на
// первой записи старше окна, поэтому сколь угодно длинная история читателю с
// горизонтом не стоит ничего.
//
// ССЫЛКИ ВЫВОДНЫЕ: НОЛЬ БАЙТ СОСТОЯНИЯ И НОЛЬ БАЙТ СЕЙВА. Сейв есть прямой
// дамп состояния мира, значит выводное обязано лежать ВНЕ состояния — иначе в
// файле лежала бы вторая истина рядом с первой. Экземпляр держит оболочка
// (как каркас сквадов и запечённую навигацию), симуляция видит его адресом.
//
// ПЕРЕСБОРКА — НА ЛЕТУ, КУРСОРОМ (ФРЕЙМ род 5 правило «г»: реакция есть чтение
// с курсора, а не подписка). Индекс есть ЧИТАТЕЛЬ летописи, такой же, как поле
// угрозы: держит номер последней подшитой записи и в момент вопроса догоняет
// лог. Писатель фактов об индексе не знает; вопрос, заданный сразу после
// записи, уже видит её; а «загрузка пересобирает ссылки» есть тот же самый
// догон с нуля, а не отдельный код. Курсоров два, потому что цепочке по клетке
// гладкая память сквадов не нужна, а цепочкам по участнику нужна.
//
// ПАМЯТЬ СКВАДА ЕСТЬ ИНДЕКС В ЭТОМ ЖЕ ХРАНИЛИЩЕ, И ОНА УМИРАЕТ СО СКВАДОМ
// (вердикт владельца 2026-11-08: «не надо следить сквады а писать прям в
// летопись вместо двух мест хранений прост оуказывать какой сквад»). Голова
// цепочки лежит по СЛОТУ сквада; ординалы не переиспользуются
// (`MacroStore@src/bus/store.h`, эмитент монотонен), поэтому голова, чей факт
// называет не нынешнего жильца слота, есть цепочка прежнего жильца — и она
// читается пустой. Стирается ДОСТУП, не ИСТОРИЯ: записи остаются в логе,
// потому что летопись — часть МИРА, а не часть сквада.
//
// УЧАСТНИК ЦЕПОЧКИ — СКВАД (роды субъекта `Squad` и `Rooted`: пространство
// ординалов одно, место есть неподвижный сквад). У клетки цепочка своя; у
// фракции цепочки нет, пока у неё нет читателя.
struct ChronicleLinks {
    // Номер последней записи, подшитой в цепочку по клетке / в цепочки по
    // участнику. Ноль — «ничего не подшито»: следующий вопрос пересоберёт всё.
    std::uint32_t cellSynced  = 0;
    std::uint32_t partySynced = 0;
    std::uint32_t cellHead[kChronicleIndexCells] = {};   // по индексной клетке
    std::uint32_t subjectHead[kUnifiedCap] = {};         // по слоту сквада
    std::uint32_t objectHead[kUnifiedCap] = {};          // по слоту сквада
    // Ссылка записи `seq` лежит в `[seq - 1]` — тот же адрес, что у записи.
    std::uint32_t nextInCell[kChronicleFactsCap];
    std::uint32_t nextInSubject[kChronicleFactsCap];
    std::uint32_t nextInObject[kChronicleFactsCap];
};
static_assert(sizeof(ChronicleLinks)
                  == 2u * 4u + 4u * kChronicleIndexCells
                         + 2u * 4u * std::size_t(kUnifiedCap)
                         + 3u * 4u * std::size_t(kChronicleFactsCap),
              "индекс летописи: два курсора, три массива голов и три массива "
              "ссылок по 4 Б на запись капа");

// Индекс живёт в куче: 48 МиБ на стеке не бывает. Рождается пустым.
std::unique_ptr<ChronicleLinks> make_chronicle_links();

// ЗАБЫТЬ ВЫВЕДЕННОЕ: следующий вопрос пересоберёт цепочки на лету от первой
// записи лога. Зовётся там, где летопись ЗАМЕНЕНА ЦЕЛИКОМ (загрузка сейва,
// рождение нового мира): сам индекс этого узнать не может, если новый лог не
// короче прежнего. Работы здесь O(голов), а не O(записей) — пересборка
// платится в момент первого вопроса и только за то, о чём спросили.
void chronicle_rebuild_links(ChronicleLinks& links);

// ── The question the witcher asks ────────────────────────────────────────
//
// Walks the index cells around (x, y), newest first, and hands every fact no
// older than `sinceDay` to `visit`. Returns how many it offered.
using FactVisitor = void (*)(void* user, const WorldFact& fact);
int chronicle_near(const Chronicle& c, ChronicleLinks& links,
                   int x, int y, int radiusCells,
                   std::int32_t sinceDay, FactVisitor visit, void* user);

// The same walk over the WHOLE LOG, newest first — режим легенд и приборы.
// Индекс ему не нужен: лог записан в порядке времени.
int chronicle_recent(const Chronicle& c, std::int32_t sinceDay, int limit,
                     FactVisitor visit, void* user);

// ── Вопросы сквада о себе ────────────────────────────────────────────────
//
// «Что я сделал» — факты, где `who` назван СУБЪЕКТОМ; «что сделали со мной» —
// где он назван ОБЪЕКТОМ; «моя история» — обе цепочки, слитые по номеру,
// новейшими вперёд (факт, где он и субъект, и объект, подаётся один раз).
// Не старше `sinceDay`, не больше `limit` записей. Протухший хэндл и сквад без
// истории отвечают нулём.
//
// ЭТО И ЕСТЬ ПАМЯТЬ ИГРОКА: его история — цепочка того сквада, которым он
// играет СЕЙЧАС, и ничего своего у игрока здесь нет. Здесь стоял журнал — 2^20
// копий записей в состоянии игрока со своим курсором и своим громким капом;
// копии были платой за забывающее кольцо и ушли вместе с ним.
struct MacroStore;
int chronicle_by_subject(const Chronicle& c, ChronicleLinks& links,
                         const MacroStore& st, MacroHandle who,
                         std::int32_t sinceDay, int limit,
                         FactVisitor visit, void* user);
int chronicle_by_object(const Chronicle& c, ChronicleLinks& links,
                        const MacroStore& st, MacroHandle who,
                        std::int32_t sinceDay, int limit,
                        FactVisitor visit, void* user);
int chronicle_about(const Chronicle& c, ChronicleLinks& links,
                    const MacroStore& st, MacroHandle who,
                    std::int32_t sinceDay, int limit,
                    FactVisitor visit, void* user);

// ── ASKING IN THE TABLE'S OWN WORDS ──────────────────────────────────────
//
// "Recent" is not one number for the whole game: a killing is news for a
// season and a sale is stale in a week, and the kind's row already says so
// (`interestDays`). This asks the question the way the world means it —
// "what of THIS sort happened near here while it was still news" — so a
// reader never restates a window the table already owns.
int chronicle_near_kind(const Chronicle& c, ChronicleLinks& links,
                        int x, int y, int radiusCells,
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
TIMAERT_ROW(sm::ChronicleLinks);
TIMAERT_ROW(sm::FactNaming);

// ── СТЕНА «БЕЗ ДЫР» (`TIMAERT_PACKED@src/core/row_law.h`, сейв-эпик, В2) ─────
// Каждый байт этих строк принадлежит названному члену: они лежат в кусках
// памяти, которые снимает прямой дамп (CRAFT §10). Новый член — новая строка
// списка; дыра выравнивания или забытый член не собираются. Индекс
// (`ChronicleLinks`) под стеной не стоит намеренно: он в дамп не едет.
TIMAERT_PACKED(sm::Chronicle, &sm::Chronicle::nextSeq, &sm::Chronicle::cols,
    &sm::Chronicle::rows, &sm::Chronicle::refused, &sm::Chronicle::facts);
