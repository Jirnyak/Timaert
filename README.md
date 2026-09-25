# Timaert — Legacy of Sacrilege / The Timaert Chronicles

Нативная игра на C++23: **макромир Mount & Blade + локальный ARPG-микромир Might &
Magic 6/7/8 — одна игра, два масштаба** (AGENTS.md, шапка). Рендер — Vulkan
(MoltenVK на macOS), SDL2 — только платформа (окно, ввод, время, звук), EnTT —
движок микромира, Dear ImGui — оверлеи. Кодовое имя проекта — `timaert`; имя
продукта — Legacy of Sacrilege / The Timaert Chronicles (release.md).

**C++ в этом репозитории — вся игра.** TypeScript-прототип мёртв, паритет с ним
— не цель.

## Две шкалы одного мира

- **Макромир — основная симуляция.** Тороидальный мир 1024×1024 клеток (сторона —
  степень двойки, всегда квадрат, заворот маской; клетка ≈ 1 км²). Терраин,
  климат и ресурсы генерируются первыми; королевства, столицы, дороги и
  поселения выводятся из них. Мир живёт честно, смотрит на него игрок или нет:
  сквады ходят, экономика тикает, бои решаются авторезолвом. Сутки — 8192 тика,
  сезон — 32 дня, год — 128 дней; всякая величина мира калибруется в тиках,
  никогда в реальных секундах.
- **Микромир (субмир) — производный режим.** По решению игрока любая клетка
  макромира открывается как бесшовное окно 3×3 от первого лица (тайл ≈ 1 м):
  терраин, вода, строения, тела, бой мечом и магией в реальном времени, до 16 384
  тел в кадре. Субмир берёт из макромира КОНТЕКСТ клетки и отдаёт наверх только
  изменения с долгим смыслом — в макро-величинах (CANON S2, ЗАКОН ШВА в AGENTS §3).
- **Данжи и спец-локации** существуют сами по себе и берут из макромира только
  таблицы (CANON S27–S28).

## Документы — где что лежит

| вопрос | документ |
|---|---|
| Законы владельца и метод работы (читать первым) | [AGENTS.md](AGENTS.md) (`CLAUDE.md` — симлинк) |
| Как ДОЛЖНО быть — замысел владельца, эталон | [CANON.md](CANON.md) |
| Как ЕСТЬ в коде — карта кода обоих миров: граф тика, паспорт памяти, матрица доступа, реестр ПРАВДА/РАСХОЖДЕНИЕ, каждая строка с `file:line` | [SKELETON.md](SKELETON.md) |
| Что осталось сделать — наряды | [macro-registry.md](macro-registry.md) |
| Живой промт текущей сессии | [NEXT_SESSION.md](NEXT_SESSION.md) |
| Кейс-стади самых трудных и критичных граблей, чтобы не наступать заново | [problems.md](problems.md) |
| Лор мира — фикшн и механика, которая его производит | [lore.md](lore.md) |
| Релиз: границы демо, Steam, ассеты, риски | [release.md](release.md) |
| Архив: старые аудиты, дизайн-док, архитектура «как построено» до SKELETON, сырьё переписи 2026-09-25, `ground.md` (замеры вида земли) и `debug.md` (macOS-плейбук профилирования: Instruments, ASan, MoltenVK, lldb) | [history/](history/) — только история, не источник правды |

Порядок чтения перед кодом задан в AGENTS.md §0. Любой документ и любой код —
свидетель, а не судья; полный замысел существует только у владельца.

## Сборка

Зависимости: компилятор C++23, CMake ≥ 3.16, Ninja, **Vulkan SDK** (заголовки,
лоадер, `glslc` — шейдеры компилируются в SPIR-V на сборке), **SDL2**,
**SDL2_mixer с MP3** (без него нативная сборка падает на конфигурации), на macOS —
**MoltenVK**. EnTT 3.14.0, Dear ImGui 1.91.5 и stb подтягиваются CMake сами
(FetchContent). SDL3 не подходит: CMake требует `SDL2` и `SDL2_mixer`.

macOS (Homebrew):

```bash
brew install cmake ninja sdl2 sdl2_mixer molten-vk vulkan-headers vulkan-loader shaderc
```

Ubuntu / Debian:

```bash
sudo apt install cmake ninja-build libsdl2-dev libsdl2-mixer-dev \
                 vulkan-tools libvulkan-dev vulkan-validationlayers glslc
```

Сборка и запуск:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/timaert
```

Windows/MSVC: тот же CMake/Ninja из Developer-окружения Visual Studio; флаги
`/GR- /EHs-c- /fp:fast` ставит `CMakeLists.txt`; нужны SDL2 и SDL2_mixer (vcpkg:
`sdl2-mixer:x64-windows` или `-DSDL2_mixer_DIR=…`). Windows-сборка — цель
верификации компиляции, не авторитет геймплея (AGENTS §0).

Варианты сборки (`CMakeLists.txt`): `-DCMAKE_BUILD_TYPE=RelWithDebInfo` для
профилирования; `-DTIMAERT_ASAN=ON` — Address + UB sanitizers; `-DTIMAERT_NATIVE=ON`
— под локальный CPU (только dev). Флаги компиляции живут в одном INTERFACE-таргете
`timaert_build_flags`; игра и каждый тест собираются одной арифметикой
(`-ffast-math -fno-finite-math-only`, без исключений и RTTI). Новые `.cpp` под
`src/{app,core,gpu,ecs,macro,sub,events,content,ui,assets}` подхватываются
`GLOB_RECURSE` — CMakeLists для отдельных файлов не правится.

## Проверка

**Вердикт цитируется только из `check`** — он собирает игру и все тесты и лишь
потом запускает ctest (голый `ctest` ничего не собирает и уже отчитывался
зелёным по старому бинарнику):

```bash
cmake --build build --target check
```

Тесты — `tests/*_test.cpp`, регистрируются функцией `timaert_test` в
`CMakeLists.txt`; каждый проваливается только через `tests/check.h`
(ТЕСТОВЫЙ ЗАКОН, AGENTS §8). Приборы, которые `check` не собирает: `balance_run`
(замер экономики), `gpu_smoke`, `gpu_smoke3d`, `macro_shot`.

**Смоуки** — скриптовые прогоны настоящей игры; `smoke.sh` выходит с кодом ИГРЫ:

```bash
sh smoke.sh                      # сценарий по умолчанию (macro_travel_sp)
sh smoke.sh subworld_enter       # любой токен из src/app/smoke.cpp
sh smoke.sh cast_spell 1,7,999   # один сценарий на нескольких сидах
sh smoke.sh all                  # вся сюита — smoke_suite.txt, по строке на сценарий
```

**Снять и посмотреть кадр** (визуальное утверждение без просмотренного кадра —
не проверено, AGENTS §5 п.9): токен `capture_frame` в смоук-скрипте пишет PNG;
путь задаёт `TIMAERT_SHOT_PATH`, иначе `/tmp/timaert_shot_<NN>_<label>.png`
(`src/app/smoke.cpp`). Пример — ночной кадр субмира:

```bash
TIMAERT_SMOKE_HOUR=1 TIMAERT_SMOKE_YAW=180 TIMAERT_SHOT_PATH=/tmp/moon.png \
  TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,subworld_enter,capture_frame,quit" \
  ./build/timaert
```

Ручки смоука (все опциональны): `TIMAERT_SMOKE_SCRIPT` (токены через запятую),
`TIMAERT_SMOKE_SEED`, `TIMAERT_SMOKE_HOUR` (0..23), `TIMAERT_SMOKE_YAW`/`_PITCH`,
`TIMAERT_SMOKE_SUBPOS` (`"x,y"`), `TIMAERT_SMOKE_SPELL`, `TIMAERT_SMOKE_PROBE`.
Мутация ECS откладывает захват минимум на кадр (AGENTS §8 п.10). Безоконный путь
к кадру — харнесс `gpu_smoke3d` (`GPU_SMOKE_SHOT=<path>.ppm`,
`GPU_SMOKE_FRAMES=N`, `GPU_SMOKE_NIGHT=1`, `GPU_SMOKE_LIGHT=1`, `GPU_SMOKE_FX=1`).

**Приборы.** `balance_run [seedsCsv] [days] [outDir]` (по умолчанию `12345 256
balance_out`; `tests/balance_run.cpp:1-13`) — мир играет сам себя без окна и
пишет `world_<seed>.tsv`, `landmarks_<seed>.tsv`; код выхода 1 = сломан закон
мира; `check` его не собирает. `TIMAERT_BOOT_TRACE=1` (в Debug всегда,
`src/app/main.cpp:155-162`) печатает отчёт генезиса `[worldgen] …`, `[roads] …`.
**Консоль** (`` ` `` в игре) — ЕДИНАЯ тест-система (вердикт владельца
2026-09-07, CANON S22): состояние игры собирается командами, `exec <file>`
прогоняет файл команд как один сценарий (останов на первой ошибке). Реестр —
49 строк `register_cmd` в `register_console_commands` (`src/app/main.cpp:3930`);
`help` и `help <cmd>` печатают всё из реестра. Группы: мир (`tp`,
`tp_settlement`, `settime`, `addtime`, `simspeed [mult]`, `rest`, `revealmap`,
`chop [radius]`, `pos`, `time`), спавн (`spawn`, `spawn_squad`, `squad_orders`,
`test_battle`, `spawn_fauna`, `killall`, `possess`), инвентарь (`items`, `give`,
`take`, `gold`, `loots`/`loot`, `roll`), лист (`skills`/`skill`, `attrs`/`attr`,
`addexp`, `levelup`, `sheet`), снаряжение (`gear`, `equip`, `unequip`), спеллы
(`spells`, `learn`, `learnall`), читы (`heal`, `godmode`, `flight`), диагностика
рендера (`fpshud`, `sunfreeze`, `lightdbg [march|clouds|map|nl|haze|off]` —
сбрасываются на каждой новой игре, не настройки).

**F3 HUD и что значит низкий FPS.** Один оборот цикла — один тик и один кадр,
поэтому FPS и темп мира — одно число: строка `World: N / 64 ticks/s` ниже
номинала помечается `SLOW MOTION` и означает, что мир живёт медленнее, а не
что картинка дёргается (`src/app/main.cpp:3840-3843`); `Present: fifo` —
дисплей задаёт потолок тиков (платформа, не баг).

**Переменные окружения** (все читаются в `src/app/`, `src/sub/`, `tests/`):
`TIMAERT_BOOT_TRACE`, `TIMAERT_GPU_STATS` (таймстампы GPU — не зависят от
vsync; wall-clock под vsync слеп), `TIMAERT_SHADOW_STATS`, `TIMAERT_SEAM_TRACE`
(разбивка загрузки терраина по режимам: вход / пересечение / async / дорога —
читать метку, не последнюю строку), `TIMAERT_SEAM_SELFCHECK` (сверка
инкрементальных путей с пересчётом с нуля, включая readback материала; УДВАИВАЕТ
работу — не для замеров), `TIMAERT_NPC_VISUAL_TRACE` (`moved=0` — мёртвый мозг,
`moved>0, ticks=0` — мёртвый глаз), `TIMAERT_COMBAT_LOG`, `TIMAERT_STORY_UI_TRACE`,
`TIMAERT_VK_VALIDATION`, `TIMAERT_SMOKE_*` (см. выше), `GPU_SMOKE_*` для
`gpu_smoke3d`. Замер без пиненного сида (`TIMAERT_SMOKE_SEED`) — не число:
пересечение шва на разных мирах давало от 0.2 до 19.7 мс чисто от рельефа.

Сборки для профилирования: `-DCMAKE_BUILD_TYPE=RelWithDebInfo` (Instruments /
perf), `-DTIMAERT_ASAN=ON` (Address + UB), `-DTIMAERT_NATIVE=ON` (только dev).
macOS-плейбук (Instruments, `sample`, `leaks`, MoltenVK-знобы, lldb, шаблон
отчёта) — `history/debug.md`.

Ноль предупреждений: предупреждение — будущий баг, чинится, а не отчитывается.

## Раскладка репозитория

```
src/
  app/       SDL2-окно (Vulkan) + ImGui, главный цикл (main.cpp), смоук-харнесс
             (smoke.cpp), консоль разработчика, App-состояние (app_state.h)
  core/      тор и адрес клетки (torus.h), время (time.h), RNG, математика,
             перепись штабелей памяти (stacks.h), кубы
  gpu/       Vulkan-бэкенд: устройство, свопчейн, пайплайны, буферы, текстуры, тени
  ecs/       EnTT-мир, компоненты, системы (движок микромира)
  macro/     L1 — макромир: состояние, генерация, поля, дневной тик, AI сквадов,
             MacroStore, каталоги (существа, спеллы, фракции, товары), сейв
  sub/       L2 — субмир: окно 3×3, генераторы клетки (gens/, kit/), интерьеры
             (dgn/), тела, движение, бой, спеллы, частицы, рендер 3D
  events/    L3 — шина, логические узлы, аппликатор эффектов, квесты
  content/   L4 — данные: спеллы, сюжет, генераторы квестов
  assets/    загрузчики таблиц спрайтов (атлас 2D, банк 3D)
  ui/        ImGui-оверлеи и экраны; логикой мира не владеют
shaders/     GLSL → SPIR-V (glslc на сборке)
tests/       тесты (*_test.cpp) и приборы (balance_run, gpu_smoke*, macro_shot)
data/        авторские таблицы земли (ground_materials.csv, ground_cover.csv)
tools/       генератор таблицы земли (gen_ground_table.py), branch_density.py
smoke.sh, smoke_suite.txt   смоук-прогоны и их полный список
```

Слои включают только вниз: `content → events → sub → macro → ecs/core/gpu`
(AGENTS §11); `ui/` читает любой слой и не владеет логикой. Что именно делает
каждый слой, какие буферы держит и через какие двери к ним ходит — в
[SKELETON.md](SKELETON.md).

## Коммиты и протокол

Conventional Commits (`feat:`/`fix:`/`refactor:`/`docs:`/`test:`/`chore:`), тело
объясняет ПОЧЕМУ. Протокол сессии: план → одобрение владельца → код + тесты → диф
→ коммит после «ок» (AGENTS §1). Сейв-совместимости нет; формат сейва — не
ограничение (AGENTS §3).
