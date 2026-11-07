// Маршрутные поля курса субмира (M-420, эпик ЗАХВАТА M-419) — одна лёгкая
// система «куда идти» для тысяч тел: весовые потоковые поля над сеткой ячеек
// 8×8 тайлов, НЕ пер-акторные пути. Вердикт владельца 2026-11-07: «нам нужно
// ЛЕГКОЕ быстрое но удобное … чтобы это не были тупые прямолинейные (а по
// весам поверхностей например и учетом преград)»; «если есть дорога в субмре
// то ятнулись к ней … по этому маршруту они будут минимизировать вес
// поврехности то есть бесплатно сами потянутсмя к дороге».
//
// Форма — слияние двух прецедентов дерева: память как у боевого поля
// (InfluenceField@src/sub/movement.h — фикс-колонки по капу, рождение
// фабрикой на куче, static_assert(sizeof)), содержимое как у макро-походки
// (nav_field: шаг-румб u8 к цели, свежесть событием, derived — в сейв не
// едет). Прайс-лист ОДИН с макромаршем: вес ячейки есть агрегат
// kTileGroundWeight@src/sub/map_data.h — второго словаря цены земли не
// существует (ЗАКОН СЛОВАРЯ / «одна карта, один прайс-лист»).
//
// ЦЕЛИ КВАНТОВАНЫ, ПОЭТОМУ ПОЛЕЙ МАЛО: 8 общих полей «к кромке по румбу»
// (все транзитные тела разом) + поле на уникальную ЯЧЕЙКУ-цель в окне
// (город/жила/дверь — делят все идущие туда). Тело читает СВОЮ ячейку →
// направление спуска стоимости → интент, O(1) на тело.
//
// МЕСТО В ИЕРАРХИИ ВОЛИ: курс пишет ТОЛЬКО собственное намерение тела
// (BodyDesc::intentVx/Vy) — война сильнее курса ПО ПОСТРОЕНИЮ, потому что
// боевой привод вытесняет намерение без смешивания («ВОЙНА ТЕЛА НЕ
// СПРАШИВАЕТ», sub/movement.cpp). Паника (Flee с живым словом) сильнее
// курса; курс сильнее прогулки (wander). Решают звонящие — это их словарь.
//
// НЕПРОХОДИМОСТИ НЕТ — ЕСТЬ СВЕРХДОРОГАЯ СТРОКА (вердикт владельца
// 2026-11-07, развилка ломтя 3: «сверхдорогая строка»): солидный тайл
// строения входит в агрегат весом kCourseSolidTileWeight, как вода входит
// в макро-веса самой дорогой строкой. Ворота выпадают САМИ — ячейка с
// проёмом дешевле глухой, и Дейкстра ведёт в проём; физику держит
// resolve_step, поле лишь советует. Скала (TILE_ROCK) проходима весом
// 5.0 — это закон мира (стены по уклону в субмире нет), а не недоделка.
#pragma once
#include <array>
#include <cstdint>
#include <memory>

#include "sub/map_data.h"

namespace sm::sub {

// Ячейка курса — 8×8 тайлов. Вывод (ряд M-420): тайл слишком мелко (×64
// работы без выгоды — огибать надо строения, не травинки), клетка 1024
// слишком крупно (дом меньше ячейки — огибание невыразимо); 8 — po2,
// перевод тайл→ячейка есть сдвиг.
inline constexpr int kCourseCellTiles = 8;
inline constexpr int kCourseCellShift = 3;
static_assert((1 << kCourseCellShift) == kCourseCellTiles,
              "сдвиг и размер ячейки курса — одно число");
inline constexpr int kCourseDim = kFullSize / kCourseCellTiles;   // 384
inline constexpr int kCourseCells = kCourseDim * kCourseDim;      // 147 456
static_assert(kCourseDim * kCourseCellTiles == kFullSize,
              "сетка курса замощает окно без остатка");

// «Шага нет» — ПОСЛЕДНЕЕ значение типа (ЗАКОН УЗКОГО ИНДЕКСА: не −1);
// законные шаги 0..7 — Dir@src/sub/map_data.h, тот же порядок румбов.
inline constexpr std::uint8_t kCourseNoStep = 255u;

// Вес ячейки хранится в КВАНТАХ 1/8 веса макрозакона: u16, потому что
// солидная ячейка (40.0 × 8 = 320) в байт не лезет, а резать квант ради
// байта запрещено (DOD п.2 — сужение должно достигать sizeof, здесь не
// достигает: колонка одна).
inline constexpr int kCourseWeightQuantum = 8;

// Солидный тайл строения в агрегате веса — сверхдорогая строка, НЕ стена
// (вердикт владельца 2026-11-07; родня «вода — самая дорогая строка»).
// 4× воды: сквозь кладку вчетверо дороже, чем вплавь, — обход любой
// реальной стены дешевле, а в замкнутый двор поле честно ведёт до стены.
// Баланс (не по-двойка): менять можно, систему под число не подгонять.
inline constexpr float kCourseSolidTileWeight =
    4.0f * kTileGroundWeight[TILE_WATER];   // 40.0

// Проём: солид, поднятый НАД ростом колонны тела, телу не стена (перемычка
// ворот — тела проходят под ней); ниже — бьётся торс. Высота колонны
// человека ~1.8 м (sub/body.h man-column), порог с запасом.
inline constexpr float kCourseLintelClearM = 2.0f;

// Полей МАЛО, и их счёт именован: 8 румбовых (по числу Dir) + кап
// одновременных уникальных точек-целей в окне. 16 — стартовый кап
// (po2 ничего здесь не покупает — это просто кап слотов); переполнение НЕ
// молчит: вытеснение LRU считается счётчиком pointOverflows (прецедент
// portalOverflows@src/macro/nav_field.h), вытесненный потребитель падает в
// свой фолбэк. Упёрлись — поднимаем числом, не обходом (ЗАКОН КАПА).
inline constexpr int kCourseRumbFields = 8;
inline constexpr int kCoursePointFields = 16;
inline constexpr int kCourseFieldCount = kCourseRumbFields + kCoursePointFields;

// Цена ребра — вес ячейки-входа × длину шага в ПОЛУтайлах ячейки: 2 —
// кардинал, 3 — диагональ (3/2 = 1.5 ≈ √2, погрешность 6% — целыми,
// ЗАКОН ТИПА: флоату в содержимом поля делать нечего).
inline constexpr std::uint32_t kCourseStepCardinal = 2;
inline constexpr std::uint32_t kCourseStepDiagonal = 3;

// Кольцо корзин Дейкстры Диала — po2 СТРОГО БОЛЬШЕ максимального ребра
// (солидная ячейка 320 квантов × диагональ 3 = 960): пока это так, две
// живые дистанции не делят корзину, и очередь точна по построению.
inline constexpr std::uint32_t kCourseBucketRing = 1024;
static_assert(kCourseBucketRing >
                  std::uint32_t(kCourseSolidTileWeight) *
                      std::uint32_t(kCourseWeightQuantum) * kCourseStepDiagonal,
              "кольцо Диала обязано вмещать максимальное ребро");

// Пул записей очереди: толчок случается не чаще, чем улучшение дистанции
// по ребру, рёбер у клетки 8, улучшение по ребру — не чаще одного на
// осадку источника, значит толчков ≤ 8N ПО ПОСТРОЕНИЮ — переполнение
// невыразимо, ветки «пул кончился» не существует.
inline constexpr std::uint32_t kCourseEntryCap = 8u * std::uint32_t(kCourseCells);

// «Записи нет» в цепочках пула — последнее значение типа.
inline constexpr std::uint32_t kCourseNone = 0xFFFFFFFFu;

// Единичные векторы румбов (порядок Dir) — чтение интента из шага.
inline constexpr float kCourseDiag = 0.70710678f;   // 1/√2
inline constexpr float kCourseDirUx[8] = {
    0.0f,  kCourseDiag, 1.0f,  kCourseDiag,
    0.0f, -kCourseDiag, -1.0f, -kCourseDiag,
};
inline constexpr float kCourseDirUy[8] = {
    -1.0f, -kCourseDiag, 0.0f,  kCourseDiag,
    1.0f,  kCourseDiag,  0.0f, -kCourseDiag,
};

// Ячейка курса точки сцены (тайловые координаты окна). Кламп — страж
// области определения: тела не покидают композита по построению.
inline int course_cell_of(float xTiles, float yTiles) {
    int cx = int(xTiles) >> kCourseCellShift;
    int cy = int(yTiles) >> kCourseCellShift;
    cx = cx < 0 ? 0 : (cx >= kCourseDim ? kCourseDim - 1 : cx);
    cy = cy < 0 ? 0 : (cy >= kCourseDim ? kCourseDim - 1 : cy);
    return cy * kCourseDim + cx;
}

// Румб по знакам векторного смещения (−1/0/+1 на ось) → ординал Dir;
// нулевая пара законного румба не имеет — спрашивать её запрещено.
inline constexpr std::uint8_t course_rumb_of(int sx, int sy) {
    for (std::uint8_t d = 0; d < 8; ++d) {
        if (kDirOffsets[d][0] == sx && kDirOffsets[d][1] == sy) return d;
    }
    return kCourseNoStep;
}

// Одно поле: шаг-румб к цели на каждую ячейку (kCourseNoStep = цель здесь
// или ячейка ещё не осажена бейком).
struct CourseField {
    std::array<std::uint8_t, kCourseCells> step;
};

// Паспорт слота поля.
struct CourseFieldMeta {
    std::uint32_t key = 0;          // румбовый слот: сам румб; точечный: ячейка цели
    std::uint32_t lastUseTick = 0;  // для LRU-вытеснения точечных слотов
    std::uint8_t  state = 0;        // CourseFieldState
    std::uint8_t  pad0 = 0;         // паддинг назван (сейв его не видит — derived)
    std::uint16_t pad1 = 0;
};
static_assert(sizeof(CourseFieldMeta) == 12, "паспорт слота поля — 12 Б");

enum CourseFieldState : std::uint8_t {
    kCourseEmpty = 0,    // слот свободен (точечный) / никогда не спрошен (румбовый)
    kCourseWanted = 1,   // спрошен, ждёт бейка (или протух инвалидацией)
    kCourseFresh = 2,    // печён по свежим весам — читается
};

// ── Весь мир курса — ОДИН плоский блок (ЗАКОН ГЛАДКОЙ ПАМЯТИ / ЗАКОН
// СТАБИЛЬНОСТИ: профиль пустой улицы и полной один; рождается фабрикой на
// куче — капозависимое тело в заголовке с именем не живёт, §5 п.13). ──────
struct CourseWorld {
    // Веса ячеек в квантах 1/8 (агрегат 64 тайлов; солид — спецстрока).
    std::array<std::uint16_t, kCourseCells> weightQ;
    // Бит «тайл накрыт солидным строением» — растр OBB-футпринтов,
    // перештампованный на каждое поколение весов.
    std::array<std::uint64_t,
               std::size_t(kFullSize) * std::size_t(kFullSize) / 64u> solidBits;
    // Поля: [0..7] — румбовые (ключ = румб), дальше — точечные.
    std::array<CourseField, kCourseFieldCount> fields;
    std::array<CourseFieldMeta, kCourseFieldCount> meta;
    // Скретч бейка (Диал): дистанции + пул записей очереди + кольцо корзин.
    std::array<std::uint32_t, kCourseCells> dist;
    std::array<std::uint32_t, kCourseEntryCap> entryNode;
    std::array<std::uint32_t, kCourseEntryCap> entryNext;
    std::array<std::uint32_t, kCourseBucketRing> bucketHead;
    // Прогресс чанкованной пересборки весов: строк ячеек готово;
    // kCourseDim = веса свежи. Солиды штампуются нулевым чанком.
    std::int32_t weightRowsDone = 0;
    // Счётчик LRU-вытеснений точечных слотов — кап не молчит (§ «no silent caps»).
    std::uint32_t pointOverflows = 0;

    CourseWorld(const CourseWorld&) = delete;
    CourseWorld& operator=(const CourseWorld&) = delete;
    CourseWorld(CourseWorld&&) = delete;
    CourseWorld& operator=(CourseWorld&&) = delete;

private:
    CourseWorld() = default;
    friend std::unique_ptr<CourseWorld> make_course_world();
};
// Память названа и прибита: ~15.1 МиБ одним блоком — тот же разряд, что
// боевое поле (17.3 МиБ), оплачено один раз при рождении движка (DOD п.2:
// десятки МиБ бесплатны; считаем, чтобы картина была видна целиком).
static_assert(sizeof(CourseWorld) ==
                  std::size_t(kCourseCells) * 2u                      // weightQ
                + std::size_t(kFullSize) * std::size_t(kFullSize) / 8u // solidBits
                + std::size_t(kCourseFieldCount) * std::size_t(kCourseCells)
                + std::size_t(kCourseFieldCount) * sizeof(CourseFieldMeta)
                + std::size_t(kCourseCells) * 4u                      // dist
                + std::size_t(kCourseEntryCap) * 8u                   // пул ×2
                + std::size_t(kCourseBucketRing) * 4u
                + 8u,                                                 // хвост скаляров
              "мир курса — один плоский блок, ничего спрятанного");

// Рождение на куче (образец make_body_crowd): `CourseWorld c;` в кадре
// стека не собирается.
std::unique_ptr<CourseWorld> make_course_world();

// Протухание ВСЕГО (ре-центр окна, смена набора строений, вход в сцену):
// веса — заново нулевым чанком, свежие поля — в Wanted (ключи живут,
// перепекутся бюджетом), точечные Empty остаются пустыми.
void course_invalidate(CourseWorld& cw);

// Шаг обслуживания, РАЗ В ТИК: пока веса не свежи — один чанк строк весов
// (нулевой чанк дополнительно штампует солиды); затем — не больше ОДНОГО
// бейка поля за тик. Амортизация по построению: рывка «всё пересобрать в
// один тик» не существует (ЗАКОН СТАБИЛЬНОСТИ), недопечённый потребитель
// живёт фолбэком, работа не выкидывается, а доделывается.
void course_maintain(CourseWorld& cw, const std::uint8_t* tiles,
                     const Structure* structs, std::size_t structCount);

// Направление спуска румбового поля (к кромке окна по румбу 0..7) из точки
// сцены (тайлы). Поле не свежо → помечает спрос и отвечает kCourseNoStep —
// фолбэк решает звонящий.
std::uint8_t course_dir_rumb(CourseWorld& cw, std::uint8_t rumb,
                             float xTiles, float yTiles);

// Направление спуска точечного поля к ячейке-цели (tx,ty — точка цели в
// тайлах окна). Слот ищется по ключу-ячейке, свободного нет — вытесняется
// LRU со счётчиком. Поле не свежо → kCourseNoStep.
std::uint8_t course_dir_point(CourseWorld& cw, float txTiles, float tyTiles,
                              float xTiles, float yTiles,
                              std::uint32_t nowTick);

} // namespace sm::sub
