// СВИДЕТЕЛЬ ИНВАРИАНТА ШТАБЕЛЯ (владелец, 2026-09-22).
//
// Сторожа РАЗМЕРОВ живут в самой переписи (core/stacks.h) и краснеют на
// сборке. Этот свидетель сторожит то, чего компилятор увидеть не может: что
// перепись описывает МИР, а не себя — что потолки настоящие, что род у каждого
// штабеля назван, и что картина памяти сходится числом.
//
// И он ПЕЧАТАЕТ картину памяти. По AGENTS п.10 она рисуется ДО кода; печать
// здесь — чтобы её не приходилось выводить заново на каждой сессии.
#include "core/stacks.h"
#include "check.h"

#include <cstdio>

int main() {
    using namespace sm;

    std::printf("\n=== ШТАБЕЛЯ МИРА ===\n");
    std::printf("%-30s %-24s %8s %12s %10s\n",
                "штабель", "строка", "Б/стр", "строк", "МиБ");
    double totalMiB = 0.0;
    for (std::size_t i = 0; i < kStackCount; ++i) {
        const StackRow& r = kStacks[i];
        const double miB = double(r.rowBytes) * double(r.cap)
                           / (1024.0 * 1024.0);
        totalMiB += miB;
        std::printf("%-30s %-24s %8zu %12zu %10.1f\n",
                    r.name, r.rowType, r.rowBytes, r.cap, miB);
    }
    std::printf("%-30s %-24s %8s %12s %10.1f\n\n",
                "ИТОГО ПО КАПАМ", "", "", "", totalMiB);

    CHECK(kStackCount > 0, "перепись не бывает пустой");

    // ПОТОЛОК ЕСТЬ У КАЖДОГО ШТАБЕЛЯ. Штабель без потолка — это вектор,
    // который растёт, куда захочет, то есть ровно то, что инвариант запрещает.
    bool everyCapped = true, everyRowSized = true, everyNamed = true;
    for (std::size_t i = 0; i < kStackCount; ++i) {
        if (kStacks[i].cap == 0) everyCapped = false;
        if (kStacks[i].rowBytes == 0) everyRowSized = false;
        if (!kStacks[i].name || !kStacks[i].rowType
            || kStacks[i].name[0] == '\0') {
            everyNamed = false;
        }
    }
    CHECK(everyCapped, "у КАЖДОГО штабеля есть потолок строк");
    CHECK(everyRowSized, "у КАЖДОГО штабеля строка имеет размер");
    CHECK(everyNamed, "КАЖДЫЙ штабель назван и знает тип своей строки");

    // ПОТОЛКИ — НАСТОЯЩИЕ, А НЕ ВЫДУМАННЫЕ ПЕРЕПИСЬЮ. Мир есть тор
    // 1024×1024 (SKELETON слой 0), сквадов в нём 16384.
    CHECK(kWorldCells == 1024u * 1024u,
          "потолок клеток = связный тор 1024x1024, слой 0 скелета");
    CHECK(kWorldSquads == 16384u, "потолок сквадов мира");

    // ПОЛЕ НАД МИРОМ ПОКРЫВАЕТ ВЕСЬ МИР — иначе это не поле, а список с
    // дырами, и «число в каждой клетке» перестаёт быть правдой.
    bool cellStacksCoverWorld = true;
    for (std::size_t i = 0; i < kStackCount; ++i) {
        if (kStacks[i].kind != StackKind::ByCell) continue;
        if (kStacks[i].cap % kWorldCells != 0) cellStacksCoverWorld = false;
    }
    CHECK(cellStacksCoverWorld,
          "штабель ПО КЛЕТКЕ кратен миру: число есть в КАЖДОЙ клетке");

    // СКАЛЯРЫ — ИСКЛЮЧЕНИЕ, И ОНО РОВНО ОДНО. Два блока скаляров означали бы,
    // что «состояние мира» снова расползлось по коду.
    int scalarBlocks = 0;
    for (std::size_t i = 0; i < kStackCount; ++i) {
        if (kStacks[i].kind == StackKind::Scalars) ++scalarBlocks;
    }
    CHECK(scalarBlocks == 1,
          "блок скаляров мира РОВНО ОДИН — названное исключение инварианта");

    return sm::test::report("stack_census_test");
}
