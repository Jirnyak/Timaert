// ПРИБОР НА ЗАКОН ТУПИКА РЕНДЕРА: у субмира нет обратного ребра из рендера.
//
// Владелец, дословно: «рендер это тупиковые пути этих систем что ушло в рендер
// уже не идёт никуда больше». Вертикальная истина окна жила В РЕНДЕРЕРЕ
// (`Renderer3DVk::heightVtxM_`), и симуляция читала её оттуда ПЯТНАДЦАТЬ раз —
// посадка игрока, опора гравитации, пол летуна, потолок полёта, восприятие
// земли мозгом НПЦ. Теперь поле стоит в мире (`sub/height.h`), а рендерер его
// читает.
//
// УТВЕРЖДЕНИЕ ОБ ОТСУТСТВИИ АДРЕСА В КОДЕ НЕ ИМЕЕТ — оно указывает на СВОЙ
// ТЕСТ (AGENTS §0 п.2). Пока стена не поднята КОМПИЛЯТОРОМ (ЗАКОН ПАКЕТНОЙ
// ШИНЫ п.7, отдельный эпик), перепись исходника — единственная честная форма
// «обратного ребра больше нет»: сегодня его нет по построению, завтра его
// вернёт одна строка, и упасть должно здесь, а не в кадре.
#include "check.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef TIMAERT_SOURCE_DIR
#error "TIMAERT_SOURCE_DIR must name the repo root - see CMakeLists.txt"
#endif

namespace {

constexpr const char* kRoot = TIMAERT_SOURCE_DIR;

bool read_file(const std::string& rel, std::string& out) {
    const std::string path = std::string(kRoot) + "/" + rel;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    std::size_t n = 0;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

std::size_t count_occurrences(const std::string& hay, const std::string& needle) {
    std::size_t n = 0, at = 0;
    while ((at = hay.find(needle, at)) != std::string::npos) {
        ++n;
        at += needle.size();
    }
    return n;
}

// Что симуляция субмира НЕ СМЕЕТ спрашивать у своего рендерера. Каждая строка
// — реально существовавшее обратное ребро, не выдуманный запрет.
struct Banned {
    const char* file;
    const char* needle;
    const char* what;
};

const Banned kBanned[] = {
    {"src/sub/engine.cpp", "renderer3dVk_.sample_height_m",
     "the simulation asks the renderer for the ground"},
    {"src/sub/engine.cpp", "renderer3dVk_.max_height_m",
     "the flight ceiling is taken from the renderer's window extent"},
    {"src/sub/engine.cpp", "renderer3dVk_.cam",
     "a gameplay number is read out of the rendering camera"},
    {"src/sub/vk_renderer_3d.h", "float sample_height_m",
     "the renderer offers the world's surface as its own"},
    {"src/sub/vk_renderer_3d.h", "float max_height_m",
     "the renderer offers the window's extent as its own"},
    {"src/sub/vk_renderer_3d.h", "heightVtxM_",
     "the renderer keeps a height grid of its own (two answers, one question)"},
};

// ПОЛОЖИТЕЛЬНЫЙ КОНТРОЛЬ: ноль запрещённого обязан означать «спрашивает МИР», а
// не «файл переименован, и прибор считает пустоту».
void positive_control() {
    std::string engine;
    CHECK_OR_RETURN(read_file("src/sub/engine.cpp", engine),
                    "could not read the subworld engine");
    const std::size_t door = count_occurrences(engine, "height_field()");
    std::fprintf(stderr, "[render-dead-end] engine asks the world %zu times\n",
                 door);
    CHECK(door >= 15,
          "the simulation stopped asking the WORLD for the ground too");
}

} // namespace

int main() {
    // СНАЧАЛА ДЕТЕКТОР ПОД ТЕСТОМ. Счётчик, который ничего не находит никогда,
    // зеленел бы на любом дереве — включая то, где закон нарушен (§8 п.6).
    const std::string probe = "xx renderer3dVk_.sample_height_m(a, b) yy";
    CHECK(count_occurrences(probe, "renderer3dVk_.sample_height_m") == 1,
          "negative control: the scanner cannot see the thing it forbids");
    CHECK(count_occurrences(probe, "renderer3dVk_.max_height_m") == 0,
          "negative control: the scanner reports needles that are not there");

    for (const Banned& b : kBanned) {
        std::string src;
        if (!read_file(b.file, src)) {
            CHECK(false, "banned-edge scan could not read a source file");
            std::fprintf(stderr, "[render-dead-end] missing %s\n", b.file);
            continue;
        }
        const std::size_t n = count_occurrences(src, b.needle);
        if (n != 0) {
            std::fprintf(stderr,
                         "[render-dead-end] %s: %zu × `%s` — %s\n",
                         b.file, n, b.needle, b.what);
        }
        CHECK(n == 0, b.what);
    }

    positive_control();
    return sm::test::report("render_dead_end_law_test");
}
