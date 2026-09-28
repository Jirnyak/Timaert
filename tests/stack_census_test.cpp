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

namespace {

// ШИРИНА ПОЛЯ СЧИТАЕТСЯ В ЗНАКАХ, А НЕ В БАЙТАХ. `%-30s` меряет байты, и на
// кириллице (2 байта на знак в UTF-8) картина памяти печаталась лесенкой —
// то есть прибор, поставленный ради того, чтобы картину было ВИДНО, её
// разваливал. Считаем кодовые точки: продолжение UTF-8 (10xxxxxx) не знак.
std::size_t glyphs(const char* s) {
    std::size_t n = 0;
    for (const char* p = s; *p; ++p) {
        if ((static_cast<unsigned char>(*p) & 0xC0u) != 0x80u) ++n;
    }
    return n;
}

void pad_to(const char* s, std::size_t width) {
    std::printf("%s", s);
    for (std::size_t g = glyphs(s); g < width; ++g) std::printf(" ");
}

const char* kind_name(sm::StackKind k) {
    switch (k) {
        case sm::StackKind::ByCell:    return "ПО КЛЕТКЕ";
        case sm::StackKind::ByOrdinal: return "ПО ОРДИНАЛУ";
        case sm::StackKind::Catalog:   return "КАТАЛОГ";
        case sm::StackKind::Scalars:   return "СКАЛЯРЫ";
    }
    return "?";
}

} // namespace

int main() {
    using namespace sm;

    std::printf("\n=== ШТАБЕЛЯ МИРА ===\n");
    pad_to("штабель", 48);
    pad_to("строка", 28);
    std::printf("%8s %12s %10s\n", "Б/стр", "строк", "МиБ");
    double totalMiB = 0.0;
    // ПОДЫТОГ НА РОД — ЧАСТЬ КАРТИНЫ, А НЕ УКРАШЕНИЕ: без него итог говорит
    // «шестнадцать гигабайт» и не говорит, ЧТО именно их держит.
    double kindMiB[4] = {0.0, 0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < kStackCount; ++i) {
        const StackRow& r = kStacks[i];
        const double miB = double(r.rowBytes) * double(r.cap)
                           / (1024.0 * 1024.0);
        totalMiB += miB;
        kindMiB[std::size_t(r.kind)] += miB;
        pad_to(r.name, 48);
        pad_to(r.rowType, 28);
        std::printf("%8zu %12zu %10.1f\n", r.rowBytes, r.cap, miB);
    }
    std::printf("\n");
    for (std::size_t k = 0; k < 4; ++k) {
        pad_to("  из них ", 10);
        pad_to(kind_name(StackKind(k)), 14);
        std::printf("%10.1f МиБ\n", kindMiB[k]);
    }
    pad_to("ИТОГО ПО КАПАМ", 24);
    std::printf("%10.1f МиБ\n\n", totalMiB);

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

    // ОБЪЯВЛЕННЫЙ РОД БЕЗ ЖИЛЬЦОВ — КОЛОНКА-СИРОТА (AGENTS DOD п.9). Род
    // `Catalog` был объявлен 2026-09-22 и простоял с нулём строк: перепись
    // умела назвать каталоги и молчала о них, а комментарий `StackRow::cap`
    // это молчание ОБЪЯСНЯЛ («0 = каталог») — объяснял ровно то, что сам
    // свидетель ниже запрещает. Проверка стоит здесь, чтобы род либо жил,
    // либо был снесён, но не числился пустым.
    int catalogRows = 0;
    for (std::size_t i = 0; i < kStackCount; ++i) {
        if (kStacks[i].kind == StackKind::Catalog) ++catalogRows;
    }
    CHECK(catalogRows > 0,
          "род КАТАЛОГ объявлен — значит у него есть строки");

    // У ГРАФА ОКРУГ НЕТ СВОЕГО ЧИСЛА, И ЭТО СКАЗАНО ВСЛУХ. Округа рождается
    // на каждое живое место, поэтому потолок графа ЗАИМСТВОВАН у мест.
    // Появится здесь отдельная константа — значит кто-то завёл второй ответ
    // на вопрос «сколько в мире мест» (DOD п.6), и упадёт эта строка.
    CHECK(kNavRegionCap == kWorldLandmarks,
          "потолок округ навигации заимствован у мест, а не выдуман свой");

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
