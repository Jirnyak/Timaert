#include "core/field_noise.h"
#include "core/math.h"
#include "core/torus.h"
#include "macro/map_generator.h"
#include "macro/plates.h"
#include "tables/biomes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace sm {

// ---- Climate master synth (CPU) ----
//
// The master climate texture -- height (R), moisture (G), temperature (B) and
// land mask (A) -- is generated on the CPU. It was formerly a GL FBO fragment
// pass read back into td.rgba; the CPU port removes the GPU->CPU readback stall
// and the OpenGL dependency ahead of the Vulkan cutover while producing the
// same periodic-Perlin / fBM field (a faithful port of `src/webgl/shaders.ts`).
// Periodic Perlin keeps the master seamless on the toroidal world.

namespace {

// The noise stack (kPerm, periodic_noise, terrain_fbm) moved VERBATIM to
// core/field_noise.h (v72): the geology field law reads the same door, and
// a private copy here would be the second implementation S26 forbids.

// Синтез → слово карты. ОКРУГЛЕНИЕ, в отличие от `field_word_of` (там
// floor по закону порога): здесь величина не сравнивается, а записывается, и
// ближайшее слово честнее усечённого.
inline std::uint16_t to_unorm16(float v) {
    return std::uint16_t(std::lround(std::clamp(v, 0.0f, 1.0f) * kFieldWordMax));
}

// ── ГРЕБНЕВАЯ ОКТАВА: ДЛИНА ВОЛНЫ МАСШТАБА ХРЕБТА ───────────────────────────
// Замер 2026-10-01 (`height_census`) назвал корень числом: 87 % дисперсии поля
// лежит на масштабах ≥32 клеток, ниже 8 клеток — 4.3 %, и самый крутой переход
// между клетками во всём мире 4.92°. Прежний член хребтов шёл по домену ×0.75,
// то есть его мельчайшая деталь была 43 КЛЕТКИ: приём `1-|n|` верен, но был
// применён на континентальном масштабе — заострять внизу было нечего.
//
// Домен ×4 растягивает те же 8 единиц карты на 32, то есть одна единица решётки
// = 32 клетки; при четырёх октавах длины волн выходят 32, 16, 8 и 4 клетки.
// Длинная октава нужна не ради неё самой: она ОРГАНИЗУЕТ цепь, вдоль которой
// короткие ставят кресты, — без неё выходят отдельные зубья, а не хребет.
// Почему не 2 клетки, хотя владелец назвал «2–8»: поле СЭМПЛИРУЕТСЯ раз в
// клетку, значит волна в 2 клетки стоит ровно на Найквисте и рисует не гребень,
// а шахматную рябь. 4 клетки — честный пол.
//
// Период обязан быть ЦЕЛЫМ числом клеток решётки (шрам: пара 0.7/5.6 ставила по
// три обрыва в каждом мире). 8 × 4 = 32 целое, и октавы 32/64/128/256 тоже — а
// верхняя из них не переваливает за 256, то есть таблица градиентов Перлина
// (`& 255` в `periodic_noise`) ни на одной октаве не сворачивается дважды
// внутри одного мира.
constexpr float kRidgeDomainScale = 4.0f;
constexpr float kRidgeBasePeriod  = 8.0f * kRidgeDomainScale;  // = 32, целое
constexpr int   kRidgeOctaves     = 4;                    // 32 / 16 / 8 / 4 клетки
static_assert(kRidgeBasePeriod * 8.0f <= 256.0f,
              "верхняя октава гребней обязана уместиться в 256 — иначе таблица "
              "градиентов повторится дважды внутри мира");

// ── ГДЕ ГРЕБНЯМ ЖИТЬ: НА ШВАХ ПЛИТ, А НЕ ПО ВСЕЙ ВЕРХНЕЙ ТРЕТИ ──────────────
//
// Здесь стояло `massif = smoothstep((uplift − 0.50)/0.30)` — гребень включала
// сама ВЫСОТА, то есть он ложился ровным слоем на верхнюю треть континента.
// Замер назвал цену: `ridged_fbm` в игровом домене держит выше половины своей
// амплитуды **30.3 %** карты (p50 0.354), то есть «горы» были пологим валом на
// трети мира, а не грядами. Владелец видел ровно это: «горы если и видны, то
// как небольшие возвышенности».
//
// Теперь место гряды решает КАРКАС ПЛИТ (`macro/plates.h`), и у отбора три
// сомножителя, каждый со своим вопросом:
//   · ШОВ      — «край ли это плиты» (плиты);
//   · СЖАТИЕ   — «сходятся ли плиты здесь» (плиты). Без него гряда встаёт на
//                КАЖДОМ шве и мир вырождается в соты — поймано картинкой
//                уклонов прототипа, числа при этом хвалили;
//   · КОНТИНЕНТ— «суша ли это вообще». Берётся от ПЛОСКОСТИ МОРЯ карты, а не
//                своей парой чисел: гряда начинается там, где поднятие
//                переваливает уровень моря, и ширина перехода — единственное
//                число, которое тут своё.

// Порог сходимости: выше него шов рождает горы, ниже — расходится без гор.
// Замер прототипа (сид 12345, пролёт 64): −0.08 даёт долю суши с «видом
// долины» 18.9 %, −0.03 → 11.8 %, 0.00 → 10.4 %. Отрицательный, потому что
// сходимость симметрична вокруг нуля, а гряд нужно больше половины швов.
constexpr float kOrogenSqueezeLevel = -0.08f;
// Мягкость этого отбора. Жёсткий порог оборвал бы хребет поперёк там, где
// сходимость проходит через него; треть размаха даёт хребту затухать концами.
constexpr float kOrogenSqueezeBand = 0.30f;

// ── АТТРАКТОР: КАРТА ПРИТЯГИВАЕТ ХРЕБТЫ, А НЕ ТОЛЬКО ПОДНИМАЕТ ЗЕМЛЮ ───────
// Во сколько сдвиг уровня плиты двигает ПОРОГ сходимости. Выведено, а не
// назначено: полностью белый пиксель (`kPlateBiasFullScale`) обязан сдвинуть
// порог на ВСЮ его мягкость (`kOrogenSqueezeBand`), то есть превратить почти
// любой шов в гряду; полностью чёрный — на столько же вверх, то есть не
// оставить ни одной. Серое не двигает ничего, поэтому мир без карты рождается
// ровно как сегодня.
constexpr float kOrogenAttractGain = kOrogenSqueezeBand / kPlateBiasFullScale;

// Ширина перехода «не суша → суша» в единицах поднятия. Одно число вместо
// прежней пары: нижний конец есть плоскость моря самой карты.
constexpr float kOrogenLandBand = 0.10f;

// ── ШИРИНА ГРЯДЫ НЕ ПОСТОЯННА ──────────────────────────────────────────────
// Постоянная читается штампованной: подножие идёт ровной кривой и обнажает
// фаски ячеек каркаса. Модулируется тем же полем детали, которое синтез уже
// посчитал, — нового шума нет. Размах 1.30 вокруг единицы: шов гуляет от 0.35
// до 1.65 обычной ширины.
constexpr float kSeamWidthJitter = 1.30f;

// ── УРОВЕНЬ ПОДНОЖИЯ ГРЯДЫ ─────────────────────────────────────────────────
// Гребень больше НЕ ПРИБАВЛЯЕТ в запас до потолка — он задаёт УРОВЕНЬ массива,
// а `ridged` лепит его силуэт от подножия до вершины. Прежняя форма
// `h += (1−h)·ridged·…` имела потолок по построению: типичное `ridged` вдоль
// гребня 0.35…0.68, значит выше ≈0.77 поля (2.8 км) она не доставала, и форму
// креста начинал лепить сам потолок.
// 0.82 поля есть 4.0 км над морем по кривой переноса (`height_m@src/sub/height.h`);
// вершина при `ridged`→1 уходит к 13.8 км. Выпуклая комбинация ниже из [0,1]
// не выходит ни при каком весе, то есть потолок стал НЕВЫРАЗИМ, а не огорожен.
constexpr float kRangeFootLevel = 0.82f;

// ── ДНО ДОЛИНЫ ─────────────────────────────────────────────────────────────
// Доля холмовой амплитуды ВНЕ гряды. Гашение среднесохраняющее (деталь
// симметрична вокруг 0.5 по построению), поэтому уровень оно не двигает — оно
// делает равнину равниной: замер прототипа даёт уклон суши p50 0.90° → 0.49°,
// то есть на 30 км долины земля уходит на ~250 м вместо ~480.
constexpr float kFloorDetailShare = 0.50f;

// Fill td.rgba with the climate master: height (R), moisture (G),
// temperature (B) and land mask (A). Faithful CPU port of the former GL synth.
void synth_master(TerrainData& td, const LayerParameters& p, PlateMap& plates) {
    const int w = td.width, h = td.height;
    // The one seed→float cast, at the noise's own door: every UI seed is
    // decimated below 100000 (main.cpp), so the float is exact and the synth
    // is bit-identical to what the old float-typed seed produced.
    const float seed      = float(p.seed);
    const int   heightOct = int(p.heightOctaves);
    const int   moistOct  = int(p.moistureOctaves);
    const float cScale    = std::max(0.001f, p.continentScale);
    // Маска A пишется ТЕМ ЖЕ порогом, что отвечает на «вода ли клетка»
    // (`TerrainData::is_water`): здесь стоял float-компаратор
    // `noiseHeight < p.seaLevel` ДО квантования, то есть девятый спеллинг
    // одного вопроса — он мог не совпасть с байтовым на округлении.
    const std::uint16_t sea16 = field_word_of(p.seaLevel);
    for (int y = 0; y < h; ++y) {
        const float uy = (float(y) + 0.5f) / float(h);
        for (int x = 0; x < w; ++x) {
            const float ux   = (float(x) + 0.5f) / float(w);
            const float posX = ux * 8.0f;
            const float posY = uy * 8.0f;

            // Domain warp (period 8 -> tiles cleanly).
            const float warpX = terrain_fbm(posX, posY, 3, 0.5f, 8.0f, seed + 50.0f);
            const float warpY = terrain_fbm(posX + 5.2f, posY + 1.3f, 3, 0.5f, 8.0f, seed + 60.0f);
            const float qx = warpX * p.domainWarp;
            const float qy = warpY * p.domainWarp;

            // ── ПОДНЯТИЕ — ПОЛЕ УРОВНЯ, А НЕ СЛАГАЕМОЕ ──────────────────
            // Здесь стоял `noiseHeight += continentBias * 0.40` поверх поля,
            // уже занимавшего [0,1], и следом `clamp(0,1)`. Замер назвал цену
            // числом: 5.64 % суши (сид 1 — 7.80 %) стояло РОВНО на 1.0, то есть
            // вершины мира были ПЛОСКИМИ СТОЛАМИ по построению, и независимый
            // прибор это подтверждал — в горном биоме уклоны выходили МЕНЬШЕ,
            // чем на остальной суше (p90 17.6 м против 23.5 м). Растянуть
            // плоский стол никакая вертикальная шкала не может, поэтому кламп
            // снимается не настройкой, а формой: поднятие задаёт УРОВЕНЬ, а
            // деталь живёт вокруг него, и каждый шаг — lerp, то есть выйти за
            // [0,1] поле больше НЕ УМЕЕТ.
            //
            // Вес выводится из авторской ручки, а не назначается заново:
            // старое `d + c·(2u−1)` жило в [−c, 1+c], и сжатие этого отрезка в
            // [0,1] есть ровно `lerp(d, u, 2c/(1+2c))`. При дефолтном c = 0.40
            // вес 0.444, то есть поле СТРУКТУРНО то же, что шипуется сегодня,
            // только больше не срезано сверху.
            const float uplift =
                terrain_fbm(posX * cScale, posY * cScale, 2, 0.5f,
                            8.0f * cScale, seed + 700.0f) * 0.5f + 0.5f;
            const float upliftWeight =
                2.0f * p.continentIntensity / (1.0f + 2.0f * p.continentIntensity);

            // Гладкая деталь — та же, что была базовой высотой.
            const float smoothDetail =
                terrain_fbm(posX + qx, posY + qy, heightOct, 0.5f, 8.0f, seed)
                * 0.5f + 0.5f;

            // Гребневая деталь — своя дверь (`ridged_fbm`): ридж берётся на
            // КАЖДОЙ октаве, иначе выходит мятая фольга, а не хребты, и это
            // поймано числом (p99/p50 уклона в горах падало 6.0 → 4.1). Домен не
            // искривляется warp'ом сознательно: смещение в 0.3 единицы здесь
            // равно 38 клеткам и размололо бы четырёхклеточный гребень в кашу.
            const float ridged =
                ridged_fbm(posX * kRidgeDomainScale, posY * kRidgeDomainScale,
                           kRidgeOctaves, 0.55f, kRidgeBasePeriod, seed + 800.0f);

            // ── КАРКАС ПЛИТ: ГДЕ ШОВ, НАСКОЛЬКО СЖАТО, ЧЬЯ КЛЕТКА ────────
            // Запрос искривляется ПОЛЕМ ИСКАЖЕНИЯ, которое уже посчитано выше:
            // прямой шов ячейки читается рукотворным. Ширина шва модулируется
            // той же деталью — подножие становится рваным даром.
            const float plateWarp = float(kPlateWarpCells);
            const PlateSample plate = plate_sample(
                plates,
                ux + warpX * plateWarp / float(w),
                uy + warpY * plateWarp / float(h),
                1.0f + (smoothDetail - 0.5f) * kSeamWidthJitter);

            // ОТБОР: шов × сходимость × суша. Три вопроса, три сомножителя.
            // ПОРОГ СХОДИМОСТИ ДВИГАЕТ КАРТА. Белое опускает его (швы охотно
            // становятся грядами), тёмное поднимает (гор нет), серое не
            // двигает — значит мир без карты рождается ровно как сегодня.
            const float squeezeGate = kOrogenSqueezeLevel
                                    - plate.levelBias * kOrogenAttractGain;
            const float orogen =
                smoothstep01(plate.seam)
                * smoothstep01((plate.squeeze - squeezeGate) / kOrogenSqueezeBand)
                * smoothstep01((uplift - p.seaLevel) / kOrogenLandBand);

            // ── ДНО ДОЛИНЫ: ХОЛМЫ ГАСНУТ ТАМ, ГДЕ НЕТ ГРЯДЫ ──────────────
            // Среднесохраняюще: `smoothDetail` симметрична вокруг 0.5 по
            // построению, поэтому уровень не двигается, двигается РАЗМАХ.
            const float hills =
                0.5f + (smoothDetail - 0.5f) * lerp(kFloorDetailShare, 1.0f, orogen);

            // ── ГРЕБЕНЬ ЗАДАЁТ УРОВЕНЬ МАССИВА ───────────────────────────
            // Первая редакция лерпила деталь В СТОРОНУ гребневого поля и
            // сделала хуже, чем было: у мультифрактала основная масса лежит у
            // нуля, поэтому `lerp` к нему ТЯНУЛ рельеф ВНИЗ (уклон гор p90
            // 36.5 м → 17.6 м). Вторая прибавляла в запас до потолка и
            // упиралась в него (см. `kRangeFootLevel`). Здесь гребень лепит
            // СИЛУЭТ массива между подножием и небом, а вес лерпа говорит,
            // насколько этот массив вообще поднялся.
            const float massifTop = lerp(kRangeFootLevel, 1.0f, ridged);

            // Сдвиг уровня от авторской карты — ЕДИНСТВЕННОЕ, что она делает.
            // Сегодня он ноль у всех плит, и поле рождается как прежде.
            float noiseHeight = lerp(hills, uplift, upliftWeight)
                              + plate.levelBias;
            noiseHeight = lerp(noiseHeight, massifTop,
                               orogen * p.ridgeIntensity);
            noiseHeight = std::pow(noiseHeight, p.heightScale);

            // ПОЛЕ ОРДИНАЛА ПИШЕТСЯ ЗДЕСЬ, И ТОЛЬКО ЗДЕСЬ. Второй двери
            // выпечки нет намеренно: она пекла бы по НЕискривлённому запросу,
            // то есть завела бы второй ответ на «чья это клетка».
            // Адрес клетки — ОДНО ЧИСЛО через каноническую дверь, даже когда
            // сворачивать нечего: рукописный `y*w + x` здесь поймал
            // `world_fold_law_test`, и поймал по делу — второй спеллинг адреса
            // живёт ровно до первого читателя, который решит, что «тор и так
            // тор» (ЗАКОН АДРЕСА).
            if (!plates.cell.empty())
                plates.cell[std::size_t(cell_of(x, y, w))] = plate.nearest;

            // Moisture (period 4 -> tiles twice).
            float noiseMoist = terrain_fbm(posX + qx * 0.5f, posY + qy * 0.5f, moistOct, 0.5f, 4.0f, seed + 200.0f) * 0.5f + 0.5f;
            noiseMoist = std::pow(noiseMoist, p.moistureScale);

            // Temperature: latitude-driven with a noise contribution.
            //
            // Latitude zoning is the owner's DESIGN (CANON S19, 2026-08-29):
            // the map's Y-centre (uy=0.5) is the hot equator, top and bottom
            // are the cold poles — the one sanctioned anisotropy of the
            // torus. The PROFILE, however, must be smooth through the y=0
            // wrap like every other field (S1). The old triangle
            // 1−|uy−0.5|·2 had the same endpoints but a derivative kink at
            // the seam — a visible climate crease on a world with no edges.
            // The cosine below is the smallest periodic function with the
            // same anchors: 0 at uy∈{0,1} (poles), 1 at uy=0.5 (equator),
            // and d/duy = π·sin(2π·uy) = 0 at both, so the profile closes
            // C¹-smooth across the wrap.
            const float latitude =
                0.5f - 0.5f * std::cos(2.0f * 3.14159265358979f * uy);
            const float noiseTemp = terrain_fbm(posX, posY, 3, 0.5f, 4.0f, seed + 300.0f) * 0.5f + 0.5f;
            float temp01 = latitude * (1.0f - p.temperatureVariation) + noiseTemp * p.temperatureVariation;
            temp01 = std::clamp(temp01, 0.0f, 1.0f);

            const std::size_t s = (std::size_t(y) * w + x) * 4;
            td.rgba[s + 0] = to_unorm16(noiseHeight);
            td.rgba[s + 1] = to_unorm16(noiseMoist);
            td.rgba[s + 2] = to_unorm16(temp01);
            td.rgba[s + 3] = td.rgba[s + 0] < sea16
                               ? std::uint16_t(0)
                               : std::uint16_t(kFieldWordMax);
        }
    }
}

} // namespace

namespace {

constexpr std::uint16_t kRiverDistInf = 65535u;
constexpr int kRiverExploreCap = 60000;
constexpr int kRiverDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// (was a private copy of the torus wrap — core/torus.h owns it. Здесь же жил
// ОМОНИМ `cell_index(x,y,w) = y*w+x` — второй ответ на «где эта клетка», без
// свёртки; перепись 2026-09-23 назвала его поимённо, адрес теперь один:
// `cell_of`, шаг к соседу — `cell_step`.)

// River meander noise — a low-frequency, seed-stable value-noise added to the
// river trace cost. It bends least-cost paths into natural curves instead of
// long axis-aligned Manhattan runs, while the small amplitude keeps rivers
// hugging the Voronoi biome edge they follow.
constexpr int kRiverMeanderAmp = 7;      // max extra trace cost from meander
// ШАГ РЕШЁТКИ — СТЕПЕНЬ ДВОЙКИ, И ЭТО НЕ КОСМЕТИКА (владелец, 2026-09-23).
// Крупный шаг был 22 — число с потолка, единственное в макромире, из-за
// которого период решётки (`w / шаг` = 46 на мире 1024) переставал быть
// степенью двойки. Три следствия разом:
//   · заворот решётки становится МАСКОЙ, и `wrapi` уходит из макромира
//     ПОЛНОСТЬЮ, без исключения-сноски (ЗАКОН АДРЕСА, п.4);
//   · остаток исчезает ПО ПОСТРОЕНИЮ. При шаге 22 карта была 46.5 решёточных
//     клеток в ширину, целочисленное деление роняло половину, и шов чинили
//     пересчётом шага (`cellX = w / periodX`). Теперь `w / (w / шаг) == шаг`
//     тождественно, и чинить нечего — компенсация ниже стала проверкой;
//   · между крупной и мелкой решёткой встаёт ЧИСТАЯ ОКТАВА ×4 (32 против 8).
//     Прежняя пара 22/8 давала 2.75 — два масштаба дрожи слипались в один.
// ЦЕНА НАЗВАНА ЗАРАНЕЕ: форма рек меняется на ВСЕХ сидах (поле дрожи другое).
// Русло этим не переписывается — его задаёт граница биомов через `ed*ed` до
// 225 в цене шага, а меандр весь укладывается в 7.
constexpr int kRiverMeanderCoarse = 32;  // broad meander lattice spacing (cells)
constexpr int kRiverMeanderFine = 8;     // fine wiggle lattice spacing (cells)
static_assert(kRiverMeanderCoarse > 0
                  && (kRiverMeanderCoarse & (kRiverMeanderCoarse - 1)) == 0
                  && kRiverMeanderFine > 0
                  && (kRiverMeanderFine & (kRiverMeanderFine - 1)) == 0,
              "шаг решётки меандра — степень двойки: иначе период решётки "
              "перестаёт быть степенью двойки и заворот маской врёт молча");

// Gentle downhill bias. An uphill step adds (rise >> kRiverClimbShift) to the
// trace cost; downhill/flat steps pay nothing extra. This curves rivers off
// ridges and down slopes toward the sea instead of letting them march straight
// across highland plateaus. Deliberately small next to the ed*ed biome-edge
// term so the Voronoi-edge routing rivers follow still dominates the path.
constexpr int kRiverClimbShift = 1;

// СТУПЕНЬ, В КОТОРОЙ НАПИСАН ЗАКОН СТОИМОСТИ ТРАССЕРА. Карта хранит высоту
// словом unorm16 (B3, M-192), а веса выше — `kRiverMeanderAmp` 7, `ed*ed` до
// 225 и сдвиг высоты `>> 5` — соразмерны байтовой ступени, в которой их
// подбирали. Сдвиг приводит слово к ней. Это НЕ потеря: трассер выбирает
// КЛЕТКУ, а не метр, и 0.63-метровое различение высот ему нечего решать —
// а вот перекос весов в 257 раз он решал, и решал прямыми линиями.
constexpr int kFieldWordToByteShift = 8;

inline std::uint32_t river_hash(int x, int y, std::uint32_t seed) {
    std::uint32_t hsh = seed * 374761393u
        + static_cast<std::uint32_t>(x) * 668265263u
        + static_cast<std::uint32_t>(y) * 2246822519u;
    hsh = (hsh ^ (hsh >> 13)) * 1274126177u;
    return hsh ^ (hsh >> 16);
}

inline float river_lattice(int gx, int gy, int periodX, int periodY, std::uint32_t seed) {
    // Период решётки есть `сторона / шаг`, и обе величины — степени двойки
    // (`kRiverMeanderCoarse/Fine` выше, сторона мира по ЗАКОНУ АДРЕСА),
    // значит период тоже степень двойки и заворот есть маска.
    const int wx = wrap_axis(gx, periodX);
    const int wy = wrap_axis(gy, periodY);
    return float(river_hash(wx, wy, seed) & 0xffffu) * (1.0f / 65535.0f);
}

// Toroidal bilinear value-noise at cell (x,y); `cell` is the lattice spacing.
//
// The lattice closes on the WORLD, not on the requested spacing: `periodX`
// whole lattice cells are laid across the map and the sampling step follows
// from that, so the ring always meets itself. It used to take the spacing as
// given and let integer division drop the remainder — at spacing 22 the map is
// 46.5 lattice cells wide, `w / cell` said 46, and the field repeated every
// 1012 cells instead of 1024. That left a twelve-cell stutter band and then a
// hard cut on the seam line: measured 11.5× the interior gradient, the single
// largest jump anywhere on the map, and up to 3 phantom units of cost in the
// A* the rivers are traced by — a river bent by a wall that is not there.
inline float river_meander_noise(int x, int y, int w, int h, int cell, std::uint32_t seed) {
    const int periodX = std::max(1, w / std::max(1, cell));
    const int periodY = std::max(1, h / std::max(1, cell));
    const float cellX = float(w) / float(periodX);
    const float cellY = float(h) / float(periodY);
    const float fx = float(x) / cellX;
    const float fy = float(y) / cellY;
    const int x0 = int(std::floor(fx));
    const int y0 = int(std::floor(fy));
    const float tx = fx - float(x0);
    const float ty = fy - float(y0);
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);
    const float v00 = river_lattice(x0,     y0,     periodX, periodY, seed);
    const float v10 = river_lattice(x0 + 1, y0,     periodX, periodY, seed);
    const float v01 = river_lattice(x0,     y0 + 1, periodX, periodY, seed);
    const float v11 = river_lattice(x0 + 1, y0 + 1, periodX, periodY, seed);
    const float a = v00 + (v10 - v00) * sx;
    const float b = v01 + (v11 - v01) * sx;
    return a + (b - a) * sy;
}

struct RiverCandidate {
    int idx = 0;
    std::uint16_t wd = 0;
};

// A* over the river-cost field uses a binary min-heap keyed on
// f = g + waterDist. waterDist (BFS step-distance to the nearest sea cell) is a
// consistent heuristic because every step costs >= 1, so the first pop of a
// node is its optimal cost and lazy deletion is valid. The previous queue was a
// fixed 4096-bucket Dial queue; any river whose accumulated cost exceeded that
// ceiling saturated into the last bucket and was popped LIFO, collapsing the
// search into a depth-first walk -- the source of the long, unnatural,
// ridge-crossing rivers. A heap has no ceiling. Ties break on cell index so the
// river layout is bit-identical across STL implementations (MSVC vs libc++).
struct RiverHeapEntry {
    int f = 0;
    int g = 0;
    int idx = 0;
};

struct RiverHeapWorse {
    bool operator()(const RiverHeapEntry& a, const RiverHeapEntry& b) const {
        if (a.f != b.f) {
            return a.f > b.f;   // higher f = lower priority (sinks in the heap)
        }
        return a.idx > b.idx;   // deterministic, cross-platform tie-break
    }
};

struct RiverTraceScratch {
    std::vector<int> gScore;
    std::vector<int> parent;
    std::vector<std::uint32_t> tag;
    std::vector<RiverHeapEntry> heap;   // reused binary-heap storage
    std::uint32_t generation = 1u;

    void init(std::size_t n) {
        gScore.assign(n, std::numeric_limits<int>::max());
        parent.assign(n, -1);
        tag.assign(n, 0u);
    }

    void begin() {
        ++generation;
        if (generation == 0u) {
            std::fill(tag.begin(), tag.end(), 0u);
            generation = 1u;
        }
        heap.clear();
    }

    int score(int idx) const {
        return tag[std::size_t(idx)] == generation
            ? gScore[std::size_t(idx)]
            : std::numeric_limits<int>::max();
    }

    void set(int idx, int g, int p) {
        const std::size_t k = std::size_t(idx);
        tag[k] = generation;
        gScore[k] = g;
        parent[k] = p;
    }

    void push(int idx, int g, int f) {
        heap.push_back(RiverHeapEntry{f, g, idx});
        std::push_heap(heap.begin(), heap.end(), RiverHeapWorse{});
    }

    bool pop(RiverHeapEntry& out) {
        if (heap.empty()) {
            return false;
        }
        std::pop_heap(heap.begin(), heap.end(), RiverHeapWorse{});
        out = heap.back();
        heap.pop_back();
        return true;
    }
};

std::vector<std::pair<int, int>> build_river_path(int source, int goal,
                                                   const RiverTraceScratch& scratch,
                                                   int w) {
    std::vector<std::pair<int, int>> path;
    int cur = goal;
    while (cur != source) {
        path.push_back({cell_x(std::uint32_t(cur), w),
                        cell_y(std::uint32_t(cur), w)});
        const int previous = scratch.parent[std::size_t(cur)];
        if (previous < 0) {
            break;
        }
        cur = previous;
    }
    path.push_back({source % w, source / w});
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<std::pair<int, int>> trace_river_to_water(
    int source,
    const std::vector<std::uint16_t>& edgeDist,
    const std::vector<std::uint16_t>& waterDist,
    const std::vector<std::uint16_t>& height,
    const std::vector<std::uint8_t>& riverMask,
    std::uint16_t seaLevel16,
    // `int h` снят 2026-10-09: мир КВАДРАТЕН (ЗАКОН АДРЕСА — сторона одна,
    // степень двойки), второй размерности у него нет, и тело её не читало.
    int w,
    const std::vector<std::uint8_t>& meander,
    RiverTraceScratch& scratch) {

    scratch.begin();
    scratch.set(source, 0, -1);
    scratch.push(source, 0, int(waterDist[std::size_t(source)]));

    int explored = 0;
    RiverHeapEntry top;
    while (explored < kRiverExploreCap && scratch.pop(top)) {
        const int cur = top.idx;
        // Lazy deletion: a stale heap entry (a cheaper path to `cur` was found
        // after this one was pushed) no longer matches the best score -- skip it.
        if (top.g != scratch.score(cur)) {
            continue;
        }
        ++explored;

        const bool done = height[std::size_t(cur)] < seaLevel16
            || (cur != source && riverMask[std::size_t(cur)] > 0);
        if (done) {
            return build_river_path(source, cur, scratch, w);
        }

        // ── СТОИМОСТЬ ТРАССЕРА ОТКАЛИБРОВАНА В БАЙТОВЫХ СТУПЕНЯХ ПОЛЯ ──
        // Четыре члена ниже — `ed*ed` (0..225), `meander` (0..7), высота и
        // подъём — соразмерны ТОЛЬКО пока высота приходит байтом. Карта стала
        // словом (B3), и сырое `nH >> 5` дало бы 0..2047 вместо 0..7, то есть
        // затоптало бы и край биома, и меандр: трассер перестал бы огибать и
        // побежал строго вниз. ЗАМЕРЕНО: прямой осевой пробег реки на 12 сидах
        // медиана 49 → 107, max 70 → 159. Поэтому слово приводится к той
        // ступени, в которой закон стоимости написан, и приводится ЯВНО —
        // спрятать это в новую величину сдвига значило бы спрятать и калибровку.
        const int curH = int(height[std::size_t(cur)]) >> kFieldWordToByteShift;
        for (const auto& d : kRiverDirs) {
            const int ni =
                int(cell_step(std::uint32_t(cur), d[0], d[1], w));
            const int ed = std::min<int>(edgeDist[std::size_t(ni)], 15);
            const int nH = int(height[std::size_t(ni)]) >> kFieldWordToByteShift;
            const int climb = nH > curH ? ((nH - curH) >> kRiverClimbShift) : 0;
            const int cost = 1 + ed * ed + (nH >> 5)
                + int(meander[std::size_t(ni)]) + climb;
            const int ng = top.g + cost;
            if (ng >= scratch.score(ni)) {
                continue;
            }

            scratch.set(ni, ng, cur);
            scratch.push(ni, ng, ng + int(waterDist[std::size_t(ni)]));
        }
    }

    return {};
}

void stamp_river_path(const std::vector<std::pair<int, int>>& path,
                      const std::vector<std::uint16_t>& height,
                      std::uint16_t seaLevel16,
                      std::vector<std::uint8_t>& riverMask,
                      int w,
                      int h,
                      const std::vector<std::uint16_t>& waterDist) {
    (void)h;   // квадрат мира: одна сторона на обе оси (ЗАКОН АДРЕСА)
    for (const auto& p : path) {
        const std::uint32_t at = cell_of(p.first, p.second, w);
        const std::uint16_t wd = waterDist[at];
        const int radius = wd < 4u ? 1 : 0;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy > radius * radius) {
                    continue;
                }
                const int ni = int(cell_step(at, dx, dy, w));
                if (height[std::size_t(ni)] >= seaLevel16) {
                    riverMask[std::size_t(ni)] = 255;
                }
            }
        }
    }
}

int count_river_neighbours(const std::vector<std::uint8_t>& riverMask,
                           int idx,
                           int w,
                           int h) {
    (void)h;   // квадрат мира: одна сторона на обе оси (ЗАКОН АДРЕСА)
    int count = 0;
    for (const auto& d : kRiverDirs) {
        const std::uint32_t ni = cell_step(std::uint32_t(idx), d[0], d[1], w);
        if (riverMask[ni] > 0) {
            ++count;
        }
    }
    return count;
}

std::vector<int> find_river_tips(const std::vector<std::uint8_t>& riverMask,
                                 const std::vector<std::uint16_t>& height,
                                 std::uint16_t seaLevel16,
                                 int w,
                                 int h) {
    std::vector<int> tips;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = int(cell_of(x, y, w));
            if (riverMask[std::size_t(idx)] == 0 || height[std::size_t(idx)] < seaLevel16) {
                continue;
            }

            int riverNbrs = 0;
            bool hasSea = false;
            for (const auto& d : kRiverDirs) {
                const std::uint32_t ni =
                    cell_step(std::uint32_t(idx), d[0], d[1], w);
                if (riverMask[ni] > 0) {
                    ++riverNbrs;
                }
                if (height[ni] < seaLevel16) {
                    hasSea = true;
                }
            }

            if (riverNbrs == 1 && !hasSea) {
                tips.push_back(idx);
            }
        }
    }
    return tips;
}

bool continue_river_from_tip(int tipIdx,
                             std::vector<std::uint8_t>& riverMask,
                             const std::vector<std::uint16_t>& edgeDist,
                             const std::vector<std::uint16_t>& waterDist,
                             const std::vector<std::uint16_t>& height,
                             std::uint16_t seaLevel16,
                             int w,
                             int h,
                             const std::vector<std::uint8_t>& meander,
                             RiverTraceScratch& scratch) {
    std::vector<int> masked;
    int cur = tipIdx;
    for (int steps = 0; steps < 200; ++steps) {
        if (cur != tipIdx) {
            masked.push_back(cur);
            riverMask[std::size_t(cur)] = 0;
        }

        int next = -1;
        int nextRiverNbrs = 0;
        for (const auto& d : kRiverDirs) {
            const int ni =
                int(cell_step(std::uint32_t(cur), d[0], d[1], w));
            if (riverMask[std::size_t(ni)] > 0) {
                next = ni;
                nextRiverNbrs = count_river_neighbours(riverMask, ni, w, h);
                break;
            }
        }

        if (next < 0 || nextRiverNbrs >= 3) {
            break;
        }
        cur = next;
    }

    const std::vector<std::pair<int, int>> path =
        trace_river_to_water(tipIdx, edgeDist, waterDist, height,
                             riverMask, seaLevel16, w, meander, scratch);

    for (int idx : masked) {
        riverMask[std::size_t(idx)] = 255;
    }

    if (path.size() >= 3) {
        stamp_river_path(path, height, seaLevel16, riverMask, w, h, waterDist);
        return true;
    }
    return false;
}

void continue_dead_end_rivers(std::vector<std::uint8_t>& riverMask,
                              const std::vector<std::uint16_t>& edgeDist,
                              const std::vector<std::uint16_t>& waterDist,
                              const std::vector<std::uint16_t>& height,
                              std::uint16_t seaLevel16,
                              int w,
                              int h,
                              const std::vector<std::uint8_t>& meander,
                              RiverTraceScratch& scratch) {
    for (int pass = 0; pass < 5; ++pass) {
        const std::vector<int> tips = find_river_tips(riverMask, height, seaLevel16, w, h);
        int resolved = 0;
        for (int tipIdx : tips) {
            if (continue_river_from_tip(tipIdx, riverMask, edgeDist, waterDist,
                                        height, seaLevel16, w, h, meander, scratch)) {
                ++resolved;
            }
        }
        if (resolved == 0) {
            break;
        }
    }
}

} // namespace  (anonymous river/terrain helpers end here)

// Second CPU synth pass. Reads td.rgba (height/moisture/temperature), traces
// least-cost rivers hugging climate-biome edges toward the nearest sea, stamps
// carves river cells below sea level so they
// classify as Biome::Water. Moved OUT of the anonymous namespace so the river
// generation test suite can drive it on a controlled synthetic TerrainData; the
// helpers above keep internal linkage and stay visible for the rest of this TU.
void generate_river_data(TerrainData& td, const LayerParameters& params) {
    const int w = td.width;
    const int h = td.height;
    const int n = w * h;
    // РОЖДЕНИЕ ПЛОСКОСТИ МОРЯ: карта уносит порог, по которому её врезали, с
    // собой — дальше её никто не переспрашивает (§5 п.3: отказ и запись — в
    // точке рождения, не на чтении). Это же вход для свидетелей, которые
    // гоняют врез рек на синтетической карте напрямую.
    td.seaLevel16 = field_word_of(params.seaLevel);
    // ОДИН ЗАКОН ПОРОГА НА ВЕСЬ ТРАССЕР (M-109): вода — строго НИЖЕ плоскости
    // (`R < seaLevel16`), земля — `R >= seaLevel16`. Восемь сравнений ниже
    // стояли через `<=` и `>`, то есть клетку РОВНО на плоскости трассер
    // считал водой, а вся остальная игра — сушей: река могла «дойти до моря»
    // на клетке, по которой ходят пешком, и не дотечь до настоящей воды.
    const std::uint16_t seaLevel16 = td.seaLevel16;

    // МАСКА РУСЛА — ЛОКАЛЬНЫЙ БУФЕР ГЕНЕРАЦИИ, А НЕ СЛОЙ МИРА (M-211,
    // вердикт владельца 2026-10-03: «никакая ривер дата не нужна… рек нет
    // как структуры, от них остаются только прокопы в рельефе, и агностично
    // эмерджентно там вода, потому что эти прокопы ниже уровня моря»).
    // Трассеру она нужна, пока он трассирует — продолжить тупик, не
    // перетрассировать уже пройденное, — и умирает вместе с проходом.
    // Генератору буферы разрешены прямо (ЗАКОН ГЕНЕРАЦИИ п.3).
    std::vector<std::uint8_t> riverMask(std::size_t(n), 0u);
    if (n <= 0 || td.rgba.size() < std::size_t(n) * 4) {
        return;
    }

    // Рабочая копия высот: трассер СРЕЗАЕТ русло по ходу (stamp_river_path), и
    // резать он обязан свою копию, а не мастер — врез в `td.rgba` идёт один раз
    // и в конце. Влага и температура своих копий больше не имеют: каскад биома
    // читает их сам, у карты.
    std::vector<std::uint16_t> heightWords(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        heightWords[std::size_t(i)] = td.rgba[std::size_t(i) * 4];
    }

    std::vector<std::uint8_t> biome(std::size_t(n), 255);
    for (int i = 0; i < n; ++i) {
        // ОДИН КАСКАД «КАКОЙ БИОМ» (M-110, CANON S6): здесь стоял СВОЙ каскад —
        // климатическая матрица без Mountain, — и он делал трассер единственным
        // читателем мира, для которого горы не существует. Рим массива не был
        // ему краем биома, поэтому река, спускаясь с гор, прижималась не к тому
        // рубежу; а порог горы жил при этом ещё и вторым числом в лесу (0.80
        // против 0.75). Карта на сиде от этой правки ДВИГАЕТСЯ — это и есть
        // цена второго ответа, которую платили молча.
        // КАСКАДОМ, А НЕ ПОЛЕМ: поля ещё нет и быть не может — оно печётся
        // после вреза, а трассеру нужен биом ДО него (врез он же и рассчитывает).
        // Это единственный законный звонящий каскада во всём дереве, кроме
        // самой выпечки.
        const Biome b = biome_classify(td, std::uint32_t(i));
        if (b == Biome::Water) {
            // 255 — «не биом»: вода трассеру ЦЕЛЬ, а не берег, и в поле краёв
            // суши она не участвует ни семенем, ни фронтом. Это не второй ответ
            // про воду — это отказ от участия, и спросил его тот же каскад.
            continue;
        }
        biome[std::size_t(i)] = std::uint8_t(b);
    }

    std::vector<std::uint16_t> edgeDist(std::size_t(n), kRiverDistInf);
    std::vector<int> edgeQueue;
    edgeQueue.reserve(std::size_t(n) / 8);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = int(cell_of(x, y, w));
            const std::uint8_t b = biome[std::size_t(idx)];
            if (b == 255) {
                continue;
            }
            for (const auto& d : kRiverDirs) {
                const std::uint32_t ni =
                    cell_step(std::uint32_t(idx), d[0], d[1], w);
                if (biome[ni] != b) {
                    edgeDist[std::size_t(idx)] = 0;
                    edgeQueue.push_back(idx);
                    break;
                }
            }
        }
    }
    if (edgeQueue.empty()) {
        return;
    }

    std::size_t head = 0;
    while (head < edgeQueue.size()) {
        const int idx = edgeQueue[head++];
        const std::uint16_t d = edgeDist[std::size_t(idx)];
        if (d >= 15u) {
            continue;
        }
        for (const auto& dir : kRiverDirs) {
            const int ni =
                int(cell_step(std::uint32_t(idx), dir[0], dir[1], w));
            if (edgeDist[std::size_t(ni)] > std::uint16_t(d + 1u)
                && biome[std::size_t(ni)] != 255) {
                edgeDist[std::size_t(ni)] = std::uint16_t(d + 1u);
                edgeQueue.push_back(ni);
            }
        }
    }

    std::vector<std::uint16_t> waterDist(std::size_t(n), kRiverDistInf);
    std::vector<int> waterQueue;
    waterQueue.reserve(std::size_t(n) / 4);
    for (int i = 0; i < n; ++i) {
        if (heightWords[std::size_t(i)] < seaLevel16) {
            waterDist[std::size_t(i)] = 0;
            waterQueue.push_back(i);
        }
    }
    if (waterQueue.empty()) {
        return;
    }

    head = 0;
    while (head < waterQueue.size()) {
        const int idx = waterQueue[head++];
        const std::uint16_t d = waterDist[std::size_t(idx)];
        for (const auto& dir : kRiverDirs) {
            const int ni =
                int(cell_step(std::uint32_t(idx), dir[0], dir[1], w));
            if (waterDist[std::size_t(ni)] > std::uint16_t(d + 1u)) {
                waterDist[std::size_t(ni)] = std::uint16_t(d + 1u);
                waterQueue.push_back(ni);
            }
        }
    }

    std::vector<RiverCandidate> candidates;
    candidates.reserve(std::size_t(n) / 32);
    for (int i = 0; i < n; ++i) {
        if (heightWords[std::size_t(i)] >= seaLevel16
            && edgeDist[std::size_t(i)] <= 2u
            && waterDist[std::size_t(i)] > 4u) {
            candidates.push_back({i, waterDist[std::size_t(i)]});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const RiverCandidate& a, const RiverCandidate& b) {
                  // Longest-first (farthest from sea), with an index tie-break so
                  // the ordering is a total order -- identical across STL impls.
                  if (a.wd != b.wd) {
                      return a.wd > b.wd;
                  }
                  return a.idx < b.idx;
              });

    std::vector<std::uint8_t> taken(std::size_t(n), 0);
    auto markTaken = [&](int idx, int radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy > radius * radius) {
                    continue;
                }
                taken[cell_step(std::uint32_t(idx), dx, dy, w)] = 1;
            }
        }
    };

    std::vector<int> sources;
    sources.reserve(candidates.size());
    constexpr int kMinSpacing = 12;
    for (const RiverCandidate& c : candidates) {
        if (taken[std::size_t(c.idx)]) {
            continue;
        }
        sources.push_back(c.idx);
        markTaken(c.idx, kMinSpacing);
    }

    std::vector<std::uint8_t> meander(std::size_t(n), 0);
    {
        const std::uint32_t mseed = params.seed * 2654435761u + 1u;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float lo = river_meander_noise(x, y, w, h, kRiverMeanderCoarse, mseed);
                const float hi = river_meander_noise(x, y, w, h, kRiverMeanderFine,
                                                     mseed ^ 0x9e3779b9u);
                const float v = 0.65f * lo + 0.35f * hi;
                meander[cell_of(x, y, w)] = std::uint8_t(
                    std::clamp(int(v * float(kRiverMeanderAmp) + 0.5f), 0, kRiverMeanderAmp));
            }
        }
    }

    RiverTraceScratch scratch;
    scratch.init(std::size_t(n));
    for (int src : sources) {
        const std::vector<std::pair<int, int>> raw =
            trace_river_to_water(src, edgeDist, waterDist, heightWords,
                                 riverMask, seaLevel16, w, meander, scratch);
        if (raw.size() < 15) {
            continue;
        }
        stamp_river_path(raw, heightWords, seaLevel16, riverMask, w, h, waterDist);
    }

    continue_dead_end_rivers(riverMask, edgeDist, waterDist, heightWords,
                             seaLevel16, w, h, meander, scratch);

    // ГЛУБИНА ВРЕЗА — ДОЛЯ ПОЛЯ, А НЕ ЧИСЛО СЛОВ СЛОВАРЯ. Здесь стояло
    // `seaLevel16 - 8`, то есть восемь шагов БАЙТА = 3.1 % поля. Перенести
    // «восемь» в uint16 дословно значило бы 0.012 % поля — русло осталось бы
    // стоять на самой плоскости, и реки исчезли бы молча на всей карте.
    // Доля сохранена дословно; её вывод В МЕТРАХ владельцем не продиктован и
    // стоит нарядом M-197 (у русла сегодня нет профиля вовсе — оно держит
    // плоскость моря и на хребте, замер: 72 % пар суша-вода речные, p50 715 м).
    // Сама глубина и её перевод в слово карты живут в заголовке
    // (`kRiverBedBelowSea01` / `river_bed_word`): после сноса маски русла дно
    // прокопа стало ЕДИНСТВЕННЫМ признаком реки, то есть свойством мира, а не
    // деталью этого прохода.
    const std::uint16_t carveH = river_bed_word(seaLevel16);
    for (int i = 0; i < n; ++i) {
        const std::size_t s = std::size_t(i) * 4;
        if (riverMask[std::size_t(i)] > 0 && td.rgba[s + 0] >= seaLevel16) {
            td.rgba[s + 0] = std::min(td.rgba[s + 0], carveH);
        }
    }

    for (int i = 0; i < n; ++i) {
        const std::size_t s = std::size_t(i) * 4;
        td.rgba[s + 3] = td.rgba[s + 0] < seaLevel16
                           ? std::uint16_t(0)
                           : std::uint16_t(kFieldWordMax);
    }

    // ЗДЕСЬ КОНЧАЕТСЯ РОЖДЕНИЕ И НАЧИНАЕТСЯ МИР. Врез только что опустил русла
    // ниже плоскости, то есть последнее изменение высот уже произошло, — и
    // ровно теперь каскад сворачивается в поле. Выпечка стоит в конце
    // `generate_river_data`, а не `generate_terrain`, потому что синтетические
    // карты свидетелей гоняют врез напрямую: дверь, которой пользуется мир,
    // обязана быть той же, которой пользуется харнесс.
    bake_biomes(td);
}

TerrainData generate_terrain(int w, int h, const LayerParameters& params) {
    // ФОРМА МИРА ПРОВЕРЯЕТСЯ ЗДЕСЬ, ГРОМКО И ОДИН РАЗ (ЗАКОН АДРЕСА).
    // Этот отказ родился из боли, оплаченной в тот же час: у дверей слоёв
    // проверка формы стоит НА ЧТЕНИИ и отвечает fail-closed, а fail-closed у
    // терраина значит «нулевая высота», то есть «вода». Мир целиком из воды
    // не отказывает размещателю поселений — он заставляет его искать сушу
    // ВЕЧНО. Свидетель расселения на мире 96×96 (квадрат, но не степень
    // двойки) провисел два часа, съев ядро, и не упал ни разу.
    // Мораль: проверка на КАЖДОМ чтении умеет только соврать потише.
    // Отказать умеет только проверка в точке РОЖДЕНИЯ, и она обязана
    // говорить вслух.
    if (!world_shape_ok(w, h)) {
        std::fprintf(stderr,
                     "[worldgen] ОТКАЗ: мир %d×%d — сторона обязана быть "
                     "степенью двойки, а мир квадратом (ЗАКОН АДРЕСА). "
                     "Терраин не построен.\n", w, h);
        return TerrainData{};
    }
    TerrainData td;
    td.width = w; td.height = h;
    td.seaLevel16 = field_word_of(params.seaLevel);
    td.seed = params.seed;
    td.rgba.assign(std::size_t(w) * h * 4, 0);

    // КАРКАС ПЛИТ РОЖДАЕТСЯ ПЕРВЫМ — синтез высоты его ЧИТАТЕЛЬ. Поле
    // ординала заводится здесь, а заполняет его тот же поклеточный проход:
    // один писатель, один ответ на «чья это клетка».
    td.plates = make_plates(w, params.seed);
    if (td.plates.valid())
        td.plates.cell.assign(std::size_t(w) * std::size_t(h), kNoPlate);

    // Climate master (height/moisture/temperature/mask) -- CPU synth, no GL.
    synth_master(td, params, td.plates);

    // Rivers trace over the height/mask channels (already CPU).
    generate_river_data(td, params);

    return td;
}

void destroy_terrain(TerrainData& t) {
    t.rgba.clear();
    t.biome.clear();
    t.plates = PlateMap{};
    t.width = t.height = 0;
}

} // namespace sm
