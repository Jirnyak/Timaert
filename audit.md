# TIMAERT — Independent Project Audit

> **Auditor:** autonomous audit agent (read-only on code; **may correct project
> documentation** per owner directive 2026-07-29 *«ты не меняешь код но зато
> можешь приводить к корректному виду документации проекта»*).
> **Method:** first-hand verification (`rg`/`jq`, live Vulkan frame capture,
> parallel read-only subsystem sweeps). T.A.R.S. rule: no claim without evidence.
> **Product lens:** grand-RPG — Mount&Blade macroworld + Daggerfall / Might&Magic
> 6-7-8 microworld — targeting a **Steam release**. Findings are graded for a
> *shippable, saleable* product, not just a compiling tree.
>
> **Status: HISTORICAL LOG.** Актуальный аудит проекта — **`canon-audit.md`**
> (сверка кода с эталоном замысла `CANON.md`); этот файл — исторический журнал
> аудит-сессий и больше не перепроверяется. Части II–III (2026-08-05/06)
> сохранены как есть; **ЧАСТЬ I (2026-07-29) вырезана 2026-08-23** — 8 из 10 её
> хедлайнов мертвы (Emscripten/CI/`.github`/`src/gl` не существуют, шпили
> генерятся, `smoke.sh` возвращает код игры), уцелевшее давно перенесено в
> Части II–III и problems.md.

---

# ЧАСТЬ III — сессия 2026-08-06: сквозной аудит 12 подсистем (код НЕ менялся)

> **Мандат:** владелец — «работай по аудиту, код не меняешь, документы можно
> приводить в соответствие с кодом». Ни одна строка `src/`, `tests/`,
> `shaders/`, `CMakeLists.txt` в этой сессии не тронута.
> **Ground truth сессии, снят первым делом:** `cmake --build build` — «no work
> to do» (дерево собрано), `ctest` — **49/49 PASS за 4.88 с**; смоуки
> `subworld_time`, `save_game→load_game` на сиде 12345 — PASS.
> **Дисциплина:** ни одной находки без `file:line` + цитаты; каждая помечена
> CONFIRMED (доказана чтением кода и/или прогоном) или SUSPECTED. Находки,
> уже описанные в Части I/II, помечены как известные — и отдельно отмечено,
> где документ считает баг закрытым, а код его хранит.

## III.0 Итог одной строкой

Инфраструктура (время, сейв-формат, шов, освещение, физика) — крепкая и
честная; **дыры сосредоточены там, где системы должны СМЫКАТЬСЯ**: экономика
не производит ничего, потому что деревни добывают только зерно; торговля
создаёт деньги из воздуха и позволяет игроку печатать золото на одном
предмете; провал сохранения не сообщается никому; сюжетный граф не переживает
загрузку; UI уничтожает GPU-текстуру под кадром, который её читает. Ни одна из
этих дыр не ловится ни тестом, ни смоуком — все 49 тестов зелены поверх них.

## III.1 🔴 ЭКОНОМИКА: ни один товар в мире не может быть произведён НИКОГДА

Все деревни мира создаются с одним-единственным добываемым ресурсом:

```cpp
// src/macro/state.cpp:214 — безусловно, для КАЖДОЙ деревни
vil.eco = create_economy_state({ResourceId::Grain});
```

`gather_resources` вызывается только для деревень (`world_tick.cpp:98`); города
её не вызывают вовсе (`tick_settlements_`, `world_tick.cpp:62-91`), хотя
`localResources` им заполняют (`state.cpp:165-170`). Значит Wood/Iron/Clay/
Silver/Gems не появляются в мире НИГДЕ. А каждый рецепт требует ДВА РАЗНЫХ
ресурса:

```cpp
// src/macro/economy.h:66-80 — все 15 рецептов, r1 != r2
{"Grain+Wood", "Bread", 0, 1, ...}, ... {"Silver+Gems", "Regalia", 4, 5, ...}
// src/macro/economy.cpp:70-77
if (have1 >= 1.0f && have2 >= 1.0f) { ... eco.goods[gi] += batch; }
```

⇒ `produce_goods` — вечный no-op. Каскад: `goods[]` всегда 0 ⇒ `goodPrices`
уезжают в потолок `base×10` и стоят там ⇒ `wealthSignal = 0` ⇒ `wealth` всех
городов навсегда 0 ⇒ `targetHappiness = min(1, 0.3 + 0) = 0.3` ⇒ настроение
ВСЕГО мира сходится к «Tense» ⇒ `workers = sqrt(pop) * happiness` в добыче
падает на 40 % от старта. **Проверяемо в игре:** новая игра, ~60 дней, панель
любого города → Wealth 0.0, Mood Tense, все стоки 0.0.
CONFIRMED. Ранее не описано (II.4 говорил про «крестьян по таймеру», не про это).

## III.2 🔴 Бесконечные деньги: купить у торговца и тут же продать ему же дороже

Закон цены один (`economy.cpp:337-343`), но контекст-множитель применяется ДО
него и в разные стороны, а канон даёт скидку/надбавку по харизме тоже в обе:

```cpp
// src/ui/macro_overlay.cpp:820-822
if (npc_has_trait(traits, NPCTrait::Generous)) mult = buying ? 0.9f : 1.2f;
// src/macro/economy.cpp:323-335
buy  = base*mult * max(0.5, 1 - 0.01*cha);
sell = base*mult * 0.7 * min(1.5, 1 + 0.01*cha);
```

Щедрый торговец, предмет 100 g, cha 10: **покупка 81 g, продажа 92 g**. Ни у
торговца, ни у поселения нет казны (поля золота не существует), запас не
ограничен — предмет пинг-понгом ходит между инвентарями, +11 g за клик,
бесконечно. В поселении то же самое чистой математикой при `cha ≥ 18`
(и при `cha ≥ 13`, если город Prosperous: 0.9-колонка применяется только к
покупке — см. III.11). CONFIRMED, player-facing, ломает всю экономику игрока.

## III.3 🔴 Процедурный квест доставки просит предмет, которого нет в каталоге

```cpp
// src/content/quests/procedural.cpp:52-58
case ResourceId::Grain:  return "mat_grain";
case ResourceId::Clay:   return "mat_clay";
case ResourceId::Silver: return "mat_silver";
case ResourceId::Gems:   return "mat_gems";
```

В каталоге предметов (`src/macro/items.cpp:31-55`) есть только `mat_wood`,
`mat_iron`, `mat_bone`, `mat_hide`, `mat_herb`, `misc_gem`. Четыре из шести id
не существуют. Хуже: ресурс выбирается как `argmax(resourcePrices)`
(`procedural.cpp:131-138`), а поскольку запас всех ресурсов 0 (III.1), цены
стоят в потолке `base×10`, и максимум — **Gems (50)**. То есть квест доставки
почти ВСЕГДА просит `mat_gems`, который нельзя ни залутать, ни купить, ни
выдать. Соседний `gen_fetch` (`procedural.cpp:308`) использует корректные id —
значит это дрейф, а не задумка. CONFIRMED.

## III.4 🟠 Логистический рост населения инвертирован для городов

```cpp
// src/macro/economy.cpp:146-149
const float popCap   = 100.0f + eco.wealth * 0.5f;   // wealth ≡ 0 (III.1) ⇒ 100
const float logistic = growthRate * (1.0f - pop / popCap);
```

Города рождаются с населением 600…7000 (`politik.cpp:171,210,253`), значит
скобка `(1 − pop/100)` — большое отрицательное число, и ЛЮБОЙ ненулевой
`growthSignal` (то есть любая съеденная еда) даёт отрицательную дельту:
при доставке 10 зерна в город с pop=1000 — **−5 жителей в день**. Сегодня
маскируется тем, что зерно до городов почти не доезжает; вскроется ровно на
шаге W2 «Stockpile + честный сбор» и будет выглядеть как «баланс поехал», а не
как перевёрнутый знак. CONFIRMED (арифметика), чинить ДО подключения W2.

## III.5 🟠 `settle_trade_route`: товар сохраняется, деньги — нет

```cpp
// src/macro/economy.cpp:299-320
dest.resources[c.key] += float(c.qty);            // получатель получил груз
origin.wealth += std::max(0.0f, revenue - cost);  // отправитель получил деньги
return revenue;                                    // ...и НИКТО не заплатил
```

У `dest` не списывается ничего. Это прямое нарушение объявленного инварианта
№1 («ресурс сохраняется, никакая формула не творит товар/деньги», work_vector
№1), и `wealth` — единственный вход в `popCap`, то есть фиктивные деньги
конвертируются в население. CONFIRMED.

## III.6 🔴 Провал сохранения абсолютно молчалив — и уже есть живой заряд

```cpp
// src/app/main.cpp:1933 (F5) и 8481 (пункт меню Save)
sm::save_game(app.gs, app.activeQuests, app.savePath);   // bool отброшен
refresh_save_summary(app);                                // перечитает СТАРЫЙ файл
```

`save_game` возвращает false при любом переполнении гарда `Writer::count`
(`save.cpp:69-77`) или отказе atomic-replace. Игрок увидит прежнюю дату сейва и
будет играть в мир, который больше не сохраняется. **Живой заряд:**
`PlayerState::eventLog` пишется из восьми мест (`effect_applicator.cpp:86`,
`main.cpp:2231,2301,2329`, `overlays.cpp:248,649`, `macro_overlay.cpp:789`) и не
подрезается НИГДЕ (`rg eventLog` — только push_back и чтение), а сериализуется
под капом `kMaxSmallVector = 8192` (`save.cpp:26,586`). На 8193-й записи журнала
F5 перестаёт работать молча. Тот же закон роста у `codexUnlocked`,
`completedQuestIds`, `failedQuestIds`, `factionPeaceUntilDay` — все под тем же
одним «универсальным» капом 8192. CONFIRMED.

## III.7 🟠 Сюжетный граф не переживает загрузку — и интро-ноды даже не регистрируются

Доказано прогоном, не рассуждением:

```
$ TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,quit"  → [smoke] boot#1 ... logic=3
$ TIMAERT_SMOKE_SCRIPT="...,save_game,open_load,load_game,..." → [smoke] load_boot ... logic=1
```

`boot_world_from_save` зовёт `boot_world(..., registerIntroStory=false)`
(`main.cpp:1419`), а `register_intro_story_nodes` регистрирует НЕ ТОЛЬКО интро:

```cpp
// src/content/plot/intro.cpp:139-143
logic.add(intro_main_node());
register_chapter_1_nodes(logic);      // ← и глава 1 уходит вместе с интро
```

Сегодня ущерб низкий (глава 1 — заглушка `check → return false`,
`chapter_1.h:17-19`), но формат к сюжету не готов: `LogicNodeEngine::active_` —
чистый рантайм, в сейве его нет. Как только сюжет станет настоящим, прогресс
главы будет теряться каждой загрузкой. CONFIRMED.

## III.8 🟡 `WorldTickRuntime` вне сейва — при том что комментарий обещает обратное

```cpp
// src/macro/world_tick.cpp:291-292
// The leftover steps stay in the runtime as a whole
// number, so pausing, saving or walking out mid-divisor loses nothing.
```

`WorldTickRuntime` не встречается в `save.cpp` ни разу, а `boot_world` (тот же,
что грузит сейв) делает `reset_world_tick_runtime(app.worldTick, seed)`
(`main.cpp:1229`). Теряются: `subworldStepRemainder` (< 16 шагов, пустяк),
состояние `jitter`-RNG (после каждой загрузки мир бросает ту же
последовательность заново) и — единственное с реальной ценой —
`pendingDailyTicks`: сохраниться с непустой очередью дневных тиков
(`dailyBudgetExhausted == true`, бюджет 32 дня за кадр, `world_tick.h:70`) значит
потерять эти игровые дни навсегда: часы мира их прошли, экономика — нет.
CONFIRMED (утверждение комментария ложно), эксплуатируемость узкая.

## III.9 🔴→✅ GPU: UI-текстура уничтожается ровно тогда, когда её читает кадр в полёте — ЗАКРЫТО 2026-08-11 (Сессия 19: пересоздание убрано §20 ещё 2026-08-10, остаточный resize-путь — через кладбище по фенсу; problems.md §22б)

Мини-карта субмира — постоянный элемент HUD, а не окно:

```cpp
// src/ui/overlays.cpp:3410-3418  ensure_sub_minimap — зовётся из draw_subworld_minimap_hud
const bool stale = (now - mm.lastBuildSec) > 2.0;
if (mm.tex == 0 || centerChanged || stale) build_sub_map_texture(mm, mgr, false);
// src/ui/overlays.cpp:3389-3392
if (mm.tex) destroy_ui_texture(mm.tex);       // vkDestroyImage/View/Sampler СРАЗУ
mm.tex = create_ui_texture(side, side, rgba.data(), true);
```

`destroy_ui_texture` (`src/ui/ui_gpu.cpp:74-84`) вызывает
`ImGui_ImplVulkan_RemoveTexture` + `VulkanTexture::destroy`
(`vk_texture.cpp:560-579`) немедленно — без `vkDeviceWaitIdle`, без отложенной
очереди на N кадров. При этом `kMaxFramesInFlight = 2` (`vk_renderer.h:21`), и
`acquire_frame` ждёт фенс ТОЛЬКО текущего слота (`vk_renderer.cpp:235`), то есть
кадр N−1 в этот момент ещё исполняется на GPU — и семплирует ровно эту
текстуру, потому что мини-карта была на экране и в прошлом кадре.
**Это уничтожение объекта, находящегося в использовании** (класс
`VUID-vkDestroyImage-image-01000`). Срабатывает в каждой сессии субмира каждые
2 секунды; проявление — от «ничего» до артефакта/краша драйвера, на MoltenVK
чаще всего «ничего», что и делает баг долгоживущим. Те же грабли у
`build_minimap` (`overlays.cpp:2829-2831`) и `recreate_ui_texture`
(`ui_gpu.cpp:68-72`). CONFIRMED чтением; валидацией не снято (в игре
`validation=0`, слой включается только у `gpu_smoke`).

## III.10 🟡 Тесты и игра — две разные плавающие машины

`-ffast-math -fno-finite-math-only` добавлен ТОЛЬКО цели игры
(`CMakeLists.txt:211-212`); проверено по факту сборки:

```
FLAGS (timaert)            = -O3 -DNDEBUG ... -ffast-math -fno-finite-math-only
FLAGS (audio_contract_test)= -O3 -DNDEBUG ... (fast-math нет)
```

Все 49 тестов — включая паритетные тесты генерации мира, физики, экономики —
валидируют арифметику, которой в шиппинг-бинаре нет. Заявление debug.md
(«a seed still reproduces the same world within one build») остаётся верным, но
из него НЕ следует, что зелёный тест описывает поведение игры. Это ровно тот
класс, что уже стоил сессии («verify at -O3, not -O0»), только на шаг тоньше.
CONFIRMED. Дешёвое лечение (не делал, код не трогаю): собирать тесты теми же
флагами, либо явно записать в AGENTS.md, что числовые тесты — про
не-fast-math-сборку.

## III.11 Мелкий калибр, но всё проверено построчно

| # | Находка | Где | Статус |
|---|---|---|---|
| 1 | Колонка настроения города применяется только к ПОКУПКЕ; продажа игнорирует mood (`contextMult 1.0f`) — «одна колонка» в комментарии, две в поведении | `ui/overlays.cpp:667-678` | CONFIRMED |
| 2 | `fprintf(stderr, "[ui_gpu] creating texture ...")` на КАЖДОЕ создание текстуры — в шиппинг-пути, каждые 2 с при открытом субмире | `ui/ui_gpu.cpp:50-51` | CONFIRMED |
| 3 | Хардкод пути с чужой машины `C:/Timaert/public/assets/character/` в поиске ассетов | `assets/character_paperdoll_gl.cpp:39` | CONFIRMED |
| 4 | Файл `character_paperdoll_gl.*` — не GL, а живой ImGui/Vulkan-кэш текстур; имя врёт, и это единственное место, где `src/` выглядит GL-эрой | `assets/character_paperdoll_gl.h`, `ui/macro_overlay.cpp:26` | CONFIRMED (косметика) |
| 5 | `CharacterTextureCache` — функционально-локальный `static`, `destroy()` не зовётся ниоткуда: до 4096 текстур копятся через смены мира (дескрипторные сеты ImGui — конечный ресурс) | `ui/macro_overlay.cpp:141`, `character_paperdoll_gl.cpp:163` | CONFIRMED |
| 6 | Пол населения несогласован: `max(10, …)` в одной строке и `max(0, pop − popCost)` через три — город может уйти ниже собственного минимума через рекрутинг гарнизона | `macro/world_tick.cpp:73,86` | CONFIRMED (мелкий) |
| 7 | `seaLevel = 0.40f` как default-аргумент в пяти сигнатурах при живом `gs.mapParams.seaLevel` — двойник-по-умолчанию: все боевые вызовы сегодня передают настоящий, но подпись приглашает забыть | `macro/spawners.h:26,36,57,72`, `macro/pathfinding.h:39` | CONFIRMED (правило «нет хардкоду») |
| 8 | `restore_into` раскладывает структуры по `kKinds = 5` и всё, что вне диапазона, МОЛЧА выбрасывает из `merged` | `sub/map_factory.cpp:66-95` | латентно (сегодня kind ≤ 4) |
| 9 | Кэш субмиров процесс-глобален и ключуется только `(seed, mode)`; чистится ровно в одном месте — `destroy_world` | `sub/map_factory.cpp:26-34`, `app/main.cpp:1107` | ок, но одна забытая точка входа = чужой мир под ногами |
| 10 | Слоты текстур сюжета — фиксированные 16; 17-я картинка молча не отрисуется | `ui/overlays.cpp:47` | латентно |
| 11 | Производные слои (`features`, `zones`, `pathCost`, `treeGrid`) после загрузки НЕ перестраиваются под загруженные деревни/города — держится только на том, что генерация детерминирована и рантайм их не двигает; свет и лес перестраиваются, эти четыре — нет | `app/main.cpp:1414-1475` | латентно, мина под «деревня выросла» |

## III.12 Что проверено и оказалось ЧИСТЫМ (не чинить)

* **Лестница времени** (`core/time.h`) — один целочисленный тик, все производные
  точны как рациональные, пять `static_assert` держат ладдер, `WorldTime` —
  одно поле. Образцовая подсистема.
* **RNG** (`core/rng.h`) — контракт `[0,1)` закрыт (`>> 8` × 2⁻²⁴),
  `next_int` не делит на ноль, нулевой сид переводится в 1.
* **Формат сейва** — порядок записи/чтения сверен поле в поле (14 пар), ВСЕ шесть
  хеш-контейнеров сортируются перед записью (детерминизм байт), короткий/битый
  файл ловится (`Reader::pod/str`, `read_count`, `read_bool`, FNV-чексумма,
  хвостовой байт), загрузка атомарна (`s = std::move(loaded)` только после
  полного успеха), `.tmp → verify → .bak → rename`.
* **Пауза** — один сохранённый бит + три производных причины; схема без утечки
  по построению (`main.cpp:1502-1542`).
* **Файловых mutable-статиков в симуляции нет** (проверено `rg` по `src/macro`,
  `src/sub`, `src/events`, `src/content`) — вся симуляция держится в переданном
  состоянии.
* **`.at()`, `throw`, `std::sto*`** в `src/` нет ни одного — при `-fno-exceptions`
  это было бы `abort()` на пользовательских данных. Чисто.

## III.13 🔴 РЕНДЕР: NPC выпали из «одного `lit_surface()`» — люди светятся в полночь

Проверено первой рукой:

```
$ head -8 shaders/npc.frag        → сразу layout(location=0) in vec2 vUv;  (ни include, ни push_constant)
$ grep -l 'lighting.glsl' shaders/*.frag
  billboard.frag creature.frag mesh.frag struct.frag water.frag      ← npc.frag ОТСУТСТВУЕТ
```

Регрессия внесена коммитом `7cd71e2` (перенос паперdoll-композиции на GPU):
из шейдера ушли `#include "lighting.glsl"`, `#include "shadow_common.glsl"`,
`shadowFactor(...)`, `lit_surface(...)` и `point_lights_flat(vWorld)`. Вершинный
шейдер до сих пор честно выдаёт `vWorld` и `vLightClip` (`npc.vert:22-23`) — оба
выхода мертвы, а `vk_renderer_3d.cpp:2130` биндит `litSet` в пайплайн, который
его не читает. Следствия: NPC рисуются на полной яркости круглые сутки; их не
касаются ни тени, ни точечные источники — **факел стражника (Inc 9) освещает
землю вокруг него и не освещает его самого**. При этом `render.md:217-225`
утверждает ровно обратное («npc.frag … calls `lit_surface()` … flat constant
0.75 NPCs») — константы 0.75 в коде не существует. Это самая дорогая находка
графической зоны: замысел «ОДНО освещение для всех» цел, а механизма
принуждения (теста/ассерта/грепа) нет — поэтому потеря прошла молча.
CONFIRMED.

## III.14 🟠→✅ РЕНДЕР: ресурсы уничтожаются и переписываются внутри открытого кадра — ЗАКРЫТО 2026-08-11 (Сессия 19: кладбище по фенсу + шов-аплоады в кадре + два материал-сета + WAR-барьер; render.md §The real fence contract, problems.md §22б)

Три независимых нарушения одного правила, все на пути `acquire_frame → …`:

1. `SubworldEngine::prepare_frame` (`sub/engine.cpp:2617`) вызывает
   `renderer3dVk_.upload(...)` МЕЖДУ `acquire_frame` (`main.cpp:8528`) и
   `end_frame`. Внутри — `buf.destroy(dev)` при росте инстанс-буфера
   (`vk_renderer_3d.cpp:971-972`) и `std::swap(materialTex_, materialTexAlt_)` +
   `writeMaterialDescriptor(...)` (`:1595-1596`), то есть `vkDestroyBuffer` и
   `vkUpdateDescriptorSets` по объектам, на которые ссылается ещё не
   завершившийся кадр N−1. Заявленный в `render.md:698-701` «Fence contract» в
   коде отсутствует: `vkDeviceWaitIdle` на этом пути нет ни одного.
2. Инстанс-буферы NPC/существ/частиц — по ОДНОМУ экземпляру
   (`vk_renderer_3d.h:194,204,215`), кольца нет (при том что `lightBuf_` —
   кольцевой на `kFramesInFlight`), а барьер стоит ПОСЛЕ записи
   (`:867-877`): классический WAR-хазард между `TRANSFER_WRITE` кадра N и
   `VERTEX_ATTRIBUTE_READ` кадра N−1.
3. UI-текстуры — тот же класс, но по-крупному: см. III.9.

Общая причина одна: `kMaxFramesInFlight = 2`, а `acquire_frame` ждёт фенс
только СВОЕГО слота. Честное лечение — отложенное удаление по фенсу, а не
`vkDeviceWaitIdle`. CONFIRMED.

## III.15 🟠 Тест, который охраняет отсутствие фичи (Lightning Chain)

```cpp
// src/content/spells/registry.cpp:101-102 — цепочка зашита нулями
c.px, c.py, 0.0f, /*chainDecay*/0.0f, /*chainRadius*/0.0f,
c.spellId, c.playerId, /*chainRemaining*/std::int16_t(0), ...
// tests/spell_casting_effects_test.cpp:467-473 — и тест ТРЕБУЕТ, чтобы там были нули
|| chain.chainRemaining != 0 || !nearf(chain.chainDecay, 0.0f)
|| !nearf(chain.chainRadius, 0.0f)) return fail("lightning_chain descriptor wrong");
```

`SpellDef` объявляет `chainCount = 4, chainDecay = 0.70` (`registry.cpp:257`),
UI показывает «Hits up to 5 targets», а `apply_spell_chain` выходит на первой
строке. То есть спелл — обычный болт, а зелёный ctest ДОКАЗЫВАЕТ отсутствие
фичи: честная починка сначала покрасит тест в красный, и следующий агент
решит, что сломал он. Худший класс теста в проекте. CONFIRMED.

## III.16 🟠 Уровень не восстанавливает пулы: единственный путь к «M&M full restore» недостижим

`award_exp` сам крутит `try_level_up` в цикле (`attributes.h:373-378`), поэтому
условие кнопки `exp >= expToNext` (`overlays.cpp:1215`) после любого начисления
ложно ⇒ кнопка «Level Up» не рисуется ⇒ `reset_player_combat_stats` не
вызывается никогда. При этом `attributes.h:300-305` объявляет полный restore
принадлежащим «the LEVEL-UP itself (M&M tradition)», а `rpg_rules_test.cpp:59`
проверяет формулу напрямую, минуя игровой путь. Игрок добивает бандита на 3 HP,
получает уровень — и остаётся с 3 HP, а maxHp не растёт, пока он вручную не
потратит очки. Регрессия правки «no free heal» от 2026-08-05. CONFIRMED.

## III.17 🟠 Кулдауны боя и магии измеряются в РЕАЛЬНЫХ секундах — прямое нарушение time.h

`core/time.h:17-19` — самое категоричное утверждение проекта: «REAL SECONDS
APPEAR IN EXACTLY ONE CONSTANT ON THIS PAGE and nowhere else in the game».
Фактически `spell_book.cpp:101,148` кладёт `sb.cooldowns[id] = d->cooldown`
(2.0f, 120.0f) и вычитает `dt = kStepSeconds`. Под землёй мировые часы идут
÷16, а кулдаун — по настенным: **армагеддон с 120-секундным кулдауном стоит
0.94 игрового дня в подземелье и 15 игровых часов на поверхности** — одно
заклинание с двумя разными игровыми кулдаунами в зависимости от слоя. Туда же
`manaDrain` haste. CONFIRMED; §II.8 («время: сим-тик чист») в этой части
устарел.

## III.18 🔴 ECS/спавн: пять параллельных «рождений гуманоида», уже разошедшихся

`spawn.cpp:152` (горожане) / `:480` (отряд игрока) / `:621` (проекция макро-NPC),
`engine.cpp:1672` (враждебный спавн), `npc_spawn.cpp:45` (макро). Разошлись по
факту: `NpcCharacter` нет у отряда игрока, `NpcInventory` нет у горожан и
проекций, `NpcTraits` только у макро, `SubworldAi.radius` 0.55 против 0.8,
`Sprite.scale` 0.55/0.8/1.0, `VisualPos.speed` 32 против 48. Самое видимое
следствие: **отряд игрока невидим** — у солдат нет `NpcCharacter`, а проход
паперdoll'ов рисует `view<Position, NpcCharacter>`; спрайт солдата создаётся
шестиаргументным `emplace`, поэтому `archetype` остаётся `0xFF`, и проход
существ его тоже пропускает (`vk_renderer_3d.cpp:827,891`). Игрок входит в
субмир с десятью наёмниками и видит пустое поле, по которому невидимо кто-то
дерётся. CONFIRMED. §9.4 назвал дублирование; это его реализовавшаяся цена.

## III.19 🔴 Микромир без последствий для макромира (и обратная ферма)

* Смерть проекции макро-NPC не трогает оригинал: `resolve_subworld_deaths`
  (`sub/engine.cpp:2054-2151`) не знает слова `MacroOrigin`, HP копируется
  внутрь и наружу не возвращается. Убил лорда в субмире, вышел, вошёл — лорд
  снова цел, лут снова падает (сид лута зависит от id новой сущности). CONFIRMED.
* Живность и горожане ре-спавнятся при каждом возврате клетки в окно
  (`engine.cpp:1062-1076`, сид клетки — чистая функция координат): вырезал стаю,
  отошёл на две клетки, вернулся — стая на месте. CONFIRMED.
* Проекция макро-NPC делается ТОЛЬКО в `enter()` (`engine.cpp:622`), а
  `despawn_subworld_entities_outside_window` бережёт только `PlayerSoldierTag`
  и `PlayerTag` (`spawn.cpp:397`), тогда как соседний реапер бережёт ещё и
  `MacroOrigin` (`:284`) — два списка исключений в двух копиях, уже разных.
  Пересёк шов — макро-фигуры в окне исчезли до конца сессии. CONFIRMED.
* `route_macro_npc_attack` (`main.cpp:878-895`) безусловно уничтожает
  макро-сущность, чем бы бой ни кончился, а популяция создаётся ровно один раз
  на буст и не пополняется ⇒ каждое нажатие «атаковать» на карте необратимо
  уменьшает мир. CONFIRMED.
* `MacroSpawnId` («стабильная личность через сейв») выдаётся как
  max-по-ЖИВЫМ + 1 (`npc_spawn.cpp:252-257`) — после гибели носителя максимума
  ординал переиспользуется, и `reattach_player_to_macro_spawn` после загрузки
  может вернуть игрока в ЧУЖОЕ тело. CONFIRMED (§9.1 проверял детерминизм, не
  переиспользование).

## III.20 🟠 Субмир: подъём без предела, персистентность по порядку записи, шов без гистерезиса

* **Нет максимального градиента подъёма.** `vertical_step` прилипает к опоре при
  `z <= supportZ + kGroundStickM` (`sub/height.h:141`) — условие выполняется при
  ЛЮБОМ подъёме. У масонри предел есть (`kStepUpM = 0.9`, `collide.h:44`), у
  рельефа нет: на бордюр в метр не залезть, на скалу 67° — взойти пешком.
  Серпантины дорог (`kGradePen = 18`) при этом никому не нужны. CONFIRMED.
* **У игрока нет закона земли.** `move_player` (`engine.cpp:2307-2360`) — это
  `playerX_ += wx * kSubworldFirstPersonMoveScale`, без `kTileMovementSpeed` и
  без штрафа за уклон, которые для NPC объявлены данными (`battle.h:367-370`).
  Игрок пересекает воду, скалу и дорогу с одной скоростью. CONFIRMED.
* **`restore_into` сопоставляет структуры ПО ПОРЯДКУ внутри `kind`**
  (`sub/map_factory.cpp:79-95`): после рубки `saved` = N−1, `fresh` = N ⇒
  копируются `saved[0..N−2]` и дописывается `fresh[N−1]`, который уже есть ⇒
  срубленное дерево возвращается (клоном последнего). Лес в субмире не редеет
  никогда, а макро-счётчик можно выдоить в ноль. CONFIRMED.
* **Порог шва без мёртвой зоны** (`seamless_manager.cpp:877-880,934-935`): после
  сдвига игрок стоит ровно на границе, шаг назад на 0.2 тайла — полный переход
  снова (3-5 клеток в плейсхолдер, задания генерации, до 5 сохранений). Плюс
  гонка: `queue_generation` читает кэш ДО того, как `queue_save` его наполнит
  (`:566-567` против `:951-953`), поэтому быстрый разворот на шве откатывает
  правки клетки. CONFIRMED.
* **Двойная система высот.** Физика, коллизия, деревья и строения читают
  `heightVtxM_` — сетку 193² (шаг 16 тайлов), а не потайловую `mgr.heightmap()`
  (`vk_renderer_3d.cpp:2303`). Любой генераторный проход тоньше 16 тайлов
  (площадка под дом, узкая река) в игре не существует, хотя тест
  `house_pad_flatten_test` меряет именно потайловую карту и потому зелёный.
  Плюс сам авторитет высоты физически живёт в классе рендера, а не в
  `height.h`. CONFIRMED.

## III.21 🟠 Главный цикл и UI: пять дефектов, каждый воспроизводится руками

| Что | Где | Проявление |
|---|---|---|
| `app.panning` включается клавиатурой и НИКОГДА не сбрасывается (сброс только на `MOUSEBUTTONUP`) | `main.cpp:2151` vs `:1967` | после первого же WASD камера навсегда отвязана от партии (README обещает «eases back»), и движение мыши БЕЗ зажатой кнопки начинает таскать карту скачком |
| F3 (debug HUD) входит в `gameplay_panel_open`, хотя намеренно исключён из `pausing_panel_open` | `main.cpp:1499` vs `:1488` | в субмире F3 парализует игрока (ни шага, ни удара, ни обзора), пока мир продолжает его бить |
| Консоль не читается ни одним гейтом ввода | `main.cpp:1544-1555` | под землёй курсор телепортируется в центр, мир не стоит, в поле ввода почти не попасть |
| Панель, СКРЫТАЯ в «Interface», продолжает паузить мир | `main.cpp:1488` vs `:8677` | нажал I при скрытой панели — мир стоит, бейдж «close the panel», закрывать нечего |
| Кнопки тулбара «>> Fast (4x)» и «Z Rest until morning» не читаются нигде (`speed4`, `rest`) | `ui/screens.h:103` | две видимые активные кнопки не делают ничего |

Туда же: `wait_visible` — сценарий, который не может упасть
(`smoke_framebuffer_has_world_pixels` — заглушка `return true; samplesHit = 9`,
`main.cpp:687-691`), и `cast_bolt_capture` фотографирует кадр, записанный ДО
создания болта (нарушение собственного правила «capture ≥1 frame later»,
которое соседний `light_probe_capture` соблюдает). CONFIRMED.

## III.23 🔴 ТЕСТЫ: три теста НЕ МОГУТ ПОКРАСНЕТЬ — «49/49 зелёных» завышено

Проверено лично:

```cpp
// tests/world_tick_parity_test.cpp:10
int fail(const char* msg) { std::fprintf(stderr, "... FAIL: %s\n", msg); return 1; }
// :21  bool test_hour_rollover() { ... return fail("..."); }   ← int 1 → bool TRUE = «прошло»
// :213 int main() { if (!test_hour_rollover()) return 1; ...   ← недостижимо
$ grep -c 'return false' tests/{world_tick_parity,macro_npc_ai_parity,material_seam}_test.cpp
  0 / 0 / 0
```

Тот же дефект в `tests/macro_npc_ai_parity_test.cpp:10` и в `bool`-разделе 7
`tests/material_seam_test.cpp:49`. Следствия по существу:

* **`world_tick_parity_test` мёртв целиком** — а именно на него `MANIFEST.md:387`
  ссылается как на доказательство целочисленной лестницы времени («10000
  one-tick advances == one 10000-tick advance»). Доказательства нет.
* **`macro_npc_ai_parity_test` мёртв целиком** — все 10 проверок макро-ИИ.
* **Раздел 7 `material_seam_test` мёртв вместе со своим негативным контролем** —
  а `MANIFEST.md:388` объявляет баг «здания меняют оттенок на шве» закрытым
  именно им.

Компилятор молчит: `-Wall -Wextra` не включают `-Wint-in-bool-context` для этой
конверсии. Одна правка (`return false` / общий `check()`) воскрешает 16 уже
написанных проверок. **До неё любая цифра «N/N green» в документах —
завышена.** CONFIRMED, проверено первой рукой.

## III.24 🔴 `smoke.sh` всегда возвращает 0 — смоук-слой это документ, а не гейт

```sh
# smoke.sh:31-35
./build/timaert 2>&1 | grep '^\[smoke\]'
```

Код возврата конвейера — это код `grep`, а строки `[smoke]` печатаются всегда.
Честный `exit 2` из `main.cpp:8993` выбрасывается; ни `set -o pipefail`, ни
агрегации по сидам нет. `sh smoke.sh a,b,c 1,7,999` при девяти красных
прогонах завершится успехом. Плюс `wait_visible` не может упасть (III.21), а
`gpu_smoke3d` возвращает 0 при любой картинке — все формулировки «verified by
eye» в документах означают ручную сверку PNG человеком, а не гейт. CONFIRMED.

## III.25 🟠 Зелёные тесты над мёртвым кодом: `econ_day` и `celestial.h`

* `rg "econ_day.h" src/` → единственное попадание — сам `econ_day.cpp`. Весь
  «честный экономический день» вызывается только из `tests/econ_v1_test.cpp`.
* `macro/celestial.h` не подключён НИ ОДНИМ файлом `src/`: две луны с фазами,
  три созвездия, `kSkyStarSizeScale` — инертны; `sky.frag:124-140` рисует одну
  всегда полную луну, `kMoonDirGain` фазы не знает. `celestial.md:26-27` при
  этом пишет, что `moon_illumination01` «replacing today's hardcoded
  always-full moon» — замены не произошло.

363 зелёные строки тестов создают впечатление, что экономика и небо покрыты.
Это тот же сюжет, что уже разбирался с `sys_level_up` (problems.md §16.4).
CONFIRMED.

## III.26 🔴 КВЕСТЫ/СОБЫТИЯ: аппликатор эффектов инертен, девять тегов мертвы, брошенный квест = ферма бандитов

* **`effect_applicator.cpp` не делает ничего в реальной сессии**: продюсеры
  QuestComplete/QuestFail всегда ставят `ev.b = kEventEffectAlreadyApplied`
  (`quest_engine.cpp:179,209`), а единственный продюсер ApplyEffect/CodexUnlock/
  BattleStart — таблица энкаунтеров, которая недостижима (никто не ставит
  `pendingEncounterIdx`). Глаголы `heal_hp/damage_hp/restore_mp/grant_xp` —
  недостижимый код, вызываемый 4 раза за тик.
* **Мёртвые теги (перепись по прод-коду, без смоуков):** SettlementVisit,
  QuestStart, QuestUpdate, SpellLearned, LandmarkChangeOwner, WorldCellChange,
  PlayerLeaveSettlement, Custom, SpellCast. При этом комментарий
  `events/event_types.h:15-17` утверждает, что у каждого тега есть и продюсер, и
  потребитель — это неправда для девяти из 24.
* **Брошенный квест не записывается никуда** (`quest_engine.cpp:228-239`:
  QuestFail со `s2="abandoned"` → аппликатор делает `break`, ни
  `completedQuestIds`, ни `failedQuestIds` не трогаются) ⇒ `is_known()` ложно ⇒
  «Accept» снова активен в тот же день. А `gen_destroy` кладёт N штук
  `SpawnEntity` в `onAccept` ⇒ цикл Accept→Abandon→Accept плодит неограниченное
  число враждебных NPC у одной клетки и бесплатный XP-фарм.
* **Выбор родины в интро даёт репутацию несуществующей фракции**:
  `intro.cpp:28-32` предлагает `magika/empire/barbarians`; в реестре
  (`macro/faction.h:93-160`) `barbarians` НЕТ (есть `barbarian_north/south/
  west/east`), а `magika` — это странствующий орден, не магократия. Работает
  один вариант из трёх; остальные создают фантомную строку с
  `faction_index == -1`.
* **Три статьи кодекса (`mage_rulers`, `empire_of_light`, `witches`) не имеют
  ни одного пути разблокировки** — написанный лор игрок не увидит.
* **Обучающий диалог после интро мёртв**: `main.cpp:2235` ждёт квест с
  префиксом `q_travel_`, которого не создаёт никто (`rg "q_travel"` — одна эта
  строка). Онбординг обрывается на выборе имени.

CONFIRMED (все пункты — по цитатам кода).

## III.27 🔴 СБОРКА И БЕЗОПАСНОСТЬ

* **P0, требует решения владельца: в локальной ODB этого клона всё ещё лежат
  недостижимые блобы с ЖИВЫМИ значениями Anthropic-токенов** (дампы терминала
  с `export ANTHROPIC_AUTH_TOKEN=…`). Проверено: `git fsck --unreachable` даёт
  **2461** недостижимый объект, packs — **76.25 MiB** против ~21 MiB, о которых
  говорит запись о вычистке истории; рабочее дерево и все ветки чисты.
  Убирается `git reflog expire --expire=now --all && git gc --prune=now`. Но
  до перезаписи истории эти объекты были на публичном `origin`, а GitHub не
  удаляет недостижимые объекты сам ⇒ **токены обязаны считаться
  скомпрометированными и должны быть ротированы, если это ещё не сделано**.
  (§4.3 знал про сам факт коммита токена; новое здесь — что блобы пережили
  вычистку файла.)
* **`build-asan/` — не asan**: `TIMAERT_ASAN:BOOL=OFF`,
  `CMAKE_BUILD_TYPE=RelWithDebInfo`. Кто запустит его «чтобы поймать UB»,
  получит ноль диагностики и ложную уверенность. 568 МБ.
* **Четыре устаревших дерева сборки** (`build-agent/debug/reldeb/asan`, все от
  27-29 июля) содержат по 27 `.spv` против 29 сегодняшних — запуск любого из них
  воспроизводит ДРУГУЮ программу, и внешне это ничем не выдаётся.
* **`stb` тянется с `GIT_TAG master`** (`CMakeLists.txt:74-77`), тогда как EnTT и
  ImGui закреплены тегами: свежий clone получит тот stb, который окажется в
  master в этот день. Он декодирует все PNG игры.
* **`-Werror` нет нигде**, CI нет (`.github/` удалена целиком), `assert()`
  мёртв во всех используемых конфигурациях (`-DNDEBUG` и в Release, и в
  RelWithDebInfo) — три слоя контроля отсутствуют одновременно, остаётся
  человек, читающий лог.
* **1160 из 1461 строки `CMakeLists.txt` — один блок, скопированный 49 раз.**
  Поэтому расхождение по fast-math (III.10) нельзя починить одной строкой: его
  физически негде записать один раз.
* **`assets/` и `public/assets/` — байт-в-байт дубликаты**, обе в git (~12 МБ
  лишнего в каждом clone); десять активов (~3.5 МБ ×2) не упоминаются в коде
  ни разу, включая `Roboto-Black.ttf`, который README:188 называет «UI font».
* **README описывает другой проект**: «SDL2 + C++17», зависимости
  `SDL2_image`/`SDL2_ttf`, инструкции для пяти платформ. Реально: C++23,
  **SDL2_mixer обязателен** (иначе `FATAL_ERROR`), `Vulkan REQUIRED`, нужен
  `glslc`. По README проект не собирается ни на одной из перечисленных
  платформ. (Правку README не делал — это изменение витрины проекта, выношу
  владельцу вместе с остальным.)

## III.28 📄 ДОКУМЕНТЫ ПРОТИВ КОДА: двенадцатый проход (закрыт после подведения итога)

Опорные факты, на которые можно ссылаться (проверены лично): `kSaveVersion = 21`
(`state.h:56`) · **49** тестов в ctest · атрибутов 9, скиллов 8
(`attributes.h:172,176`) · `main.cpp` = **9008** строк · **ноль** compute-шейдеров
и `vkCmdDispatch` во всём репозитории.

**Имена, которых нет в коде, но которые документы предписывают как рабочие**
(каждое проверено `grep` — ноль совпадений в `src/` и `tests/`):

| Имя из документа | Где предписано | Что на самом деле |
|---|---|---|
| `spawn_hostile_npc` | monsters.md:12,148,155; MANIFEST:383; MASTER_PROMPT:178,997; render.md:194 | `SubworldEngine::spawn_npc_body` (`sub/engine.h:124`) |
| `kCrowdPenalty`, `tick_combat_move` | microcombat.md:92; ARCHITECTURE:452 | `tick_subworld_combat`; расталкивание — сетки `sub/battle.cpp`, без константы |
| `sm::SpatialHash`, `sub/spatial_hash.h` | AGENTS.md:188 («use it»); ARCHITECTURE:53,847 | бакет-сетки `sub/battle.h:178`, `sub/collide.h:105` |
| `kMaxPointLights` | ARCHITECTURE:1207; MASTER_PROMPT:842 | `kSubworldMaxLights = 32` (SSBO, не C-массив) |
| `generate_spires()` | landmarks.md:16; ARCHITECTURE:614,629 | генератора нет: `gs.spires` только очищается и читается из сейва |
| `kObjectiveCheckers`, `kRewardAppliers` | ARCHITECTURE:1264-1309 | `switch` по `ObjectiveKind`/`RewardKind` — data-driven тут аспирация |
| `FT_Tree` как строка enum | MANIFEST:374; macro-lighting.md:107 | enum сегодня `FT_None/FT_Road/FT_DirtRoad/FT_Field`; крона — непрерывный `kCanopyOpticalCost` |
| `control <id>` (консоль) | MANIFEST:217; possession.md:55 | команда называется `possess` |
| `vulkan_plan.md`, `translation.md`, `timaert_c/` | AGENTS:48; MASTER_PROMPT:951,1010; design.md:6,1996 | не существуют |
| 6 имён UI-функций (`draw_quest_overlay` и др.) | ARCHITECTURE:771,1334-1345 | `draw_quest_log`, `draw_codex`, `draw_diplomacy`, `draw_settlement`, `draw_death_screen`; `draw_pause_overlay` не существует вовсе |

**Числа:** тесты — доки называют 43/45/46/26, в коде **49**; сейв — v10/v12/v18,
в коде **21**; год — design.md:463 «100 дней» против **128**; «kAiTicks = 32 =
полчаса игрового времени» (macrosim.md:21, time.md:135, entry_context.h:31) — на
деле **5.6 игровой минуты**; «кап 512 NPC» (ARCHITECTURE:153) — реально
`kMaxEntityInstances = 16384`; затухание civ-поля 0.012 против **0.06**
(`zones.cpp:25`); «every макро-NPC проецируется» (possession.md:57) — кап
**128** (`spawn.cpp:24`).

**Статусы наоборот — сделано, а числится несделанным:** тени спрайтов
(ARCHITECTURE:1164 «Pending» при 4096² карте, 5 caster-пайплайнов и 10 шейдеров);
точечный свет (ARCHITECTURE:1206 «следующий инкремент», MASTER_PROMPT:838 «no
upload path exists» — при живом SSBO, трёх формах `point_lights*` и двух тестах в
ctest); дома и стены (ARCHITECTURE:1161 «not implemented» — рендерятся проходом
A5); макро-свет (MASTER_PROMPT:268 «UNCOMMITTED, дерево не линкуется» — вызывается
из `main.cpp:1161-1200`); высотная окклюзия свечения (render.md:876 и
macro-lighting.md §7 «planned follow-up» — работает через `kGlowClimbCost`, и тот
же файл в §Increment C это описывает, противореча сам себе).

**И наоборот — числится сделанным, а инертно:** GPU-симуляция толпы в настоящем
времени в microcombat.md:108, macrosim.md:81, microworld.md:370 и во всём
разделе vulkan.md:152-204 (ARCHITECTURE уже получил баннер «NOT YET
IMPLEMENTED», остальные четыре — нет); econ v1 в economy.md не упомянут вовсе;
`flatten_footprint` (microworld.md:193) определён и не вызывается ни разу.

**README — витрина описывает не эту программу.** Блок 1-81 подан как текущий:
«2D tile-based, SDL2 + C++17», «object-pooled entity system 16384 (128×128
pool)», «zoom 0.25×–4×», «64 diffusion steps» — и **выдуманная целиком таблица
«Core System Configuration Parameters»** (`MAX_BUFFER_SIZE`, `FRAME_RATE_TARGET`,
`ENABLE_TELEMETRY`, `THREAD_POOL_COUNT` — ноль совпадений в `src/` и
`CMakeLists.txt`, проверено). Раздел Controls при этом проверен построчно и
**точен**; более того, он точнее MANIFEST, который не знает четырёх реальных
биндов.

**Реализовано, но не записано нигде:** `FT_Field` (пашни: контекстное
размещение, рендер, вес хода — в features.md ноль упоминаний); частицы Inc C
(кровь/пыль по архетипу, ×1.8 на смертельном ударе — память проекта числит Inc C
PENDING); массовый бой `sub/battle.*` (microcombat.md:3 прямо заявляет «there is
no separate battle mode»); макро-стамина и `movement_cost.h`; entry-context;
кап проекции 128; три бампа сейва v18→v21.

**Дефект кода, найденный документным проходом:** комментарий `sub/height.h:33`
«Projectiles: own their z with NO ceiling» противоречит
`spell_effects.cpp:374`, где потолок `kFlightMaxAboveTerrainM` применяется. Здесь
права документация, а врёт код.

## III.22 Что осталось незакрытым в этой сессии

Все двенадцать проходов закрыты и записаны выше (двенадцатый — сквозная сверка
документов — вернулся последним и лёг в III.28).

Отдельно: правки документов под код в этой сессии НЕ выполнялись, хотя мандат
их разрешает. Причина — их набралось много (README целиком врёт про
зависимости и стандарт; `time.md` про «три места с реальным временем» и
«kAiTicks = полчаса»; `render.md` в шести местах; `MANIFEST.md` про 43 теста и
про «TS — источник истины по геймплею»; `celestial.md` про замену полной луны;
`design.md` про 100-дневный год и пять удалённых тегов событий;
`movement_cost.h` про 25 клеток/час), и часть из них — это выбор, ЧТО считать
правдой: документ или код. Полный список с точными строками собран в отчётах
проходов и подан владельцу; править витрину проекта без его слова не стал.

---

# ЧАСТЬ II — сессия 2026-08-05: карта моделей + чистка A/B/C (ветка `audit/abc-2026-08-05`)

> Новые разделы этой сессии добавляются СЮДА, сверху вниз, один вывод — один
> раздел, первой строкой суть. (Старый аудит — Часть I, 2026-07-29 — вырезан
> чисткой 2026-08-23: 8 из 10 хедлайнов были мертвы; см. шапку.)

## II.0 Решения владельца, принятые в этой сессии (зафиксированы)

Суть: четыре решения через AskUserQuestion, они управляют всей сессией.

1. **RNG чинится целиком СЕЙЧАС** — и `next_f01()` (честный [0,1)), и guard
   `next_int` от деления по модулю нуля. Владелец принял цену: мир seed 12345
   перегенерируется, смоуки перебазируются.
2. **work_vector.md сортируется по фундаментальности систем**, не по близости
   к «часу геймплея».
3. **Атрибутов ровно 16, фиксировано** (сначала 8, владелец поднял до 16 «про
   запас» — хранение то же по форме, 16×u8, po2). Оговорка агента принята: в UI
   и генерацию листов выставляются только атрибуты с потребляющей формулой,
   остальные — зарезервированные индексы.
4. **Инвентарь 16×16 = 256 клеток, фиксировано** (индекс слота = ровно байт).
   Экипировка — отдельная малая таблица слотов поверх.

## II.1 Класс C: контракт RNG починен (тест красный → фикс → зелёный)

Суть: `next_f01()` мог вернуть ровно 1.0f (верхние ~2^8 кодов u32 округлялись
вверх при делении float(u32)/2^32 — мантисса 24 бита), `next_int(lo,hi)` при
`hi==lo` делил по модулю нуля (UB; на ARM молча возвращает мусор).

- Тест `tests/rng_contract_test.cpp`: xorshift32 — биекция на ненулевых u32,
  тест ИНВЕРТИРУЕТ шаг и загоняет настоящий генератор в каждый код верхней
  полосы 0xFFFFFF80..0xFFFFFFFF. На старом коде падал:
  `FAIL: next_f01 escaped [0,1) on a top-band draw`. Плюс вырожденные диапазоны
  `next_int(5,5)`/`next_int(7,3)` и выборочная проверка закона диапазона.
- Фикс `core/rng.h`: `next_f01() = float(u32 >> 8) * 0x1.0p-24f` — каждое
  значение точно представимо, максимум 1−2^-24; `next_int` схлопывает
  вырожденный диапазон в `lo` (для всех валидных вызовов поведение прежнее
  бит-в-бит — все живые вызовы имели hi>lo, перепись вызовов в II.census).
- `kSaveVersion` 18→19: тот же сид теперь порождает другой мир, сейвы с
  worldSeed недействительны.
- Доказательство безопасности: rng_contract_test красный на старом коде,
  зелёный на новом; полный ctest **44/44 passed** после фикса (юнит-сюита
  мир-независима); смоук-сюита: 16 сценариев прогнаны на новом мире, 15
  зелёных сразу, один (`console`) чинился — см. II.2.

## II.2 Перебазирование мира выявило ровно один мир-зависимый смоук

Суть: после смены мира seed 12345 красным стал только `console` — и это был
класс §16 (сценарий утверждал не тот инвариант), не регрессия логики.

Сценарий спавнил бандитов БЕЗ фракции; правило «земля решает»
(spawn_npc_body: тело без фракции получает фракцию владельца клетки —
намеренное, задокументированное) на новом мире дало «бандитам» фракцию
дружелюбного королевства. `killall` (убивает по ВРАЖДЕБНОСТИ) их честно не
тронул, а проверка считала по ТИПУ. Починено (`ef32528`): спавн с явной
фракцией `bandits` (реестр приколачивает её playerReputation = −100) + новое
утверждение настоящего инварианта — повторный `dev_kill_all_hostiles()`
обязан вернуть 0. Свип: seed 1, 7 — PASS; валидированный прогон
(console+subworld_loot_xp+subworld_time) — PASS, только известный 05137.

## II.3 Инструмент branch_density ЛГАЛ: он не видел ~600 строк main.cpp

Суть: измеритель, по которому выставлена цель сессии, имел слепое пятно на
26 КБ живого кода — базовые числа владельца были недосчётом.

Старый стриппер комментариев — два regex'а. `/*`, стоящий внутри
`//`-комментария (main.cpp, ~строка 78), открывал ложный блочный комментарий
до ближайшего `*/` (строка ~673, `/*app*/`): 26 040 символов реального кода —
весь смоук-парсер — не участвовали в счёте. Поэтому моя замена 54-веточной
цепочки на таблицу «не сдвинула» метрику: правка попала в слепое пятно.
Починено (`d1e7256`): 5-состоянийный лексер (код / `//` / `/* */` / строка /
char-литерал); содержимое строк тоже больше не считается (в логах живут
«if (» и «||»). Честные числа ОДНИМ инструментом:

| | всего условий | main.cpp | engine.cpp |
|---|---|---|---|
| до сессии (main) | **5330** | **1473** | 446 |
| после (ветка) | **5271** | **1421** | 443 |

Урок: прежде чем оптимизировать метрику — проверь измеритель. Это тот же
класс, что «смоук мерил случайный мир» (§16) и «self-check проверял быстрый
путь самим быстрым путём» (§15.3).

## II.4 МАКРО: экономика и сущности — две симуляции, не связанные ни строкой

Суть: «живой мир» отсутствует не потому, что систем мало, а потому что две
существующие (поселенческая экономика и передвижение NPC) не разделяют ни
одного состояния. Всё, что владелец назвал недостающим, лежит ровно в этом
зазоре.

Проверено чтением всего world_tick.cpp / npc_ai.cpp / economy.cpp:
- **Дровосеки и крестьяне НЕ собирают ничего.** `ai_woodcutter` — чистая
  машина позиций (Idle→Traveling→Working→Returning), ветка Working меняет
  только таймер. Ресурсы производит абстрактная формула
  `workers = sqrt(population) * happiness` (economy.cpp:55-62) — число живых
  NPC не влияет вообще. Слой деревьев (set_tree_count, готовый для лесорубов)
  из npc_ai не трогается.
- **Караваны — декорация.** NPCType::Caravan/Merchant ходят между
  поселениями, не неся ни груза, ни золота, ни касания EconomyState.
  Параллельно живёт абстрактный TradeRoute-механизм — невидимый на карте.
- **Отношения фракций заморожены навечно** после boot (все записи в
  `relations[]` — create_factions + add_player_reputation, только строка
  игрока). Прокси-войны структурно невозможны.
- **Бандиты видят только игрока** (ai_aggressive читает ровно
  ctx.playerX/playerY): не грабят караваны, не трогают деревни. Спавн NPC —
  один раз на boot, респавна нет; загрузка сейва воскрешает всех убитых
  (ECS не сериализуется, spawn_macro_npcs перегенерируется от сида).
- ~~**NPC ходят по океану бесплатно**: один закон стоимости местности
  (movement_cost.h) применяется только к игроку (travel.cpp), NPC двигаются
  чистой геометрией с плоским `rt.sp -= 10`.~~ **ЗАКРЫТО Сессией 21
  (2026-08-06)**: try_move платит по тем же строкам movement_cost.h через
  грид app.pathCost, жадный шаг обходит воду, океан топит лорда
  (kExhaustionBite в HP); итог в proposals/session-prompts.md.
- Отсутствуют как класс (ноль кода): войны, лорды/партии, завоевание,
  динамика дипломатии, поведение культов (есть только строка фракции),
  именованные сюжетные сквады, еда/мораль отрядов.

### Три ДОКАЗАННЫХ бага макроэкономики (перепроверены первоисточником)

1. **Коллизия ID: деревни экспортируют в пустоту.** Поселения и деревни
   нумеруются из НУЛЯ каждые в своём списке (state.cpp:152 `s.id=int(i)`;
   state.cpp:188+206 `villageId=0; vil.id=villageId++`), а резолвер прибытия
   маршрута ищет СНАЧАЛА по поселениям (world_tick.cpp:127-136): origin
   деревенского маршрута с id=k связывается с settlements[k]. Товар честно
   списан с деревни при отправке (по живому указателю), а выручка при
   прибытии зачисляется НЕ ТОЙ экономике. Деревни вечно беднеют, их popCap
   заперт.
2. **Плоская дистанция на торе.** economy.cpp:186-189 и :281-284 считают
   `dx*dx+dy*dy` без обёртки — пара соседей через шов карты выглядит
   максимально далёкой и не торгует никогда. Нарушение инварианта
   тороидальности; НЕ починено в этой сессии сознательно: фикс метрики в
   системе, которая одновременно мис-доставляет грузы (п.1), — это две
   полупочинки; в векторе это ОДНА работа «торговая сеть» с решением
   владельца по кодировке ID.
3. **Гарнизоны растут без предела и молча портят сейв.** Ежедневно
   generate_garrison добавляет до 10 солдат (world_tick.cpp:79-87), удаляет
   их только найм игрока по одному. При пересечении kMaxSoldiers=8192
   (save.cpp:39) Writer::count ставит ok=false — падает сохранение ЦЕЛИКОМ,
   примерно на ~820-й игровой день у крупного города.

Плюс выброшенная работа на каждом boot: полный Voronoi-проход
generate_politik (politik.cpp:376-388, ~63 млн вызовов метрики на 1024²)
целиком перезаписывается finalize_politik.

## II.5 ШИНА СОБЫТИЙ: это буфер с нулём подписчиков; 15 из 40 тегов — мертвы

Суть: «шина» де-факто — потиковый буфер, который читают опросом; подписка
как механизм в проде не используется вовсе (boot даже ассертит
subscription_count()==0, main.cpp:1381).

- Из 40 EventTag: **15 живых**, **15 без единого упоминания вне enum**
  (PlayerDeath, NpcSpawn, Encounter, Trade, BattleEnd, MagicSurge,
  FactionRelationChange, DialogStart, CameraMove, NpcHpChange,
  SettlementMoodChange, PlayerStatChange, NpcGreeted, SettlementChangeOwner,
  QuestAbandoned), остальные подключены с одной стороны.
- **SpawnEntity: по-прежнему 2 продюсера, 0 потребителей** — kill/defend
  квесты кладут спавн врагов в onAccept, quest_engine честно эмитит, никто
  не слушает: **враги не появляются**. PlayerLevelUp теперь 0/0 (узел
  удалён, продюсера не было никогда).
- Из 6 глаголов целей квестов работают 3 (VisitCell, DeliverItems,
  DestroyNpc — его старый баг id-vs-type ПОЧИНЕН, проверено); WaitAt
  недостижим (gen_protect требует деревню, а
  `generate_quests_for_village` НЕ ВЫЗЫВАЕТСЯ нигде — grep: только
  объявление+определение); FindLocation и InteractCell не генерятся и/или
  ждут событий без продюсеров.
- `Objective` — не таблица: switch по kind в ТРЁХ местах (eval, маркер, UI).
  ARCHITECTURE.md описывает kObjectiveCheckers/kRewardAppliers/
  kQuestGenerators — **этих символов не существует** (📄 доку врёт).
- Сюжет = интро (9 слайдов, работает) + chapter_1 из одного узла
  `return false`, зацикленного на себя. Квест-цепочек нет ни в каком виде
  (нет ни prerequisite, ни next, ни chain id — grep пуст).
- Второй, недостижимый рендер энкаунтеров: draw_encounter_modal требует
  GameSubStateKind::Event, который никто никогда не присваивает.
- obj.zoneRadius пишется и не читается: «убей 3 бандитов у (x,y)» на деле
  «убей 3 бандитов где угодно и когда угодно».

## II.6 РПГ-СЛОЙ: половина листа персонажа вычисляется в никуда

Суть: атрибутов сейчас 9 (не 8), из них два полностью мертвы; перки —
хранилище без семантики; экипировки нет вовсе; предметы нельзя ни надеть,
ни использовать из UI.

- **Атрибуты:** str/vit/end/wil/intl/spd — живые; **wis (expMult) и lck
  (critBase) вычисляются и читаются ТОЛЬКО текстом UI** — ни один путь XP не
  умножает на expMult, крита в бою не существует; cha наполовину (скидка
  торговли живая, relationBonus без единого читателя).
- **Скиллы:** все 8 потребляются, но 6 из 8 нарушают собственный
  задокументированный закон «1 ранг = 1 %», хардкодя 0.05f/0.1f мимо
  skill_bonus_mult. Добавление скилла = правки в 6 местах (не таблица).
- **Перки:** 24 id, 7 строк описаний, приобретение через UI итерирует
  kPerkList ⇒ 17 недостижимы; эффект имеет РОВНО ОДИН перк (Talented).
  Immortal/ShortLived — чистый текст.
- **Экипировки нет**, и UI-заглушка это сама документирует
  («Equipment data model missing», overlays.cpp:1334-1337). ItemDef не имеет
  ни слота, ни урона, ни брони; «+2 STR when equipped» у кинжала — флавор,
  который никогда не применяется. Визуальная половина СУЩЕСТВУЕТ: paperdoll
  (37 слоёв-категорий, 24 палитры) — готовый рендер экипировки без
  геймплейной половины.
- **use_item работает, протестирован — и не имеет ни одного продакшен
  вызова** (grep: items.cpp+h и тест). Инвентарь игрока в UI — read-only.
- **Инвентарь безлимитен** (vector, один стек на id); вес считается, но
  влияет только на стоимость шагов — подбор не блокирует ничего.
- **Золото — двойное понятие:** PlayerState::gold И предмет каталога
  "gold"; NPC и поселения золота не имеют — торговец не получает денег от
  сделки.
- **CharacterSheet на ECS-телах мёртв для геймплея** (4 писателя, 1
  читатель-смоук): значение запечено в Health/Combat на спавне. Это,
  впрочем, ГОТОВЫЙ носитель для будущего пер-тельного состояния экипировки.
- **Свободный полный хил** (баг, перепроверен): calculate_combat_stats
  ставит currentHp=maxHp (attributes.h:291), а кнопка «+» на атрибуте/скилле
  вызывает reset_player_combat_stats (overlays.cpp:1182-1186) — каждая
  потраченная точка лечит в полный.
- Мёртвые в живых структурах: NpcCharacter.bodyShape и tintR/G/B (0
  читателей), CombatStats.hp/mp/spRegen (сериализуются, реальный реген
  пересчитывается заново в player_recovery.cpp), ecs::Active (13 писателей,
  0 читателей), ecs::Structure — из 5 kind реально живёт один Corpse (и это
  ДРУГОЙ тип, чем sm::sub::Structure геометрии — одно имя, два смысла).

## II.7 БОЙ: урон — пять несинхронных протоколов; модели попадания три; митигации ноль

Суть: «универсальный бой» существует как лозунг: применение урона, смерть,
враждебность и досягаемость реализованы наборами частных случаев, которые
уже разошлись.

- **Урон в hp применяют 5 мест** (падение engine:175, меле игрока :1365,
  battle-strike :1981, спеллы spell_effects:113, dev-kill :2385) и ещё 3
  смоук-копии протокола смерти в main.cpp. Протоколы дрейфуют: у падения и
  dev-kill нет HitFlash; порядок «вычислить lethal / проставить компоненты /
  вычесть» — ТРИ разных; NpcDeath от спелла НЕ эмитится для тела без
  NPCKind (ранний return), от меле — эмитится с kNoNpcType; LastHit игрока
  несёт attackerId=0 — валидный entt-индекс, не null.
- **Митигации не существует:** ни armor/defense/resist/dodge (grep нулевой
  в боевом дереве), critBase вычислен и не читается. Каждый удар —
  детерминированное вычитание.
- **Враждебность — 4 несовместимых правила** (TempHostile+порог репутации;
  непрерывный stance; фракционные маски battle; «всё не-игрокское» в
  melee/targeting). Следствие: **меч игрока наводится на ближайшее
  не-игрокское тело без учёта фракции** — мирный крестьянин ближе бандита
  получает удар.
- **Меле игрока не читает body_radius** (engine.cpp:1346 — точечная
  дистанция), тогда как прицел (crosshair_stance) и снаряды читают: тролль
  и лягушка рубятся с одного расстояния, aim и hit расходятся по
  построению. Это выживший кусок бага §18.
- **sight — колонка данных, которую не заполняет ни одна строка** ни в
  fauna.cpp, ни в npc.h ⇒ у всех существ обзор 200. То же
  CombatTemplate.bodyRadius (все гуманоиды 0.55).
- **Спелл-тик — O(P·N)**: find_projectile_hit/blast/chain обходят ВСЕХ
  (view<Position,Health>) на каждый снаряд/звено, без сетки — рядом лежит
  battlePick_, построенный в тот же тик. Армагеддон на большой армии = та
  самая работа, которую battle.cpp выпиливал классом.
- **«Игрок — обычное тело» не выполняется:** 16 особых веток по PlayerTag
  (отдельная функция меле, BU_Pinned, свой мостик фракции, подавленный
  NpcDeath, исключён из loot/XP/VFX/ground-follow/flight-clamp/AI, скалярный
  playerVz_ вместо Airborne, godmode-ветки, константный casterRadius у
  снарядов игрока против настоящего у NPC). MASTER_PROMPT «no player
  special-case in the hit code» — 📄 не соответствует коду.
- **Окно 3×3 — физическая стена** трёх сортов: AI отражается от края
  (ai.cpp:23-26), battle зажимает к границе, игрок клампится. Зависимость
  поведения от положения окна = нарушение заявленной изотропии (олень
  «отскакивает от ничего»).
- **Фракционные исключения репутации — строковый if-чейн** в боевом TU
  (wildlife/demons/bandits, engine.cpp:354-364) мимо реестра факций: новая
  фракция не может объявить «за моих не штрафуют».
- sub/engine.cpp — свалка подтверждена построчно (таблица в отчёте агента):
  хэш строк, компас, блипы, спавнер №2, лут/XP, читы, профайлер — в одном TU
  с физикой.

## II.8 СКВОЗНАЯ ПЕРЕПИСЬ (дистанции, время, RNG, «держите синхронно»)

- **Дистанция вручную — 22 файла** (не 12): в macro-слое 10 сайтов
  НЕ-обёрнутых (economy ×2 — сим-значимо; ecs/systems interp;
  node_registry шаги энкаунтеров; npc_ai visualSpeed) — на торе это тихие
  швы; сабворлд рулит вручную при трёх ПРИВАТНЫХ хелперах (dist2/dist3sq в
  engine.cpp, length2d в battle.cpp), недоступных соседям; общего
  плоского dist в core/math.h нет вовсе.
- **Время:** сим-тик чист (dt = kStepSeconds); нарушение — сид мира от
  SDL_GetTicks в ТРЁХ копиях (две задедуплены в этой сессии, `97bedea`);
  std::chrono в engine.cpp — только трейс, но живёт в одной функции с
  физикой.
- **RNG — 4 разных генератора** (xorshift32; SheetRng-LCG; SinRng у мёртвого
  flag_generator; ihash01 в spawners с «почти» теми же множителями, что
  hash3) и 2 конвенции нормировки (…96 vs …95 — dispatch.cpp:238 делит на
  2^32−1).
- **«Держите синхронно» без механизма — ~30 мест** (items kNpcLoot/kNpcLootId
  без static_assert; biomes.h↔macro.frag; fauna.h↔creature_sprite.glsl;
  height.h↔литерал 1500 в трёх шейдерах; politik.h id↔kFactionDefs; плюс
  залежи «mirrors X.ts» при мёртвом TS-парити). Контрпримеры с
  static_assert есть — образец известен.
- TODO во всём src ровно два (rebuild_landmarks ×2).

## II.9 UI ВЛАДЕЕТ ЛОГИКОЙ; каноничные реализации мертвы при живых самоделках

- **step_macro_walk — интегратор позиции игрока по макромиру — живёт в
  ui/macro_overlay.cpp** (пишет gs.player.x/y, торус-обёртку) — прямое
  нарушение слоёв: main.cpp зовёт ui, чтобы продвинуть мир.
- Покупка/продажа (×4 копии тел транзакций), таверна, прокачка
  (spend point + перк с хардкодом Talented=бонус-левел) — всё коммитится из
  ImGui-обработчиков.
- **Три самодельных закона цены в UI, при этом канонические
  player_buy_price/player_sell_price (харизма+торг) в economy.cpp МЕРТВЫ
  (0 вызовов)**. То же spellbook_can_cast: главный гейт каста в main.cpp
  переписан вручную, канон без вызовов.
- draw_settlement сам зовёт sub::dispatch_generate с выдуманным контекстом
  (macroHeight=0.55 хардкод) — UI гоняет генератор мира.

## II.10 Что изменено в этой сессии (классы, коммиты, доказательства)

Ветка `audit/abc-2026-08-05`, база main@884d330. Сборка и ctest зелёные до и
после КАЖДОГО коммита; предупреждений компилятора в дереве теперь НОЛЬ.

| класс | коммит | что |
|---|---|---|
| C | d711b00 | rng: честный [0,1) + guard next_int; save v18→19; тест-первый (rng_contract_test красный→зелёный) |
| C | ef32528 | смоук console: утверждал тип вместо враждебности (класс §16) |
| A | 78fc376 | 54-веточный смоук-парсер → таблица (−52 условия по честному счёту) |
| A | 97bedea | сид нового мира: 3 копии выражения → одна функция |
| A | acd7b74 | politik: лямбда-дубль торус-метрики → вызов своей же функции |
| A | 78d2213 | ui: лямбда-дубль wrap_delta удалена |
| A | 069274f | близнецы maybe_emplace_missile_attack/carried_light → один дом (spawn.{h,cpp}) |
| A | f613314 | kHitFlashDuration: 2 файловые копии → одна в spell_effects.h |
| C | f2a34b5 | смоук-печать гнала uint32_t через %.3f (varargs-UB, единственный ворнинг) |
| B | 0890b9a | литерал 10.0f ×2 → kPlayerBaseMeleeDamage |
| DEAD | 19a6a83 | 12 символов с единственной ссылкой-определением удалены (перечень в коммите; каноны-«мертвецы», которых надо ОЖИВИТЬ, а не хоронить, оставлены сознательно) |
| tests | d89872d | 7 фикстур Position без z — ноль ворнингов |
| tools | d1e7256 | лексер в branch_density (слепое пятно 600 строк) |
| A | 3e26386 | push_unique_string/push_string: близнецы в двух events-TU → events/event_log_util.h |
| A | 045047e | блок перегруза (capacity/carried/overload/ceil) ×2 → player_overload_charge в travel.{h,cpp} |
| B | 8e61452 | зум-литералы (1.15/4/96 ×2) → константы; danger-gem: три параллельные if-цепочки → таблица стилей |
| A | b9eeed1 | заливка NpcCharacter ×3 TU → ecs::roll_npc_character(rng, tintBase); порядок 6 извлечений — один |

Числа честного инструмента: **5330 → 5249** условий; main.cpp **1473 → 1419**;
engine.cpp 446 → 443. Смоук-сюита: **18 сценариев PASS** на новом мире (+ свип
console на seed 1,7; + валидированный прогон с Vulkan-валидацией; каждая
правка сопровождалась своим прогоном затронутого сценария).

## II.17 ИТОГ СЕССИИ 2026-08-05/06: числа, состояние, что осталось

Ветка `audit/abc-2026-08-05` от main@884d330, **40 коммитов**, в main НЕ
влита (решение владельца — ревью поклассно). Сборка зелёная и **с нулём
предупреждений** до и после каждого коммита; **ctest 43 → 48** (пять новых
тестов: rng_contract, spawn_entity_event, econ_v1, trade_law, rpg_rules);
смоук-сюита 18 сценариев PASS на перебазированном мире + свип console на
сидах 1/7 + валидированный прогон с Vulkan-валидацией (только известная
teardown-утечка).

**Плотность условий, одним честным инструментом (после починки его слепого
пятна, II.3) — оба обязательных файла упали, при том что за сессию ДОБАВЛЕНО
пять систем:**

| | всего | app/main.cpp | sub/engine.cpp | ui/overlays.cpp |
|---|---|---|---|---|
| main@884d330 | 5330 | 1473 | 446 | 476 |
| ветка HEAD | **5321** | **1443** | **440** | **473** |

Сейв прошёл **v18 → v21** (v19 rng-перебазирование, v20 плотный EventTag,
v21 вид сторон торгового маршрута).

**Что осталось открытым (все — решения владельца, не работа):** железо —
модель пополнения депозита (рекомендация: конечное + открытие жил через
разведку); заморозка раскладки конверта NPC 128 Б (числа предложены, №5);
подключение экономики W2 (Stockpile + честный сбор, план в work_vector №1);
ротация ИИ-свипа под кап 16k; вынос смоук-харнесса из main.cpp (73 % файла
механически выносимо, блокер — `struct App` в анонимном неймспейсе).

## II.16b Мини-фикс интерфейса: кнопки «+» перестали ездить от числа цифр

Заявка владельца: при взятии скилла выше 9 «плюсик» сдвигается. Причина —
и в атрибутах, и в скиллах кнопка ставилась голым `SameLine()`, то есть
сразу за текстом ПЕРЕМЕННОЙ ширины: её x зависел от числа цифр (а у
атрибутов ещё и от длины подписи). Теперь кнопка стоит на ОДНОМ измеренном
x на весь блок: самая широкая подпись блока плюс поле на три цифры,
измеряется каждый кадр — значит следует масштабу шрифта бесплатно.

Вторая, менее очевидная половина: колонка «Rank» имела ЗАХАРДКОЖЕННУЮ
ширину 80 px, тогда как панель масштабирует шрифт (`SetWindowFontScale`) —
измеренное смещение и немасштабируемая колонка разошлись бы при первом же
изменении масштаба интерфейса, вытолкнув кнопку из её собственной колонки.
Ширина колонки теперь выводится из тех же измерений (поле числа + кнопка +
отступы), так что колонка, смещение и шрифт разойтись не могут.
Проверено кадром (VIT 9 против единиц, Bodybuilding 3 против нулей): все
«плюсики» в одну колонку, просвет под цифры виден, ничего не обрезано.
Двузначное значение харнессом не поставить (лист первого уровня упирает
VIT в 9, ранг в 3), поэтому этот случай держится на построении — смещение
больше вообще не читает ширину текста. (`7ff09ac`)

## II.16 Последняя чистка: тег, который никто не читал, и приказ синхронизировать пустоту

`ecs::Active` — тег с **тринадцатью** местами записи и **нулём** читателей
(ни view, ни any_of, ни get — проверено grep до и после). Тег, который
никто не читает, — не состояние, а шум, за который платит каждый спавн.
Удалён вместе со структурой. Там же: в `sub/engine.cpp` жил комментарий
«Combat hit radius… Twin of the copy in spell_effects.cpp; keep the two in
lockstep» — описание функции, ПЕРЕЕХАВШЕЙ в `sub/body.h`, и стоял он в
одиннадцати строках над комментарием, который об этом переезде и сообщает.
То есть файл содержал живой приказ синхронизировать то, чего нет — ровно
тот класс, ради истребления которого body.h и написан (§18). Удалён.
(`8ec6f6e`)

## II.14 Разведка gigahrush/gigahrush2: как сосед решил тор «без циклов» (справка для №1/№7)

Суть: у владельца в gigahrush2 (C++, 128³ 3-тор) готова эталонная связка
для торовой навигации и полей; «без циклов» значит ТРИ вещи сразу: нет
поискового цикла в рантайме (всё запечено), нет циклов в спуске (BFS
parent-chain монотонно убывает), и — главный анти-урок из старого
gigahrush — **никогда не строить остовное дерево над тором** (оно режет
циклы тора и создаёт невидимые швы: там это давало крюки в 240+ шагов).

Что стоит портировать в timaert, когда дойдём до караванов/армий/полей:
1. **wrap.h (5 функций)** — канонический набор: wrapi/wrapf (есть у нас),
   `wrap_delta` (int, кратчайшая знаковая дельта), `wrap_delta_f`
   (БЕЗВЕТОЧНАЯ: `d - period*floor(d/period + 0.5)` — floor(x+0.5), НЕ
   round: у GLSL round неопределён на половине периода, CPU и шейдер
   разошлись бы), `nearest_image` (минимальный образ точки у камеры).
2. **Каждый аксессор сетки оборачивает ВНУТРИ** — тогда ни одному
   потребителю не нужна ветка шва.
3. **Запекание = per-source BFS** с wrap-модулем на соседях; сентинель
   0xFF = и «стена», и «не посещено» (один массив, одна очистка);
   хранить НАПРАВЛЕНИЕ родителя (обратное = d^1), не дистанцию.
4. **Циклическая решётка узлов** (дим делит дим мира, Вороной-полосы) +
   мульти-сорс BFS «ближайший узел» = вход в маршрут из любой клетки;
   Floyd–Warshall по 64 узлам; O(1) запрос шага.
5. **Скалярные поля (диффузия опасности/запаха)**: шовные условия
   ВЫНЕСЕНЫ из внутреннего цикла на уровень z-плоскости/y-строки как
   знаковые смещения — внутренний цикл без модуля и без ветки; плюс
   hot-group skip (нулевые группы пропускаются с доказательством) 15×.
6. **Рендер: оборачивается ЯКОРЬ инстанса, вершины никогда** (пер-вершинный
   wrap рвал длинные меши у полупериода) — у нас это уже частично закон
   (§15.2 «ключи к абсолютным координатам»), формулировка соседа жёстче.
7. **wrap_delta — только эвристика упорядочивания, никогда метрика пути**
   (на антиподе «кратчайшая стрелка» врёт); истину несут запечённые поля.
8. **Кэш запекания на диск** ключом от входов генерации (наш принцип
   «клетка — чистая функция контекста» дословно).
Караванной геометрии там нет (economy абстрактна в обоих) — наш №1 будет
первым; паттерн стоимости брать с acoustic distance: цепочка регионов +
сумма торо-манхэттенов через порталы.

## II.15 Три бага торговой сети починены (тест-первый, `e6e87e6`, save v21)

Суть: все три доказанных дефекта II.4 закрыты. (1) Дистанция торговли
теперь ТОРОВАЯ (и цена, и дни пути) — красный прогон доказан вживую:
на плоской математике тест выбирал дальнего соседа по эту сторону шва
(`chose destId=1`), на торовой — ближнего через шов. (2) Маршрут несёт
ВИД сторон (originIsVillage/destIsVillage, штампуется при создании),
resolve_route_parties диспатчит по виду — выручка деревни больше не
уходит одноимённому городу; сериализуется ⇒ v21. (3) Гарнизон перестал
расти бесконечно: kMaxGarrisonPerSettlement=64 (po2), static_assert
далеко под сейв-гардом 8192. trade_law_test красный→зелёный, ctest 47/47,
ноль ворнингов, смоуки PASS.

## II.12 Правило владельца: события — только из контекста; случайный триггер энкаунтеров удалён

Суть: владелец зафиксировал принцип — **шина и события обязаны исходить из
игрового контекста и состояния; безусловные случайные события из списка —
пережиток и болезнь**. Узел `enc_random` (каждые 15 клеток пути — бросок
1–12 % из СОБСТВЕННОГО мир-независимого RNG с хардкод-сидом, при попадании —
РАВНОМЕРНО случайная строка таблицы энкаунтеров, вслепую к биому, зоне
опасности, территории фракции, времени суток и состоянию игрока) удалён
целиком (`ffdd978`) вместе со своим тестом, который доказывал лишь «кубик
рано или поздно выпадает». СОХРАНЕНЫ: таблица энкаунтеров (авторский
L4-контент) и весь путь ShowDialog-с-выборами → effect_applicator — это
презентационная половина, которую будет вести будущий контекстный триггер.
Урок: логик-нода была правильным МЕСТОМ для триггера; болезнью было
СОДЕРЖАНИЕ предиката (чистый rand вместо контекста).

## II.13 Разбор шины: что она такое, что позволяет, что нет, и альтернативы

Суть: шина — пригодный канал для РЕДКИХ ФАКТОВ и презентации, узаконенный
буфер с журналом; её настоящие болезни — нетипизированная нагрузка и тихая
пустота; для контекстных событий нужен слой правил НАД ней, а не замена ЕЁ.

**Что она есть (факт кода).** `GameEvent` — плоское «всё-в-одном» (tag,
a/b, fx/fy, ix/iy, s1/s2-строки, два shared_ptr для диалогов/историй).
`EventBus` — tick-буфер → last → кольцо истории; подписки существуют, но в
проде подписчиков ноль (потребление опросом, порядок потребителей выписан
руками в process_world_events и потому детерминирован).

**Что ПОЗВОЛЯЕТ (и это стоит беречь):**
- слабую связность: quest engine не знает про UI, продюсер не знает
  потребителей;
- **события как ДАННЫЕ контента** — onAccept квеста это список GameEvent,
  он сериализуется в сейв; «эффект = список событий» это и есть механизм,
  которым контент дёргает мир без кода;
- журнал даром (query_history) — для кодекса/дебага/будущей «хроники мира»;
- детерминизм: один поток, явный порядок, события уходят в историю с
  tick/day/hour.

**Чего НЕ позволяет / болезни (по убыванию цены):**
1. **Нетипизированная нагрузка.** Смысл полей a/ix/s1 меняется от тега к
   тегу, контракт живёт в комментарии enum'а. Уже стреляло: DestroyNpc
   сравнивал entity-id с типом; понадобился костыль kNoNpcType=-1.
   Компилятор перепутанные поля не ловит.
2. **Тихая пустота.** Событие без потребителя не ошибка, а молчание —
   SpawnEntity жил так с рождения. Лечится только тестом-инвариантом
   «у каждого тега есть продюсер И потребитель» (в векторе).
3. **Два времени жизни.** applier/main читают tick_events (тот же кадр),
   logic/quests — last_tick_events (после flush, кадром позже). Уже ловили
   родню этого в смоуках (§16.1). Неочевидно и нигде не написано — теперь
   написано в заголовке шины, но остаётся ловушкой.
4. **Строки и shared_ptr в каждом событии** — аллокации; для UI-темпа
   приемлемо, как канал МАССОВОЙ симуляции шина непригодна в принципе
   (и не должна пытаться — тысячам тел место в ECS/агрегатах, №1 вектора).
5. **Нет адресации/области**: событие глобально, «события этой клетки»
   не выразимы — потребитель фильтрует всё сам.

**Альтернативы (выбор владельца):**
- **A. Узаконенный буфер (текущее, ядро уже сделано)** + два дополнения:
  тест матрицы продюсер/потребитель и контракт полей per-tag как таблица
  (не комментарий). Дёшево; болезни 1 и 5 остаются, но придавлены.
- **B. Типизированные события** (tag + POD-union / std::variant пер-тег):
  компилятор ловит поля, сериализация честная POD. Цена: переписать все
  ~40 мест продюсеров/потребителей. Средний объём; главный выигрыш —
  болезнь 1 умирает классом.
- **C. События как ECS-сущности** (событие = entity с компонентами,
  потребитель = система-view): адресация/область даром (компонент-ссылка
  на клетку/поселение), масштабируется, идеально ложится в EnTT-архитектуру.
  Цена: история и сериализация требуют отдельного пути; это скорее
  инфраструктура для симуляционных фактов №1 (макро-модель), чем для UI.
- **D. Снести шину, звать напрямую** — НЕ рекомендую: потребителей
  сегодня четыре и вызовы просты, но погибает «событие как данные
  контента» (onAccept в сейвах), а это несущая способность.
- **E. Контекстные правила как ДАННЫЕ поверх шины** — не альтернатива, а
  недостающий слой и прямой ответ на правило владельца: таблица строк
  «условие(контекст) → события», где контекст — то, что УЖЕ есть в
  GameState (зона опасности, биом, территория фракции по cellOwner, время
  суток, репутация, состояние игрока), а рандом допустим только как ВЕС
  внутри отфильтрованного контекстом набора — никогда как триггер.
  Строка таблицы = встреча/происшествие. Это же — субстрат «генератора
  историй». Хилый LogicNodeEngine (2 ноды, string-id, shared_ptr-стейт)
  либо вырастает в исполнителя этой таблицы, либо заменяется ею.

**Рекомендация (решать владельцу):** A + E как ближайший шаг (буфер для
фактов/презентации, таблица контекстных правил для возникновения событий),
B — когда таблица правил устоится и станет ясно, какие payload'ы настоящие;
C — держать в голове для №1, не тащить в UI-шину.

## II.11 Шина узаконена (вариант A вектора №3, одобрен владельцем): буфер — честный, SpawnEntity — жив

Суть: владелец выбрал «узаконить буфер»; выполнено двумя коммитами, и
kill-контракты ВПЕРВЫЕ спавнят свои цели.

- `16999f2` (DEAD, save v19→20): удалены **16 тегов**, на которые не
  ссылалось ничто вне enum (перечень в коммите), enum стал плотным и
  неявным, TS-числовые якоря и их static_assert'ы умерли вместе с парити
  (значения тегов сериализуются в onAccept квестов ⇒ бамп сейва). Удалены
  **5 методов шины с нулём продакшен-вызовов** (has_subscribers, has_tag,
  find, find_all, trim_history); заголовок шины теперь прямо говорит:
  потребление — ОПРОС tick_events()/last_tick_events(). Тесты переведены
  на опросный контракт (локальные has_tag/find_tag-сканы); батарея
  сериализации сохранила покрытие всех полей payload на живых тегах.
- `03e7205` (feat): потребитель SpawnEntity. `npc_type_from_label`
  (macro/npc.h) — регистро-данный лукап токена без if-цепочки;
  `spawn_npc_at` (npc_spawn) — один враждебный макро-NPC у названной
  клетки: агрессивные типы → фракция bandits (паритет с boot-пулом),
  мирные → фракция ЗЕМЛИ (то же «земля решает», что в сабворлде); ординал
  MacroSpawnId продолжается за максимум (идентичность possession цела);
  make_npc получил levelOverride с нетронутым RNG-потоком загрузки.
  Консюмер в main.cpp — тот же курсорный паттерн, что у BattleStart, на
  обоих местах вызова. Продюсер kill-N теперь кладёт N событий (копии,
  поток RNG генератора не сдвинут). Пришедший в клетку игрок воплощает
  тело обычной макро-проекцией (Inc 5d), его смерть кормит DestroyNpc
  обычным NpcDeath.
- Доказательства: новый `spawn_entity_event_test` (отказ по неизвестному
  токену / тип / фракция / уровень / разброс / ординал), **ctest 45/45**,
  ноль ворнингов, три смоука + валидированный прогон PASS.

**Что тесты НЕ покрывают из изменённого:** таблица токенов смоука прогнана
только через сами смоуки (парсер тестируется использованием); дедуп сида
кастомной новой игры (нет смоука кастом-меню — проверено сборкой и чтением);
kHitFlashDuration/kPlayerBaseMeleeDamage — численно идентичны, отдельного
теста нет; удалённые мёртвые символы по определению не тестировались.



Severity tags:

- **🔴 BLOCKER** — ships broken / crashes / data-loss / core promise absent.
- **🟠 MAJOR** — wrong behaviour, real bug, or a shippability defect a player sees.
- **🟡 MINOR** — smell, hardcode, small bug, maintainability risk.
- **📄 DOC-DRIFT** — documentation contradicts code. (These I am authorised to fix.)
- **🎮 DESIGN** — game-design / product critique (is this fun & saleable?).

Each finding carries `file:line` + a concrete failure scenario or evidence.
Nothing here is speculation unless explicitly tagged **(UNVERIFIED)**.

