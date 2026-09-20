// ПАМЯТЬ МИРА — свидетель закона (macro/memory.h, CANON S19.2).
//
// Заведён 2026-09-21 на дефект, который прожил в дани с самого её рождения и
// НЕ ПАДАЛ ни на одном тесте, потому что дефект был МОЛЧАНИЕМ: память с
// непредмасштабированным хранением имеет мёртвую зону размером в горизонт —
// шаг `(уровень − память) >> 5` равен нулю на всякой разнице меньше 32.
// Следствие в мире: склад, ни разу не превысивший 31, держал среднее РОВНО
// НОЛЬ вечно, и вся лестница комфорта не облагалась данью НИКОГДА (в мире 21
// инструмент на 1860 мест), а «1/8 со всего» было неправдой тем сильнее, чем
// место мельче.
//
// Поэтому здесь пинится не число, а СВОЙСТВА: память сходится к уровню,
// мелкий уровень не проваливается в ноль, забывание работает, и у каждого
// утверждения есть негативный контроль — в том числе СТАРАЯ форма, которая
// обязана на этих же входах провалиться.
#include "check.h"

#include <initializer_list>

#include "macro/memory.h"

using namespace sm;

namespace {

// Старая, непредмасштабированная форма — ровно то, что стояло в world_tick
// до 2026-09-21. Живёт ТОЛЬКО здесь и только как негативный контроль: если
// детектор не видит дефекта на ней, он не увидит его и в мире.
void legacy_step(std::int32_t& avg, std::int32_t level) {
    avg += (level - avg) >> kMemoryShift;
}

void test_horizon_is_the_season() {
    // Горизонт памяти не «примерно сезон» и не своя ручка: он РАВЕН сезону,
    // и это закон, а не калибровка (CANON S19.2 «веха»). Сдвинется сезон —
    // сдвинется память, и никто ничего не будет подкручивать отдельно.
    CHECK(kMemoryHorizonDays == kDaysPerSeason,
          "горизонт памяти мира ЕСТЬ сезон");
    CHECK((1 << kMemoryShift) == kMemoryHorizonDays,
          "сдвиг — логарифм горизонта, иначе масштаб врёт в 2^k раз");
}

void test_tracking_converges_to_the_level() {
    // Постоянный уровень: память обязана прийти К НЕМУ, а не осесть ниже.
    // Прежняя форма оседала на целый горизонт ниже — и это было записано в
    // коде как «принятое следствие».
    //
    // СКОЛЬКО ЖДАТЬ — не вкус, а расчёт (и сам свидетель поймал на нём автора
    // 2026-09-21): невязка гаснет как e^(−дни/горизонт), поэтому «в пределах
    // единицы» наступает через ln(уровень) горизонтов, а не через один. Для
    // миллиона это ~14 сезонов, для потолка счёта — ~22. Тридцать два взяты
    // с запасом над худшим случаем. ЭТО СВОЙСТВО МИРА, а не теста: склад,
    // скакнувший на новый уровень, входит в дань ПОСТЕПЕННО — за свой
    // горизонт память проходит 63 % пути, и это ровно то сглаживание, ради
    // которого она заведена.
    for (const std::int64_t level : {10ll, 31ll, 1000ll, 1000000ll}) {
        WorldMemory mem = 0;
        for (int day = 0; day < kMemoryHorizonDays * 32; ++day)
            memory_track(mem, level);
        const std::int64_t got = memory_value(mem);
        CHECK(got >= level - 1 && got <= level,
              "слежение сходится К УРОВНЮ, без смещения на горизонт вниз");
    }
}

void test_small_level_is_not_swallowed() {
    // ЭТО И ЕСТЬ ПИН ДЕФЕКТА. Уровень мельче горизонта обязан быть виден.
    const std::int64_t small = 10;
    WorldMemory mem = 0;
    for (int day = 0; day < kMemoryHorizonDays * 4; ++day)
        memory_track(mem, small);
    CHECK(memory_value(mem) > 0,
          "мелкий уровень ВИДЕН памяти — мёртвой зоны нет");

    // НЕГАТИВНЫЙ КОНТРОЛЬ: старая форма на том же входе даёт ровно ноль, и
    // даёт его ВЕЧНО. Без этой проверки утверждение выше ничего не стоит —
    // оно прошло бы и на сломанной реализации, которая случайно не ноль.
    std::int32_t legacy = 0;
    for (int day = 0; day < kMemoryHorizonDays * 4; ++day)
        legacy_step(legacy, std::int32_t(small));
    CHECK(legacy == 0,
          "негативный контроль: прежняя форма НЕ ВИДЕЛА мелкий уровень вовсе");
}

void test_memory_forgets() {
    // Забывание — вторая половина памяти: уровень ушёл в ноль, и память
    // обязана уйти за ним в пределах своего горизонта (а не застрять).
    WorldMemory mem = 0;
    for (int day = 0; day < kMemoryHorizonDays * 8; ++day)
        memory_track(mem, 4096);
    const std::int64_t peak = memory_value(mem);
    CHECK(peak > 4000, "негативный контроль: памяти было ЧТО забывать");
    for (int day = 0; day < kMemoryHorizonDays; ++day) memory_track(mem, 0);
    const std::int64_t afterSeason = memory_value(mem);
    CHECK(afterSeason < peak / 2,
          "за свой горизонт память забывает больше половины");
    for (int day = 0; day < kMemoryHorizonDays * 8; ++day) memory_track(mem, 0);
    CHECK(memory_value(mem) == 0, "и в конце концов забывает целиком");
}

void test_accumulate_settles_on_rate_times_horizon() {
    // ВТОРОЙ ГЛАГОЛ: на входе импульсы, равновесие — «темп × горизонт».
    // Путать глаголы нельзя, и цена путаницы ровно 32-кратная — это и
    // проверяется сравнением с слежением за тем же темпом.
    const std::int64_t rate = 7;
    WorldMemory acc = 0;
    for (int day = 0; day < kMemoryHorizonDays * 8; ++day)
        memory_accumulate(acc, rate);
    const std::int64_t settled = memory_value(acc);
    const std::int64_t expect = rate * kMemoryHorizonDays;
    CHECK(settled >= expect - kMemoryHorizonDays && settled <= expect,
          "накопление сходится к «темп × горизонт»");

    WorldMemory tracked = 0;
    for (int day = 0; day < kMemoryHorizonDays * 8; ++day)
        memory_track(tracked, rate);
    CHECK(memory_value(tracked) < settled,
          "негативный контроль: слежение за тем же числом даёт ДРУГУЮ "
          "величину — глаголы не взаимозаменяемы");

    // Источник иссяк — накопленное гаснет тем же горизонтом.
    for (int day = 0; day < kMemoryHorizonDays * 8; ++day)
        memory_accumulate(acc, 0);
    CHECK(memory_value(acc) == 0, "без источника накопленное гаснет в ноль");
}

void test_width_is_calculated_not_hoped() {
    // ЗАКОН ТИПА: поле держит значение × 32, а самое крупное значение мира —
    // счёт предмета на складе (int32). Проверяем, что потолок счёта проходит
    // через память без потери — именно этот расчёт и потребовал 64 бита.
    const std::int64_t huge = 2147483647ll;   // INT32_MAX — потолок счёта
    WorldMemory mem = 0;
    for (int day = 0; day < kMemoryHorizonDays * 32; ++day)
        memory_track(mem, huge);
    CHECK(memory_value(mem) >= huge - 1,
          "потолок счёта предмета проходит память без переполнения");
    CHECK(mem > huge, "негативный контроль: хранимое ДЕЙСТВИТЕЛЬНО больше "
                      "значения — масштаб на месте");
}

}  // namespace

int main() {
    test_horizon_is_the_season();
    test_tracking_converges_to_the_level();
    test_small_level_is_not_swallowed();
    test_memory_forgets();
    test_accumulate_settles_on_rate_times_horizon();
    test_width_is_calculated_not_hoped();
    return sm::test::report("world_memory_test");
}
