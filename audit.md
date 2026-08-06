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
> **Status:** LIVING DOCUMENT. Re-audited periodically as code changes.
> Last full pass: **2026-07-29** (Pass 4 — re-audit after the tree moved again:
> commits `68bc669` seasons / `a6c9eb7` point-light SSBO / `9e5b666` faction
> hostility landed, plus in-flight uncommitted code edits by the owner. Ground
> truth re-verified first-hand: `src/` fully GL/WASM-free; **28/28 ctest green**
> from a clean reconfigure — `seasons_test` is the new 28th, and
> `character_paperdoll_gl_smoke_test` was **removed** (only a stale build/
> artifact lingers). All numeric drift re-reconciled in README + here).

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

## III.9 🔴 GPU: UI-текстура уничтожается ровно тогда, когда её читает кадр в полёте

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

## III.14 🟠 РЕНДЕР: ресурсы уничтожаются и переписываются внутри открытого кадра

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
> раздел, первой строкой суть. Старый аудит (Часть I, ниже) не переписывается —
> его протухшие находки помечаются в соответствующем разделе здесь.

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

---

## 1. Executive summary (first pass)

The project is **substantially more real than a prototype** and **substantially
less finished than the docs claim**. Two truths held at once:

1. **The engine is genuinely built.** A live Vulkan frame (seed 12345, captured
   2026-07-29 during this audit) shows a coherent macro-world — mountain
   massifs, forests, a winding river, MST roads, biome variety — **and an
   authored 9-panel intro story** ("A thousand years ago, a man broke the laws
   of the gods… He built all of this"). This is not vaporware. 373 Vulkan call
   sites, 0 OpenGL calls: the GL→Vulkan migration the docs call "in progress" is
   **effectively DONE**.

2. **The documentation is badly out of sync with the code**, in ways that would
   mislead any new contributor (human or agent) on day one. The single largest
   theme of this audit's first pass is **doc-drift** (§2), and per the owner's
   directive I will correct it directly.

**Headline findings (first pass, expanded below):**

- 🔴🔒 **SECURITY — a live-looking API auth token is committed to the repo.**
  `paperdolfix.txt` (repo root, **git-tracked**, introduced in commit `12f6ca5`)
  contains an `ANTHROPIC_AUTH_TOKEN=…` assignment — it is a raw terminal-session
  dump that was checked in. Deleting the file is **not** sufficient: the secret
  lives in git history and must be treated as compromised → **rotate/revoke the
  token now**, then purge the file. This is the single most urgent item in the
  audit and is a *code/ops* action for the humans (I only flag it; I will not
  touch git history). (§4.3)
- 🔴 **CI is broken and ships the wrong, dropped target.**
  `.github/workflows/deploy.yml` runs on every push to `main` and does an
  **Emscripten → Cloudflare Pages** browser deploy — the exact target the docs
  say is *dropped*. It copies `samosbor.{html,js,wasm,data}` while CMake emits
  `timaert.*` (`project(timaert)`), so the job **cannot succeed**; it also
  resurrects the abandoned WASM path in automation. Fix or delete. (§4.4)
- 🔴/📄 **The whole "OpenGL is the current baseline, Vulkan is the forward
  target" framing is FALSE.** `src/gl/` does not exist; there are **zero** `gl*`
  API calls in `src/`; ARCHITECTURE.md's title is still "C++ / **OpenGL** / EnTT
  port." The backend migration is complete but every top-level doc describes it
  as pending. (§2.1)
- 📄 **The "browser / WASM / Emscripten" target was described as live in README**
  (full build section + dependency list) but has **0 references in `src/`** and
  is officially dropped. CMake still carries **14 `if(NOT EMSCRIPTEN)` + 1
  `if(EMSCRIPTEN)`** guard blocks (exact count, re-verified 2026-07-29). *(README
  side CORRECTED this pass — WASM build block replaced with a "no browser build"
  note; CMake cleanup remains a flagged code change.)* (§2.2)
- 🟠 **HUD regression, seen with my own eyes** in the current build: the
  top-right status cluster overlaps into unreadable garbage
  (`Gold 1000Traveller 7Lv 1Pos 0 1221,10014EXP`). The old (OpenGL-era) frame
  had these fields cleanly `|`-separated. A player sees this in the first second.
  (§4.1)
- 🟡 **`src/app/main.cpp` is 7551 lines** — 7.5× the project's own stated hard
  limit ("never let one exceed 1000 unless a naturally encapsulated module").
  This is the top maintainability risk for a shipping product. (§5)
- 📄 **AGENTS.md prescribes a recon arsenal (`rg`, `fd`, `sg`/ast-grep, `jq`,
  `tokei`) that is only half-installed** — `fd`, `sg`, `tokei`, `cloc` are
  absent on the dev machine; only `rg` and `jq` exist. "Use tokei" is a dead
  instruction. (§2.4)
- 🔴🎮 **CONFIRMED: the core architectural promise — "thousands of NPCs
  simulated on the GPU via compute shaders" — DOES NOT EXIST IN CODE.** This is
  the single most important finding of the audit. Verified independently twice
  (my own trace + a dedicated GPU sweep): **0 `.comp` files, 0 `vkCmdDispatch`,
  0 compute pipelines, 0 SSBO/`std430` storage buffers** anywhere in `src/`. The
  mass of NPCs runs on the **CPU** via a *time-sliced budgeted tick*
  (`tick_macro_npc_ai_budgeted`, `src/macro/npc_ai.cpp:555`) that carries a
  `result.backlog` flag which trips when the CPU cannot keep up — i.e. it
  **LOD-skips**, the exact thing AGENTS.md's "GPU-Driven Simulation, no cheats"
  Hard Rule forbids ("NPCs are never frozen, faked, or LOD-skipped"). The
  subworld renderer is hard-capped at **512** visible NPCs + 512 creatures
  (`src/sub/vk_renderer_3d.cpp:280,286,700,739`), silently dropping the rest.
  **The M&B "command an army of thousands" fantasy — the stated *reason for the
  entire Vulkan migration* — is currently unbuilt.** See §6 for full evidence.
  (In fairness: `vulkan.md` is HONEST about this — it lists compute as "P7 …
  pending." It is `ARCHITECTURE.md` that describes it in the present tense as if
  shipped — that doc I will correct.)

- 🟠 **Correctness bug, player-facing: stamina debt compounds into HP.**
  `src/macro/travel.cpp:70-72` (and copy-pasted at `src/app/main.cpp:1613-1615`)
  bleeds negative SP into HP but **never floors SP back to 0**, so the same
  negative pool is re-subtracted from HP on *every* subsequent step —
  accelerating damage. A player crossing mountains on empty stamina dies far
  faster than the per-cell cost implies. CONFIRMED by reading both sites. (§7)
- 🔴🎮 **A signature POI type — spires — can never spawn in a fresh world.**
  Spires are fully plumbed for save/load/render/night-glow/zone-gating, but
  **no generator ever writes one**: the only writes to `gs.spires` are the
  save-loader (`save.cpp:1043`) and a `std::move` of an always-empty `fresh.spires`
  (`main.cpp:1431`). CONFIRMED by exhaustive grep. The "Might & Magic microworld"
  whose arcane landmark never appears ships an empty promise. (§7)
- 🔴🎮 **Subworld combat is raw `hp -= damage` — no armor, defense, mitigation,
  crit, dodge, or variance.** All three strike sites (`engine.cpp:1121,1423`,
  `spell_effects.cpp:125`) are literal subtraction. `critBase` is *computed*
  (`attributes.h:273`) and **never read**; there is no `armor`/`defense`/`resist`/
  `dodge` symbol anywhere in the combat tree (CONFIRMED by grep). Every hit is
  deterministic and identical — no defensive build matters, luck does nothing.
  For a Daggerfall/M&M target this is the biggest "tech demo, not an ARPG" gap. (§9)
- 🟠🎮 **The macro map is static scaffolding, not a living M&B world.** Faction
  relations are sampled once at boot and **never updated** by the daily tick; no
  parties, lords, armies, sieges, or shifting borders exist (`world_tick.cpp`
  touches only economy/mood/garrison). The economy *does* tick, but its outputs
  feed nothing strategic. (§7). *(Correction to the macro sweep, which claimed
  relations are read by nothing: they ARE read by the subworld hostility resolver,
  `engine.cpp:229` — but no **macro-strategic** system consumes them.)*
- 🟠 **The player walks through everything in the subworld.** `move_player`
  (`engine.cpp:1676-1704`) clamps only to world bounds; it never consults the
  `trav` grid, walls, water, or structure radii the generators fill. Walls and
  houses are pure decoration — a fundamental feel bug for an explorable world. (§8)
- 🟠 **A GL-era call survives on the Vulkan window.** `main.cpp:1876` calls
  `SDL_GL_GetDrawableSize` (the other two resize sites correctly use
  `SDL_Vulkan_GetDrawableSize`). On a `SDL_WINDOW_VULKAN` window this queries a
  GL drawable that doesn't exist → likely stale/zero size on that resize path.
  CONFIRMED. (§11)
- 🔴🎮 **The content layer barely exists, and 2 of 7 quest types are broken
  end-to-end.** CONFIRMED: `SpawnEntity` (the event that spawns quest enemies) has
  **two producers and zero consumers** (`procedural.cpp:245,286`), so "kill/defend"
  quests spawn nothing; the `DestroyNpc` objective compares an **entity-id to a
  type ordinal** (`quest_engine.cpp:126`) and completes off unrelated kills — a bug
  a green test enshrines. The post-intro **plot is a single `return false` node**
  (`chapter_1.h:14`): no main story, no dialog, no authored side quests. Census:
  8 spells (2 no-op), 7 quest templates (5 work), 15 encounters, 1 intro, 0 plot.
  This is the L4 counterpart to the empty macro strategic layer (§7.7). (§10)
- 🟡 **RNG contract hole is real and now root-caused.** `core/rng.h:23`
  `next_f01()` divides by `2^32`, so `float(0xFFFFFFFF)` rounds to exactly
  `1.0f` — the "[0,1)" comment is violated. Traced downstream: an off-by-one
  over-band in faction relations (`state.cpp:43`), loot/inventory qty one over
  `max` (`items.cpp:233`), one creature over the per-cell cap (`fauna.cpp:135`).
  All **bounded** (no OOB crash survives in `src/sub`), but it is a genuine
  contract violation with three inconsistent normalization conventions across the
  codebase. Separately, `next_int(lo,hi)` is `% (hi-lo)` with **no guard** →
  modulo-by-zero UB reachable via `procedural.cpp:180` if a quest's candidate set
  is empty. (§7/§8/§9)

Detailed sections follow. Subsystem sections (§7–§11) are populated from five
parallel read-only sweeps (macro, sub, gpu, ecs/combat, ui/app) plus a
docs-vs-code census; every CONFIRMED finding below was re-verified first-hand
before being promoted here. One sweep (events/content/quests/spells) is still
running and will land in §10.

---

## 2. Documentation drift (authorised to fix)

This is the densest problem area. The docs were written incrementally and the
top-level ones never caught up to the Vulkan cutover or the WASM drop.

### 2.1 🔴📄 "OpenGL is current / Vulkan is forward" — FALSE; migration is done

**Evidence (code):**
- `src/gl/` directory **does not exist** (documented as a layer in README,
  AGENTS.md Layer Discipline, ARCHITECTURE.md).
- `rg '\bgl[A-Z]\w+\('` over `src/` → **0 matches**. `rg '\bvk[A-Z]\w+\('` →
  **373 matches**. No file in `src/` includes `<GL/…>`, `glad`, `SDL_opengl`.
- `CMakeLists.txt:135` `find_package(Vulkan REQUIRED)`; `:217` compiles
  GLSL→SPIR-V with `glslc`. The binary `build/timaert` links and runs on
  MoltenVK (verified: live frame captured this pass).

**Evidence (docs still claiming OpenGL is the baseline):**
- `ARCHITECTURE.md:1` — title: *"Architecture — Timaert (C++ / **OpenGL** / EnTT
  port)"*.
- `ARCHITECTURE.md:52` — *"gl/  Thin GL wrappers…"* (dir doesn't exist).
- `ARCHITECTURE.md:217` — layer table maps a TS module to `src/gl/`.
- `AGENTS.md:224-226` — *"the commands below build the **current** OpenGL
  baseline. The forward target is Vulkan."*
- `AGENTS.md:269-272` — Layer Discipline diagram lists `gl/` as an includable
  layer in all four rows.
- `README.md` — "legacy OpenGL 3.2 Core", OpenGL-as-a-**dependency**, and
  `gl/ OpenGL helpers` in the project-layout block. ✅ **CORRECTED this pass**
  (see Fix status).

**Impact:** A new contributor is told to target a backend that's gone, avoid one
that's already shipped, and include from a directory that doesn't exist.

**Fix status:** ✅ **APPLIED (README).** README now states the migration is
*complete in `src/`* (0 GL calls, no `src/gl/`, backend in `src/gpu/`), the
Project Layout block maps `gpu/ → Vulkan backend` (was `gl/ OpenGL helpers`), and
the `app/` line reads "SDL2 (Vulkan window)". Still **PENDING (next docs)**:
AGENTS.md Layer-Discipline `gl/` rows + "current OpenGL baseline" Build note, and
ARCHITECTURE.md title/`gl/`-layer/nonexistent-source paths.
**Code-side survivors — FLAG-ONLY (cannot edit code):** 4 stale OpenGL mentions
remain in source and are the humans' to fix — `main.cpp:1` file banner,
`screens.cpp:82` title-screen version string (`"v0.1 — C++ / OpenGL / EnTT
port"`), `vk_device.h:2` and `map_generator.cpp:18` migration comments. Re-verified
first-hand 2026-07-29: `rg -i opengl src/` → exactly these 4; `rg '\bgl[A-Z]\w+\('
src/` → 0 real GL API calls.

### 2.2 📄 WASM / Emscripten "browser target" — described as live, actually dropped

- `src/` → **0** `EMSCRIPTEN`/`emscripten` references.
- `README.md:168-176` — full *"WebAssembly (Emscripten)"* build section with
  `emcmake` commands and a localhost URL.
- `README.md:188` — lists *"WebGL2 / GLES3 on Emscripten"* as a **dependency**.
- Yet `README.md:11` and `AGENTS.md:161,225,250` say the browser target is
  "dropped" / "being retired." So README **both** drops it and documents how to
  build it.
- `CMakeLists.txt` — **exactly 14 `if(NOT EMSCRIPTEN)` + 1 `if(EMSCRIPTEN)`**
  guard blocks remain (re-counted first-hand 2026-07-29; my Pass-1 "18" was
  WRONG, the doc-census "14" was right — correction logged in §13.1). Dead
  scaffolding for a dead target.

**Fix status:** ✅ **APPLIED (README).** The WASM build section was replaced with
an explicit "No browser build" note, and the WebGL2/GLES3/Emscripten dependency
lines were already removed in a prior pass. The README no longer both drops and
documents the browser target. **CMake cleanup (the 15 guard blocks) is a *code*
change → flagged for humans, not performed.**

### 2.3 📄 Stale front-matter dates / counts

- `README.md` Integration Ledger — was *"Updated 2026-05-15"* while the code is
  dated 2026-07-29. ✅ **CORRECTED**: header now separates the 2026-05-15
  Windows/MSVC build evidence from the **28/28-green macOS ctest re-verification
  of 2026-07-29** (run first-hand from a clean reconfigure).
- Save version: **confirmed `kSaveVersion = 10`** (`src/macro/state.h:20`) — this
  matches README's v10 claims. ✅
- CTest target count: **the tree moved TWICE.** Pass 1 saw 26; Pass 3 saw 27
  (`faction_relations_test` added); **Pass 4 (this pass) sees 28** — a new
  `seasons_test` (`tests/seasons_test.cpp`, dated today 05:52, committed
  `68bc669`) is now the 28th `add_executable` **and** the 28th entry in the
  `enable_testing()/foreach` block. Verified first-hand from a **clean
  reconfigure**: `ctest -N` → *Total Tests: 28*; `ctest --output-on-failure -j 8`
  → **28/28 passed, 0 failed** (2026-07-29). **Correction to my own prior finding:**
  the "28th binary is the unregistered `character_paperdoll_gl_smoke_test`" claim
  is now WRONG — that target + its source were **removed** (commits `a63ed38` /
  `94ffadb`); only a stale binary artifact survives in `build/` from before the
  reconfigure. README said "27/27" in nine places — ✅ **ALL CORRECTED this pass**
  to 28/28 (+ `seasons_test` added to the list, `gl_smoke_test` reference deleted,
  `river_generation_test` "pending from-scratch ctest" caveat resolved — it ran
  green this pass). My own Pass-1 "26" and Pass-3 "27" are superseded — logged in
  §13.1.

### 2.4 📄 AGENTS.md recon arsenal half-fictional

`AGENTS.md:114-120` §8 mandated `rg`, `fd`, `sg` (ast-grep), `jq`, and §11
mandated `tokei`. Re-verified on the dev machine (2026-07-29,
`for t in rg fd sg ast-grep jq tokei cloc`): `rg` ✅, `jq` ✅, but **`fd` ✗,
`sg`/ast-grep ✗, `tokei` ✗, `cloc` ✗**. The instruction *"Use tokei to audit
codebase size"* (§11) could not be followed. ✅ **CORRECTED this pass** (authorised
doc-fix): §8 now marks `rg`+`jq` PRESENT and `fd`/`sg`/`tokei`/`cloc` ABSENT with
`rg`/`find`/`wc -l` fallbacks; §11 drops the `tokei` mandate for the same
fallbacks. No tool was installed (that would be a machine change, out of scope) —
the docs were brought in line with reality instead.

### 2.6 🔴📄 ARCHITECTURE.md present-tenses the unbuilt GPU crowd sim (highest-impact doc lie)

`ARCHITECTURE.md:82-83,117-139` describe "thousands of NPCs on compute shaders
over SSBOs" and the embodiment/de-embodiment seam **declaratively, as shipped
capability** — while `vulkan.md:106` honestly marks the same thing "P7 pending"
and the code has none of it (§6.0). Any reader — contributor or a Steam-page
writer cribbing from the architecture doc — would believe the defining feature
exists. **Fix: re-tense ARCHITECTURE.md's GPU-residency section to match the
honest `vulkan.md` (planned/P7), and add a one-line "Status: NOT YET IMPLEMENTED"
banner to that section.** This is the top doc-correction priority alongside §2.1.

### 2.7 📄 Cross-cutting numeric contradictions (docs disagree with code AND each other)

The doc-census sweep + my spot-checks found the same fact stated with **different
values in different docs**. Code is ground truth; the disagreeing docs are listed.

| Fact | Code truth (re-verified 2026-07-29) | Docs that AGREE | Docs still WRONG |
|---|---|---|---|
| Save version | `kSaveVersion = 10` (`state.h:20`) | README, ARCHITECTURE:1298, MASTER_PROMPT, possession.md | **ARCHITECTURE:731 (v9)**, **translation.md:101/108/166/189 (v8)** |
| Biome count | **11** (`biomes.h`: `Water = 9`, `Mountain = 10`) | README:52, design.md:117 | **MASTER_PROMPT:155 (10)**, **ARCHITECTURE:1018 (10)** |
| CTest targets | **28** (`ctest -N` → 28; `ctest --output-on-failure` → 28/28 green; 28 `*_test` binaries, all registered) | README (✅ all nine sites CORRECTED this pass) | **MASTER_PROMPT:740 (25/25)** |

These are all authorised doc-fixes. **README's CTest count is now fully
reconciled (28/28).** Remaining: the save-version and biome-count contradictions in
ARCHITECTURE / MASTER_PROMPT / translation.md — the most dangerous because
ARCHITECTURE is *internally* contradictory (both v9 at :731 and v10 at :1298).
Those live in the next docs to correct (§13 Next).

### 2.8 📄 `translation.md` is UTF-8-corrupted (186 lines of mojibake)

`translation.md` is the only doc with a text-encoding defect: **186 lines**
contain mojibake — every `—` renders `â€"`, every `×` renders `Ã—`, every status
glyph `✅/🟨/⛔` renders `âœ…/ðŸŸ¨/…` (CONFIRMED: `rg -c 'â€|Ã—|âœ|ðŸ'` → 186). The
file also carries stale `kSaveVersion=8` claims (§2.7) and a "C++ / OpenGL / EnTT"
title. It reads as a *historical* TS→C++ port ledger, so the right fix is: re-encode
to clean UTF-8, correct the version/title lines, and stamp it "historical — do not
trust version numbers." (Large edit; will do after the live sweeps are integrated.)

### 2.9 📄 Docs reference source files that do not exist

The census found doc links/paths to files that were renamed or deleted in the
Vulkan cutover (all CONFIRMED absent):
- `src/gl/**` — entire directory gone (README, AGENTS ×4, ARCHITECTURE ×6).
- `macro/macro_renderer.{cpp,h}` → real file is `vk_macro_renderer.*`
  (ARCHITECTURE:205,212; translation.md).
- `sub/renderer_3d.{cpp,h}` → real file is `vk_renderer_3d.*`
  (ARCHITECTURE:229,736; microworld.md:11-12; camera.h:23; base_generator.h:17).
- `sub/renderer_2d.{cpp,h}` — **no 2D subworld renderer exists at all**;
  `rg renderer_2d src/` is empty, yet ARCHITECTURE:734-735 + microworld.md:3,27
  advertise a "2D top-down (default)" subworld view. `engine.h:2-4` itself says the
  subworld is *always* first-person 3D and the flat view is the macro map. Direct
  contradiction.
- `sub/sky.{cpp,h}` — sky lives inside `vk_renderer_3d`; ARCHITECTURE:1051 cites
  `sub/sky.cpp`.
- `sub/textures.{cpp,h}` — deleted (AGENTS.md:63 itself records the deletion);
  ARCHITECTURE:739 still links it.

### 2.5 📄 Legacy TS-prototype-era docs still in repo root (candidate for archive)

Large stale files that predate the C++ port or the current architecture and now
risk misleading readers (assessment being refined by the doc sweep):
`matwej.md` (59 KB), `MERGE_PLAN.md`, `TIMAERT BATCH.md`,
`TIMAERT_MASTER_CHANGESET_AND_COMMIT_MANIFEST_2026-05-15.md`,
`TIMAERT_START_HERE_MAIN_DOCUMENTS_2026-05-15.md`, `paperdolfix.txt`,
`problems.md`, `debug.md`. Total doc payload at root is very large
(ARCHITECTURE 90 KB + MASTER_PROMPT 72 KB + translation 64 KB + design 45 KB +
matwej 59 KB) — heavy duplication likely. [PENDING doc sweep for dedup map.]

---

## 3. Confirmed positives (what's genuinely good)

Being T.A.R.S.-honest cuts both ways — these are real:

- **Vulkan backend works end-to-end** on MoltenVK; headless frame capture
  (`TIMAERT_SHOT_PATH` + `capture_frame`) works exactly as `render.md` documents.
- **Macro world looks like a real map** (live frame this pass): massif mountains,
  forests, a meandering river, roads, distinct biomes. Not programmer-art noise.
- **There is authored narrative content** — a 9-panel illustrated intro story
  with real prose. That's more than most engine-first projects have.
- **Save system at v10** with a version gate and a passing `save_roundtrip_test`.
- **28 logic tests wired into ctest** (previously build-only / vacuous), verified
  **28/28 green** first-hand this pass from a clean reconfigure — a real
  regression net that keeps growing (26 → 27 → 28 across the three passes).
- **Data-driven spine exists**: one monster table, one loot registry, universal
  combat curve, player-as-a-flag possession — all confirmed present in docs and
  (pending sweep) in code.

---

## 4. Shippability defects seen in live frames

### 4.1 🟠 HUD status bar overlaps into unreadable garbage (top-right)

**Evidence:** `build/audit_macro.png` (Vulkan, seed 12345, this pass). Top bar
reads `… Gold 1000Traveller 7Lv 1Pos 0 1221,10014EXP` — Gold value, class name
("Traveller"), Items count, Level, Pos, and EXP all render on top of each other
with no separators. The May-2026 OpenGL frame (`build/smoke_04_ui_from_buildcwd.png`)
shows the same fields cleanly separated: `Gold 1000 | Items 7 | Pos 794,883 …
Traveller Lv 1  0 / 1100 EXP`. So this is a **regression** in the HUD layout,
almost certainly tied to the Vulkan UI text path or a window-width assumption.

**ROOT CAUSE (confirmed by reading the code):** `src/ui/screens.cpp:347-426`
`draw_player_hud()` lays the top strip out as a single ImGui row. The left half
flows left-to-right with `SameLine()` (Time | HP/MP/SP bars | Gold | Items |
Pos), and then the name/level block is **absolutely** positioned at
`screens.cpp:421` `ImGui::SameLine(vp.x - nw - 14.0f)` — i.e. pinned to the
right edge at `windowWidth − textWidth`. There is **no gap check** between where
the left cluster ends and where the right block begins. On a wide-but-dense
layout (retina, long class name, 5-digit gold, 4-digit EXP) the left cluster's
right edge passes `vp.x − nw − 14`, so the right block is drawn *back over* the
`Pos`/`Items` text — producing the observed `1000Traveller 7Lv 1Pos 0 1221,10014EXP`
mash. It is a layout-collision bug, not a text-rendering bug. The fix is a
right-cluster-in-its-own-child or a measured min-x clamp; **flagged for the
humans (code change).**

**Player impact:** the very first frame of a new game shows a broken HUD. For a
Steam product this is an immediate "unfinished" signal. **MAJOR.**

### 4.2 🟡 Headless frame-capture is stubbed for the SUBWORLD path

`src/app/main.cpp:765-770` `smoke_framebuffer_has_world_pixels()` is a **stub**:

```cpp
bool smoke_framebuffer_has_world_pixels(const App& /*app*/, int& samplesHit) {
    // Vulkan readback not yet implemented (PHASE C). Assume pixels are present.
    samplesHit = 9;
    return true;
}
```

It unconditionally returns "world is visible," so the `wait_visible` smoke
action (main.cpp:6181 `WaitVisible`) is a pass-through that **cannot actually
settle the async subworld**. Consequence, reproduced three times this pass with
three different scripts/seeds: the documented capture command
(`new_game,wait_boot_done,subworld_enter,capture_frame,quit`, render.md:493-495)
**captures the macro map fine but HANGS after `subworld_enter` and never emits a
subworld PNG** — the run prints `subworld_enter active=1 3d=1` and then spins
indefinitely (killed by a 60 s watchdog; `capture_frame` is never reached). So
`render.md`'s own subworld-capture example does not currently produce a subworld
PNG on this machine. This undercuts the AGENTS.md mandate to "screenshot
liberally and LOOK" — the LOOK is not actually available for the 3D subworld
through the documented path. **Minor for correctness, but it means every "I
verified the subworld frame" claim in the handoff docs should be treated as
UNVERIFIED** unless a real PNG is attached. (Macro-view capture works perfectly —
`audit_macro.png` this pass.) The subworld renders (`active=1 3d=1` every run);
only the *headless capture* of it is broken.

*(More live-frame findings — subworld 3D, night lighting, combat — added as
captures land.)*

### 4.3 🔴🔒 SECURITY — committed API auth token (rotate immediately)

`paperdolfix.txt` at the repo root is a checked-in raw terminal-session dump that
contains an **`ANTHROPIC_AUTH_TOKEN=…` assignment** (pattern CONFIRMED present;
value deliberately not reproduced here). It is **git-tracked** — `git ls-files`
matches it and `git log --follow` shows it entered history in commit `12f6ca5`
("пофкишены краши изза пейпердолов и тд").

**Why this is a 🔴 and not a cleanup item:** a secret in git history is not
removed by deleting the file — every clone and the remote retain it. The only
correct remediation is:
1. **Revoke / rotate the token now** (assume it is compromised).
2. Then remove the file and, if policy requires, scrub history (`git filter-repo`
   / BFG) and force-push — a *code/ops* action for the humans; **I do not touch
   git history or code.**

The doc-census sweep surfaced this; I confirmed it first-hand (pattern match +
tracked status + introducing commit). It is the most urgent single item in this
audit.

### 4.4 🔴 CI is broken and deploys the dropped browser target

`.github/workflows/deploy.yml` is a live workflow (`on: push: branches:
[main, rewrite]`) that:
- runs `emcmake cmake … && emmake make` — the **Emscripten/WASM** path the docs
  (AGENTS.md, README, vulkan.md) all say is **dropped**;
- copies `build/samosbor.{html,js,wasm,data}` into `dist/` and deploys to
  **Cloudflare Pages** (`pages deploy dist --project-name=samosbor`).

Two hard defects:
1. **Output-name mismatch → guaranteed failure.** CMake declares
   `project(timaert)` (`CMakeLists.txt:2`), so Emscripten would emit `timaert.*`,
   not `samosbor.*`. The `cp build/samosbor.html …` step cannot find its inputs;
   the job fails on every push.
2. **It contradicts the stated architecture.** The one piece of *automation*
   in the repo resurrects the abandoned browser target and would, if it worked,
   publish a WASM build the project no longer supports.

**Fix or delete.** (Code/ops change → flagged for humans.)

---

## 5. 🟡 Structure: `main.cpp` is 7551 lines (7.5× the stated limit)

`src/app/main.cpp` = **7551 lines**. AGENTS.md / README File-Organization rule:
*"never let one exceed 1000 unless it is a naturally encapsulated module."*
`main.cpp` is the opposite of encapsulated — it's the junction box: SDL/Vulkan
boot, the whole main loop, **the entire smoke-script interpreter** (50+ tokens,
`rg` shows the token dispatch spanning lines ~466–6472), event/input routing,
save/load flow, subworld transition, console commands, HUD orchestration.

For a shipping product this is the single biggest maintainability liability:
every subsystem reaches into one file, so every change risks it. Extraction
candidates (for the humans — this is a *code* change I will not make): the smoke
interpreter → `app/smoke.cpp`; console commands → `app/console.cpp` (a
`debug_console.cpp` already exists — some lives there); input/event routing →
`app/input.cpp`; save/load UI flow → its own unit. [Full section map pending the
UI/app sweep.]

---

## 6. Subsystem: GPU / Vulkan / the crowd-sim promise — CONFIRMED

*(Independently verified by me + a dedicated read-only GPU sweep; every line
traced to `file:line`.)*

### 6.0 🔴 The GPU crowd simulation is VAPORWARE (5 independent proofs)

1. **Zero compute shaders.** `find . -name '*.comp'` → 0. The 30 files in
   `shaders/` are all `.vert`/`.frag`/`.glsl` raster + includes.
2. **Zero compute API usage.** `rg 'vkCmdDispatch|VK_PIPELINE_BIND_POINT_COMPUTE|
   vkCreateComputePipelines|VK_SHADER_STAGE_COMPUTE_BIT|std430'` over `src/` →
   the only hit is a *comment* (`src/macro/vk_macro_renderer.cpp:17`). No
   dispatch, no compute pipeline, no compute queue, no storage buffer, ever.
3. **The shader build list has no compute stage** (`CMakeLists.txt:227` — glslc
   invoked only on raster stages).
4. **The backend doc agrees it's unbuilt** — `vulkan.md:106` "P7 | Compute NPC
   mass sim + CPU↔GPU embodiment seam | **pending**." (So `vulkan.md` is honest;
   `ARCHITECTURE.md` is not — §2.6 / §6.5.1.)
5. **The render path can't even display "thousands."** `src/sub/vk_renderer_3d.cpp:280,286`
   allocate 512-entry instance buffers; `:700,739` silently drop everything past
   512. NPCs are pulled from the **CPU EnTT registry** every frame (`:729`) and
   uploaded with `vkCmdUpdateBuffer`.

**And the CPU sim it actually uses admits defeat under load:**
`tick_macro_npc_ai_budgeted` (`src/macro/npc_ai.cpp:555-632`) is a time-sliced
CPU sweep with a `max_npc_ticks` cap and a `kMaxQueuedSweeps = 4` backlog ceiling;
when exceeded it sets `result.backlog = true` and **skips** NPC updates. That is
LOD-skipping the AGENTS.md Hard Rule explicitly bans.

**Bottom line:** what ships today is a conventional CPU-simulated,
instanced-billboard renderer capped at 512 visible units — a competent
small-scale engine, not the compute-driven mass-simulation architecture the
vision requires.

### 6.1 🟠 BUG — `find_memory_type` returns type 0 on failure (fail-silent), ×3 files

`src/gpu/vk_buffer.cpp:20`, `src/gpu/vk_renderer.cpp:23`, `src/gpu/vk_shadow.cpp:18`
all `return 0;` when no memory type matches, then feed that 0 straight into
`VkMemoryAllocateInfo::memoryTypeIndex`. On a device whose depth-capable
DEVICE_LOCAL type isn't index 0 (plausible under MoltenVK heap ordering), the
depth/shadow buffer binds wrong-heap memory → validation error or corrupt depth.
The codebase's *own* correct pattern is right next door: `vk_texture.cpp:12` and
`vk_sprite_array.cpp:12` return `UINT32_MAX` and callers check it. Fix = unify
(also kills §6.4.1 duplication). **Flagged for humans (code change).**

### 6.2 🟡 BUG (latent) — `vkCmdUpdateBuffer` 64 KB limit couples to the 512 cap

`src/sub/vk_renderer_3d.cpp:706,754` push instances with `vkCmdUpdateBuffer`,
spec-limited to 65536 B/call. Today safe (512×20 B NPCs, 512×36 B creatures). But
the moment anyone raises the cap toward "thousands," creatures overflow at 1,821
instances → silent UB / validation break. The cap and transfer method are coupled
and undocumented as such — a trap for whoever tries to deliver the crowd promise.

### 6.3 🟡 BUG (latent) — public `VulkanBuffer::update()` does a full-queue `vkQueueWaitIdle`

`src/gpu/vk_buffer.cpp:110,176` — load-time-only today, but it's a public method
with no guard; any per-frame caller would serialize the whole GPU.

### 6.4 🟡 Hardcode + duplication (maintainability)

- **512 instance cap is a bare literal repeated 4×** (`vk_renderer_3d.cpp:280,286,700,739`)
  with no named constant, and `:725 reserve(256)` disagrees with its own 512 cap.
- **Shadow dim `4096` bare literal** (`:299`); depth-bias `1.0/0.0/1.5` bare
  literals (`:1410`); near/far `0.5/1500` bare (`:1517`).
- **`find_memory_type` reimplemented 5×**; **one-time-submit boilerplate ~17×**;
  **`make_buffer` copied 3×**; **4 near-identical 120-line pipeline creators**
  (`vk_pipeline.cpp:45,165,183,323`, ~460 LOC) — the largest dup surface. All
  are the kind of copy-paste AGENTS.md's "ZERO MOCKS / no boilerplate" standard
  is meant to prevent.

### 6.5 📄 DOC-DRIFT (I will fix the doc side)

- **6.5.1 (severe)** `ARCHITECTURE.md:82-83,117-139` describes the GPU mass sim +
  embodiment/de-embodiment seam in **present/declarative tense** as if it exists.
  Reality: §6.0. Highest-impact doc lie in the project. **Fix: re-tense to
  "planned / P7," matching the honest `vulkan.md:106`.**
- **6.5.2** `render.md:211` says raster depth bias is "**disabled**," but
  `vk_pipeline.cpp:377 depthBiasEnable = VK_TRUE` + `vk_renderer_3d.cpp:1410
  vkCmdSetDepthBias(1.0,0,1.5)` enable it — AND the receiver shaders also bias
  (`shadow_common.glsl:17`), so the engine double-biases, the very peter-panning
  render.md claims to avoid. Doc stale or code regressed; **flag both** (the
  code half is for humans).
- **6.5.3** `vulkan.md:105` lists "delete `src/gl/`" as pending P6, but `src/gl/`
  is already gone. Stale phase bookkeeping.
- **6.5.4** `PointLight`/`kMaxPointLights` (`src/sub/lighting.h:21,23`) are DEAD —
  defined, zero consumers. render.md/ARCHITECTURE honestly call point lights the
  "approved next increment," so no drift there — just dead scaffolding to note.

### 6.6 🎮 Renderer design verdict (Steam lens) — genuinely mixed, leaning good

The GPU sweep read every lit shader. Honest assessment:
- **Sky is ship-quality** (`shaders/sky.frag`): procedural day/night/twilight,
  sun+moon disc/glow/scatter sharing one bearing, 3-layer stars + Milky Way +
  twinkle, drifting FBM clouds. A real strength.
- **Water is competent-stylized** (`water.frag`): animated waves, Fresnel sky mix,
  sun/moon glitter road. Gaps: no true reflection/refraction, no foam, constant
  0.82 alpha — fine for rivers, thin for oceans.
- **Lighting is ONE directional light + flat ambient** (`lighting.h:67`). The
  biggest visual-ambition gap: no torches/campfires/spell/window lights at all
  (§6.5.4), so night towns & dungeons are lit by a flat blue ambient floor.
  4-band quantized N·L is a defensible stylized choice but caps fidelity.
- **Shadows**: single 4096² map over a ~1 km radius, no cascades; billboards cast
  but don't *receive* (NPCs in a building's shadow won't darken). Documented gap.
- **Terrain is procedural-color only** — no albedo/normal/roughness textures. Art
  choice, but a reviewer will read it as "flat."
- **Crowd density** ties back to §6.0: ≤512 CPU-fed billboards is not an "army."

## 7. Subsystem: Macroworld (M&B layer)

*(Read-only sweep of all `src/macro/*`; headline items re-verified first-hand.)*

### 7.1 🟠 BUG — stamina debt compounds into HP (player-facing, reachable)
`src/macro/travel.cpp:68-72` subtracts move cost from SP, and when SP goes
negative bleeds the overflow into HP — **but never floors SP back to 0**:
```cpp
cs.currentSp -= cost.totalCost;
if (cs.currentSp < 0) { cs.currentHp += cs.currentSp; }   // SP stays negative
```
The macro walk charges one cell at a time between minute-boundary recovery ticks,
so a persistently-negative SP is re-applied to HP **every step**, and the HP loss
per step accelerates (SP=5→−25→−55→−85 …). The identical un-floored pattern is
copy-pasted at `src/app/main.cpp:1613-1615` (subworld drain). CONFIRMED by reading
both. Fix (for humans) = `cs.currentSp = 0;` inside the `if`, in both sites.

### 7.2 🔴🎮 DEAD FEATURE — spires are never generated
CONFIRMED end-to-end: the only writes to `gs.spires` are the save-loader
(`save.cpp:1043`) and `main.cpp:1431` moving an **always-empty** `fresh.spires`
out of `boot_world`. Grep for `generate_spires|place_spires|spires.push_back|
Spire{` finds only the loader. Yet spires are fully wired downstream —
`landmark_registry` (zone≥5 gating), `macro_lighting.cpp:216` (purple night-glow),
`save.cpp:684` (serialize), spawn-cell avoidance. A signature arcane POI that can
**never appear in a fresh world**. `macroworld.md:20` even lists "→ spires" as the
final generation step — the doc's last stage is fictional (§7.7).

### 7.3 🟡 BUG — RNG contract hole + reachable modulo-by-zero
- `core/rng.h:23` `next_f01()` divides `float(next_u32())` by `2^32`; the max
  draw rounds to **exactly `1.0f`** (24-bit mantissa), violating the "[0,1)"
  comment. Bounded downstream breaks: `sample_band` returns one over `hi`
  (`state.cpp:43` → a faction relation of 101 where the invariant is ±100);
  loot/inventory qty one over `max` (`items.cpp:233,305,322`). No OOB crash
  survives, but the contract is genuinely broken.
- `core/rng.h:26-29` `next_int(lo,hi)` is `lo + r % (hi-lo)` with **no guard** —
  `hi==lo` is modulo-by-zero UB, reachable via `content/quests/procedural.cpp:180`
  (`next_int(0, pickCount)` when the candidate set is empty).
- Third inconsistency: `base_generator.cpp:38` and `gens/dispatch.cpp:197` divide
  by `2^32−1` (also reaching 1.0), a *different* convention from `rng.h`'s `2^32`.

### 7.4 🟡🎯 CROSS-STL non-determinism in the faction matrix
`state.cpp:147-159` builds the relation matrix by iterating
`gs.factions` — a `std::unordered_map` — into a vector, consuming **one RNG draw
per pair** in iteration order. `unordered_map` order is implementation-defined, so
the same `worldSeed` yields a **different diplomatic matrix on libc++ vs MSVC**.
This contradicts `state.h:149` ("sampled deterministically from seed"). *Nuance:
AGENTS.md declares cross-build determinism a **non-goal** — so this is not a
rule-violation, but the in-code comment claiming determinism is still wrong, and
a macOS-made save loading differently on Windows is a real surprise.*

### 7.5 🟡 Hardcode: faction identity is `f[0]`-switch + latitude bands (two sources of truth)
`npc_spawn.cpp:117-138` maps faction id→index on the **first character**
(`'b'→bandits`), collapsing every `barbarian_*` kingdom to "bandits" and every
`*_magica` to index 1 — so a `barbarian_north` NPC is mislabeled. Separately,
`npc.h:261-267` assigns settlement faction by **latitude band**
(`ny<0.3 → magika/barbarians`), *ignoring* the actual Voronoi `cellOwner`
territory computed in `politik.cpp:340`. Two independent faction-assignment
systems that can disagree about who owns a city. Economy archetype (`state.cpp:268`)
and gold multiplier (`items.cpp:241`) are likewise `if`/`strcmp` chains, not
tables — so "add a faction = one row" (`macroworld.md:29`) is false; it needs
edits in ≥4 engine branches.

### 7.6 🟡 Duplication: two full A* implementations + a re-implemented torus metric
`spawners.cpp:131-390` is a complete second binary-heap A* duplicating
`pathfinding.cpp:51-263` (neighbour tables byte-identical in three places).
`politik.cpp:12-18` re-implements `torus_dist2` while the **same file** also calls
the canonical `core/torus.h` `torus_dist_sq` at `:344`. Two places to fix any
pathfinding/tie-break bug the rivers depend on.

### 7.7 📄🎮 The macro layer is static scaffolding, not a living M&B world
The defining M&B fantasy — parties, lords, war, shifting borders — is **absent**:
- **Faction relations are sampled once at boot and never updated.** `world_tick.cpp`
  (the only daily sim) touches settlements/economy/mood/garrison but never
  factions. *Correction to the sweep:* relations ARE read — by the **subworld**
  hostility resolver (`sub/engine.cpp:229`) — but **no macro-strategic** system
  (war, siege, alliance shift) ever consumes them. The elaborate WAR/ALLY bands
  in `resolve_band` drive nothing on the map.
- **No parties/armies/sieges.** Grep for `siege|conquest|declare_war|
  capture_settlement` in `src/macro` → nothing. The only "aggressive" AI
  (`npc_ai.cpp:323`) chases the **player** only; it never engages another faction
  or raids a settlement. Borders (`politik.cpp:382` Voronoi) are computed once and
  frozen forever.
- **The economy is the one live system** (`world_tick.cpp` produce→price→grow,
  trade caravans) — genuinely good — but it is **closed-loop cosmetic**:
  population/wealth never trigger expansion, garrisons never sortie, trade never
  funds a war. It ticks and feeds only tooltips + the night-glow rebake.
- **Possession has no macro payoff** — inhabiting a lord confers no diplomatic,
  military, or economic control because lords have no agency (above).

**Verdict:** the map *looks* alive (see §3, the live frame) but *is not*. This is
the #1 gap between the pitch ("command an army, wage war") and the build, tied
with the unbuilt GPU crowd sim (§6.0).

### 7.8 Legacy/dead in macro (cleanup, for humans)
- `map_generator.cpp:16-19` + `.h:12,17` — dead OpenGL-FBO / `webgl/shaders.ts`
  comments and two unused WebGL-era fields.
- `movement_cost.h:13-15`, `save.cpp:745-750` — archaeology comments for the
  removed `FT_Mountain` feature and the removed battle sub-state.
- `attributes.h:46-81` — 24 `PerkID` values but only **7** have info rows; perks
  8–24 render blank name/description if ever selected.

---

## 8. Subsystem: Subworld / microworld (M&M layer)

*(Read-only sweep of all 27 files in `src/sub/`; headline items re-verified.)*

### 8.1 ✅📄 GOOD NEWS, verified — the seamless-crossing "O(new content)" claim is HONEST
The single biggest technical claim of the microworld — that a 3×3 world-shift
costs O(new content), not O(3×3), via a toroidal shift — is **real and shipped**.
Traced end-to-end: CPU composite memmove (`seamless_manager.cpp:816` +
`shift_buffer` template), CPU height-grid memmove (`vk_renderer_3d.cpp:887-926`),
and **GPU material ping-pong** via `blit_shift_r8` + `vkCmdCopyImage` +
descriptor swap (`vk_renderer_3d.cpp:1120-1202`, `vk_texture.cpp:393-501`) — each
matching `seamless-crossing.md`'s formulas verbatim. This is genuine engine work
and it delivers what the doc promises. **Caveat (§8.6):** the *object* layer is
not O(new content).

### 8.2 🟠 BUG — no collision: the player walks through walls, houses, trees, water
`engine.cpp:1676-1704` (`move_player`) integrates position and clamps **only** to
`[0, kFullSize]`. It never reads `out.trav` (which generators fill, e.g.
`dispatch.cpp:67`), tile type, water, wall, or structure radius. CONFIRMED. Every
settlement wall the generators carefully stamp is cosmetic — you cannot be
funneled, blocked by a gate, or use a building as cover. For a Daggerfall/M&M
explorable world this removes the entire spatial-gameplay dimension.

### 8.3 🟠 BUG — generated bridges are invisible
`gens/dispatch.cpp:1164` emits `Structure{Structure::Bridge, …}` (deck height 3.0)
at road/river crossings, but the renderer only draws `Tree` (`vk_renderer_3d.cpp:1312`)
and `House`/`Wall` (`:1357`) — `Bridge` (and `Rock`) fall through both filters and
are **never drawn**. CONFIRMED. Every road that crosses water shows the path
plunging into the river with no visible deck — a visible defect at every crossing.

### 8.4 🟡 BUG — `record_main` silently drops haste/flight/px/py (dead computation)
`vk_renderer_3d.cpp:1493-1499` names its `haste/flight/px/py` params `/*…*/`
(unused). `engine.cpp:1889` computes `hasteAura`/`flightAura` via
`spellbook_has_sustained` and passes player position **every frame**, all thrown
away. Either the first-person haste/flight screen effect is unimplemented or the
parameters are dead weight — decide and wire or delete.

### 8.5 🟡 Latent — `Structure::Kind` enum disagreement (value 4 = Corpse vs Bridge)
`ecs/components.h:176` `enum { …, Corpse=4 }` vs `sub/map_data.h:45`
`enum { …, Bridge=4 }`. Value `4` means two different things in the ECS vs
map/render domains. No code currently casts between them, so **latent, not live** —
but `map_factory.cpp:66` hardcodes `kKinds=5` in the map numbering, deepening the
trap for anyone who ever bridges the two sets.

### 8.6 📄 Drift — the "O(new content)" claim oversells the object layer
True for terrain/material (§8.1), but on **every** crossing the CPU still does
O(all-9-cells) work: `rebuild_composite_structures` (`seamless_manager.cpp:392`)
rebuilds the whole structure vector, and `vk_renderer_3d.cpp:1304-1388` destroys
+ recreates the **entire** tree/structure instance buffer from the full 3×3 list.
The doc's blanket "a crossing is O(new content)" should be scoped to the terrain
surface; the object/instance path is still O(window). (Perf table 232-241 only
measures terrain+material, so it hides this.)

### 8.7 🎮 The world is a beautiful skin with no insides (biggest M&M gap)
- **No interiors, no dungeons.** Grep confirms zero door/enter-building/dungeon
  system in `src/sub`. Cities/villages/ruins/spires (`dispatch.cpp` generators)
  are **exterior box clusters only** (`vk_renderer_3d.cpp:1357` draws them as
  solid instanced cubes). M&M6-8's entire loop — enter buildings, descend
  dungeons, clear interiors — has no substrate here.
- **The only interaction verb is "loot corpse"** (`engine.cpp:1217` `interact()`
  scans exclusively for `Corpse`). No talk/trade/quest-giver/door/lever/container.
- **Combat is competent but content-thin**: encounters are `roll_fauna` random
  scatter (`fauna.cpp:127`) from a **19-creature** catalog; no set-pieces, bosses,
  or guarded loot — no reason to go *there* vs *anywhere*.
- Highest-leverage additions, in order: (1) collision so structures matter;
  (2) an interior/dungeon load system; (3) interaction verbs beyond corpse-loot;
  (4) render Bridge/Rock so existing generated content is visible.

### 8.8 Legacy/dead in sub (cleanup, for humans)
- `spatial_hash.h` (118 lines) — **entirely dead**; combat/AI use direct
  `reg.view<>` scans, zero references to the hash anywhere.
- `tree_atlas.cpp:26` `tree_type_for(Biome,hash)` — no callers (only the
  `_for_temperature` variant is used).
- Pervasive "must match the GL Renderer3D" / "compiles unused until the flip"
  comments (`vk_renderer_3d.h:2-4`, `.cpp:30,772,1318`; `camera.h:23`;
  `base_generator.h:17`) describe a GL original that no longer exists — this IS
  the shipping renderer. Stale framing.

---

## 9. Subsystem: ECS / combat / possession / RPG

*(Read-only sweep; the marquee "two-PlayerTag-after-load" hypothesis was tested
and NOT found — reported honestly below. The real problems are structural.)*

### 9.1 ✅ Verified clean — the "exactly one PlayerTag" invariant holds
Traced boot, save/load, subworld enter, possess, and exit-adopt: **every**
transition is remove-then-add or destroy-then-create, and a smoke test
(`main.cpp:5536`) actively asserts `tags == 1` after possession. Loot-roll
off-by-one (`items.cpp:225`), XP last-hit attribution, and `MacroSpawnId`
determinism were all checked and are **clean**. The possession architecture is
genuinely robust. (Two *latent* recovery gaps: `player_entity.cpp:51` dedupes only
the first 8 stray tags, `:12` only the first — neither reachable in normal flow.)

### 9.2 🔴🎮 Combat is raw `hp -= damage` — no mitigation, crit, dodge, or variance
CONFIRMED by grep: there is **no** `armor`/`defense`/`mitigation`/`resist`/
`dodge`/`evasion` symbol anywhere in `src/sub` or `src/macro`. All three strike
sites are literal subtraction (`engine.cpp:1121,1423`, `spell_effects.cpp:125`).
`critBase` is *computed* (`attributes.h:273` `lck/(lck+50)`) and **never read** by
any strike. So every hit is deterministic and identical: no defensive build
matters, Luck does nothing, there are no crits/misses, and damage has zero
roll-to-roll variety. For an M&M/Daggerfall target this is the single biggest
"tech demo, not an ARPG" gap. (Related: `expMult` from Wisdom is computed but
never multiplied in at the XP grant, `engine.cpp:1581` — WIS and LCK are dead
stats in combat.)

### 9.3 🟡 Possessed-body kills level the HERO, not the body you fight as
`engine.cpp:1568-1582`: `tick_player_melee` fires for the possessed body and
routes kill-XP to `gs_->player.sheet` — always the hero. But possession is
documented body-native ("possess a rat ⇒ weak as the rat"). So you fight *as* the
lord yet the hero levels up. Not a crash — an unresolved semantic inconsistency
the docs never address (possession.md claims body-native). Decide and document.

### 9.4 🟡 Hardcode + duplication in spawning
- Level-scaling law `1 + (lvl-1)*0.15` is hand-copied in `spawn.cpp:191` and
  `engine.cpp:1299` (bare `0.15f` both); player base melee `10.0f + rawPhysDamage`
  exists twice (`engine.cpp:650,717`) and can silently diverge spawn-time vs
  tick-time; danger boost `1 + zb*0.18` bare at `spawn.cpp:288`.
- **Four parallel humanoid-emplace sequences** (`spawn.cpp:153`, squad, projection
  `:566`, `engine.cpp:1348`) build the same ~13-component stack by hand. The fauna
  path *was* unified into `emplace_fauna_entity` — but it's file-local, so
  `engine.cpp:1304` re-implements it line-for-line for monsters. A new required
  component must be added in all four.
- Three `NpcCharacter` visual generators with a **drifted tint base** (150 vs 160:
  `spawn.cpp:44` vs `engine.cpp:192` / `npc_spawn.cpp:108`) — the *same* NPC can
  render a different color depending on which path built it.

### 9.5 🟡 Dead code/data
- `ai.cpp:129-150` legacy no-`SubworldAi` view — always empty (every spawn path
  emplaces `SubworldAi`). `ai.cpp:99-124` `Combat`-case "no Combat component"
  degrade branch — unreachable (every `Combat` AI entity has a `Combat` component).
- `fauna.h:60-61` `xpReward`/`lootId` — **never populated** on any of the 19 rows,
  so the per-creature XP/loot override machinery is 100% inert; every monster falls
  back to `exp_from_fight` + faction-default loot.

### 9.6 ✅ Verified clean — no battle-mode / RPS remnants
Grep for `battle mode|RPS|UnitType|kUnitStats|damage_multiplier` across `src/` →
**zero**. The docs' "no separate battle mode, no RPS table" claims (AGENTS.md:179)
are accurate. Combat is unified subworld play as designed.

---

## 10. Subsystem: Events / content / quests / spells

*(Read-only sweep of `src/events/*` + `src/content/*`; the five most severe items
re-verified first-hand. This is the emptiest layer in the project.)*

**Headline:** the L3 event/logic-node plumbing is real and mostly clean; the L4
**authored content on top of it barely exists**, and two of seven quest types are
**broken end-to-end**. Confirmed content census: **8 spells** (2 do nothing),
**7 procedural quest templates** (5 functional), **15 flavour encounters**, **1**
character-creation intro, **0** hand-authored main/side quests, **0** dialog/branching
narrative, and **1 permanently-dead placeholder plot chapter**.

### 10.1 🔴 BUG — `SpawnEntity` has producers but ZERO consumers → combat quests spawn nothing
CONFIRMED by producer/consumer sweep: `content/quests/procedural.cpp:245` (`gen_destroy`)
and `:286` (`gen_protect`) push a `SpawnEntity` event onto `q.onAccept`, `QuestEngine::accept`
faithfully emits it — and **nothing listens** (`rg 'case EventTag::SpawnEntity'` → 0
handlers). So accepting a "clear the road / eliminate N bandits" or "defend the
village" quest **spawns no enemies**. The quest is then either impossible or (via
§10.2) falsely completed off an unrelated creature. **2 of 7 quest types are dead
on arrival.**

### 10.2 🔴 BUG — DestroyNpc compares an entity-id to an NPC-type → false completions
`events/quests/quest_engine.cpp:126`: `if (… (int(ev.a) == o.npcType || ev.ix == o.npcType))`.
`ev.a` is the EnTT **entity id**; `o.npcType` is an `NPCType` **enum ordinal**. On a
fresh world entity ids start 0,1,2,3…, so a "kill 2 Bandits (type 4)" objective is
satisfied by killing **entity #4 regardless of what it is** (a deer, a merchant).
Only the `ev.ix == o.npcType` half is correct. Worse, the test
`quest_lifecycle_test.cpp:1479` **enshrines** the bug (asserts both the `.a` and
`.ix` paths increment the kill count), so a green test locks it in. CONFIRMED.
*(The same objective is also zone-blind: `eval_objective` never reads
`o.ix/o.iy/o.zoneRadius` for DestroyNpc, so a kill anywhere on the map counts,
while the description says "near …".)*

### 10.3 🟠 BUG — quest XP reward (and `grant_xp` effect) never levels the player up
`quest_engine.cpp:78`: `case RewardKind::Xp: p.sheet.levelData.exp += r.amount; break;`
— adds raw exp and stops. The **combat** path drains the ladder
(`sub/engine.cpp:1582 while (try_level_up(...)) {}`), so **XP from a kill levels you
up but identical XP from a quest reward does not** until some later combat tick
happens to re-drain it. CONFIRMED. Root cause is §10-dup: `try_level_up` is called
from ~5 scattered sites with no single "award XP" chokepoint, so the reward path
forgot it.

### 10.4 🟠 BUG — world-map spells burn mana and do nothing
`app/main.cpp:1737`: every non-sustained macro spell hits
`emit_spell_cast(app, id, false, "World-map spell effect not implemented")`.
`haste`/`flight` are sustained toggles that **deduct mana every tick**
(`spell_book.cpp:190`) but **no travel/movement code reads their `macroType`** — so
the two world-map spells drain mana with zero mechanical effect. CONFIRMED. (This
also makes spells.md:3's "adding a spell is one file, no engine changes" false for
any macro-effect spell — §10.6.)

### 10.5 🔴🎮 DEAD FEATURE — there is no plot. Chapter 1 is `return false`.
`content/plot/chapter_1.h:11-25`: the sole post-intro plot node's condition is
literally `[](const EventBus&, const PlayerState&){ return false; }`. It is
registered and `activate`d from `main.cpp:2057`, so the entire narrative layer past
the character-creation intro is one hard-coded always-false node. CONFIRMED. No
`chapter_2`, `main_quest`, `plot_beat`, or `storyline` data exists (grep empty).
The "grand RPG" story is unwritten.

### 10.6 🟡 Hardcode + dead schema
- **Effect verbs are an if/else string chain** (`effect_applicator.cpp:8-28`:
  `if(name=="heal_hp")…else if("grant_xp")`), not the registry the objective/reward
  sides use — contradicting quests.md's "no hardcoded if-chains."
- **`NPCType::Bandit` is the only enemy any quest can target** (`procedural.cpp:235,246,287`)
  — every combat quest in the game is "kill bandits."
- **`level_up_dialog_node`** (`node_registry.cpp:91`) listens for `PlayerLevelUp`,
  which **nothing emits** (combat/quest both call `try_level_up` directly) → the
  node can never fire in production.
- **All 8 README-flagged "partial" EventTags are dead on BOTH ends** (0 producers,
  0 consumers): `NpcHpChange, SettlementMoodChange, PlayerStatChange, BattleEnd,
  MagicSurge, FactionRelationChange, DialogStart, CameraMove`. README.md:299-304
  calls them "partial for several" — the honest statement is "8 of 8 unused." (§2
  doc-fix candidate.)

### 10.7 ✅ What's genuinely sound here
The event bus, logic-node engine, and the quest **objective/reward** sides are
real registries (one `switch` case to extend). The spell registry is clean data (8
rows). The intro character-creation story (`content/plot/intro.cpp`, 9 slides) is
authored and works. The plumbing is finished; the content is not.

---

## 11. Subsystem: UI / app / main-loop

*(Read-only sweep of `main.cpp` + `src/ui/*`; headline items re-verified.)*

### 11.1 🟡 STRUCTURE — `main.cpp` is a 7551-line junction box (≈63% test harness)
Beyond the raw line count (§5), the sweep found the file's bulk is the
**smoke-script interpreter** — a ~4,900-line headless test harness (50+ action
tokens) living inside the shipping entry point. Extraction candidates (for humans):
smoke interpreter → `app/smoke.cpp`; console commands → the existing
`debug_console.cpp`; input/event routing → `app/input.cpp`. This is the top
maintainability liability: every subsystem reaches into one file.

### 11.2 🟠 BUG — GL-era `SDL_GL_GetDrawableSize` on a Vulkan window
CONFIRMED: `main.cpp:1876` calls `SDL_GL_GetDrawableSize(app.window, …)` on a
window created with `SDL_WINDOW_VULKAN`. The other two resize/init sites correctly
use `SDL_Vulkan_GetDrawableSize` (`:1057`, `:7490`). On a Vulkan window the GL
variant has no GL drawable to query → likely returns stale/zero dimensions on that
specific resize path (a swapchain-recreate size bug waiting to happen).

### 11.3 🟠 Player-facing UI defects (Steam "unfinished" signals)
- **HUD overlap** — the top-right status cluster renders as unreadable garbage
  (`Gold 1000Traveller 7Lv 1Pos 0 1221,10014EXP`). Full root-cause in §4.1
  (`screens.cpp:421` absolute-positions the name block with no gap-check against
  the left cluster). Seen in the very first frame of a new game.
- **Phantom control hint** — `screens.cpp:489` advertises `[F] 2D/3D` in the
  subworld hint bar, but there is **no `SDLK_f` handler anywhere** (grep confirms
  zero). The key does nothing; the hint promises a 2D/3D toggle that (per §2.9)
  doesn't exist. Remove the hint (doc/text) — the *code* to add a toggle is a
  human call, but the false advertisement is the immediate defect.
- **Stale version string** — `screens.cpp:82` title screen prints
  `"v0.1 — C++ / OpenGL / EnTT port"` (OpenGL is gone). The file banner
  `main.cpp:1` likewise still says "SDL2 + OpenGL 3.2 Core + ImGui." **These live
  in `.cpp` source — they are CODE, NOT documentation, so they are FLAG-ONLY: I do
  not edit them** (correcting my own earlier wording, which wrongly self-assigned
  them — logged in §13.1). The player-visible one (`screens.cpp:82`, shown on the
  title screen) is the more urgent of the two for a Steam build.

### 11.4 🟡 Honest-but-awkward UI surfaces (verified accurate, not bugs)
- The settlement overlay "Enter City" button (`overlays.cpp:1700`) is
  **deliberately disabled** with a tooltip explaining entry routes through
  Enter/In — not a silently-dead button (the sweep's framing was slightly off; I
  checked the code). Fine as-is, if a little confusing.
- The "Build" tab (`overlays.cpp:1723`) shows an honest apology that the TS source
  never defined build projects/costs — acceptable placeholder, clearly labeled.
- **Gap to flag:** `wants_subworld_relative_mouse` (`main.cpp:1490`) does not
  appear to consult debug-console open-state, so opening the console in the
  subworld may not release the relative-mouse grab (couldn't fully confirm the
  console's open-accessor; flagged as *needs-human-check*, not asserted).

---

## 12. 🎮 Game-design / product critique (post-sweep verdict)

Judged as a thing to **sell on Steam**, not as an engine demo. The subsystem
sweeps answered the open questions from the first pass — mostly unfavourably for
the *game* (favourably for the *engine*).

**The one-line verdict:** this is an impressive **engine** wearing the costume of
a **game** that is not built yet. Every foundational *system* is real and often
elegant (seamless world, unified combat, possession-as-a-flag, live economy,
ship-quality sky). Almost every *game* on top of those systems — the reasons a
player buys and keeps playing — is absent, stubbed, or inert.

**Macro layer (Mount & Blade promise): NOT playable as pitched.** CONFIRMED (§7.7):
factions never change relations, there are no parties/lords/armies/sieges, borders
freeze after generation, and possessing a lord grants no strategic control. The
"command an army of thousands and wage war" fantasy — the headline Steam hook — is
a beautiful static map with a live-but-cosmetic economy ticking underneath. Tie
this to §6.0 (the GPU crowd sim that would *render* those thousands is also
unbuilt) and the entire M&B pillar is, today, aspiration.

**Micro layer (Daggerfall / M&M 6-8 promise): a skin without insides.** CONFIRMED
(§8.7): no interiors, no dungeons, no doors — cities are enterable-looking box
clusters you actually walk *through* (§8.2, no collision). The only interaction
verb is "loot corpse." Combat (§9.2) is raw `hp -= damage` with no armor, crit,
dodge, or variance, over a 19-creature catalog of random scatter with no
set-pieces or bosses. It plays as "walk across pretty procedural terrain and fight
random animals," not "explore Enroth."

**RPG layer: character building is decorative.** CONFIRMED (§9.2): Luck and Wisdom
are computed into `critBase`/`expMult` that **nothing reads**; there is no
defensive stat, so no build choice changes how you take damage. Perks 8–24 are
blank (§7.8). A signature POI (spires) can never spawn (§7.2).

**What genuinely counts as a differentiator today:**
- The **seamless infinite overworld** with a real O(new-content) crossing (§8.1)
  — this is hard tech done correctly and is a legitimate selling point.
- The **authored intro narrative** (§3) — more than most engine-first projects.
- The **procedural sky** (§6.6) — ship-quality atmosphere.
- The **possession architecture** (§9.1) — clean and robust, *if* a game is built
  around it (today it is mechanically thin, §9.3).

**Product risk ranking (what to fix before a Steam page is honest):**
1. Build the M&B strategic layer (war/parties/borders) **or** re-pitch the game —
   the store page cannot currently claim it (§7.7, §6.0).
2. Add collision + interiors + interaction verbs so the microworld is explorable
   (§8.2, §8.7).
3. Add a combat mitigation/crit/variance layer so builds and RPG stats matter
   (§9.2) — the machinery (`critBase`, `expMult`) is already computed; it just
   needs to be *read*.
4. Generate spires (§7.2) and render bridges (§8.3) so already-authored content
   is reachable/visible.
5. Fix the first-frame "unfinished" signals: HUD overlap (§4.1), phantom `[F]`
   hint and stale version string (§11.3).

None of this is a criticism of the engineering, which is strong. It is the honest
gap between "the systems compile and run" and "there is a game here to sell."

---

## 13. Method log / provenance

> **⚠ Two environment facts discovered in Pass 4 that affect every git-based check
> in this audit (record once, apply everywhere):**
>
> 1. **The git repo root is the PARENT dir `/Users/jirnyak/Mirror/timaert`, and
>    `timaert_c/` is a *subdirectory*, not its own repository.** Consequences for
>    any auditor: (a) `git show HEAD:CMakeLists.txt` reads the *parent's* root
>    `CMakeLists`, NOT `timaert_c/CMakeLists.txt` — you MUST path-qualify as
>    `git show HEAD:timaert_c/CMakeLists.txt`; (b) path-scoped `git log -- <file>`
>    from inside `timaert_c/` silently resolves against the wrong root and returns
>    empty/misleading history. My earlier-pass git archaeology hit exactly this and
>    briefly produced a false "not committed" reading, immediately corrected once
>    the topology was found. Always run repo commands with `git -C
>    /Users/jirnyak/Mirror/timaert` and repo-relative `timaert_c/...` paths.
> 2. **A concurrent agent is actively committing to this tree.** During Pass 4,
>    HEAD moved `68bc669` → `bca8812` *between two commands in the same pass*
>    (`68bc669` seasons, `a6c9eb7` point-light SSBO, `9e5b666` faction-hostility,
>    then `bca8812` landmark-icon fix), and `git status` shows in-flight
>    uncommitted **code** edits (`src/ecs/components.h`, `src/sub/vk_renderer_3d.
>    {cpp,h}`, `src/ui/macro_overlay.cpp`). This is the "code updates regularly, so
>    re-review periodically" mandate made literal: **any numeric/behavioural finding
>    here is a snapshot and may already have moved.** Re-verify before acting; never
>    edit those in-flight code files. **Live proof:** during Pass-4 finalization the
>    count moved *again* — a **29th** test target (`torus_geometry_test`, new
>    `tests/torus_geometry_test.cpp` + an `add_executable` and a `foreach` entry in
>    `timaert_c/CMakeLists.txt`) was observed being added **uncommitted** by the
>    concurrent agent. This audit's **28/28** is the verified-green snapshot at HEAD
>    `bca8812`; the 29th is in-flight, not yet built or run by me, so it is
>    deliberately NOT counted here. (The `M timaert_c/CMakeLists.txt` in `git
>    status` is that agent's test-target addition — **not** an audit edit; my ctest
>    reconfigure only writes the generated `build/` dir, never the source
>    `CMakeLists.txt`.)

**New subsystems that landed while this audit was open (Pass 4, verified
first-hand — all genuine progress, none are the doc-lie GPU crowd sim):**
- `68bc669` **data-driven seasons** derived purely from world time — documented in
  `seasons.md`; a new `seasons_test` is the 28th ctest target (28/28 green).
- `a6c9eb7` **positional point-light SSBO foundation** — the "positional half" of
  subworld lighting (set-0/binding-1 storage buffer, `point_lights()` in
  `shaders/lighting.glsl`, persistently-mapped host-coherent ring, one descriptor
  across all five lit passes). **Status: plumbing landed, "inert until emitters"**
  (no `LightEmitter` sources feed it yet). **This is a GRAPHICS-side fragment-
  lighting SSBO — 0 compute shaders / 0 `vkCmdDispatch`** — so it does **not**
  discharge `vulkan.md` P7 (the compute *crowd-sim*), which remains correctly
  "pending." §2.6 / §6.0's "GPU crowd sim is unbuilt" finding still stands.
- `9e5b666` **universal faction hostility fix** — monsters now fight guards &
  citizens, not just the player.

- `2026-07-29` — **Pass 1 (skeleton).** Census (`rg`, manual `wc`; `tokei`/`fd`
  unavailable). Live Vulkan capture on MoltenVK (macro frame `audit_macro.png`;
  subworld capture found broken, §4.2). Launched 7 parallel read-only subsystem
  sweeps. Skeleton (§1–§6, §12–§13) written from first-hand evidence only.
- `2026-07-29` — **Pass 2 (subsystem integration).** All 5 content sweeps landed
  (macro, sub, ecs/combat, ui/app, events/content) + doc-census. §7–§11 written
  from them; **every CONFIRMED finding promoted here was re-verified first-hand**
  before inclusion (I do not take a subagent's word — e.g. I corrected the macro
  sweep's "relations read by nothing" to the accurate "read by the subworld
  hostility resolver but no macro-strategic system"; corrected the UI sweep's
  "dead Enter City button" to "deliberately disabled with a tooltip"; re-verified
  the leaked-token, broken-CI, spire-dead-feature, SP-debt, no-collision, raw-
  combat, SpawnEntity-no-consumer, and Chapter-1-`return false` claims with direct
  greps/reads). Two P0 code/ops items surfaced (§4.3 leaked token, §4.4 broken CI)
  — flagged for humans, not touched.
- `2026-07-29` — **Pass 3 (re-audit + authorised doc corrections; README).**
  Re-ran ground-truth greps before touching any doc — and the tree had **moved**
  since Pass 1: `src/` is now **entirely GL-free and emscripten-free** (0 GL API
  calls, 0 `emscripten` refs, no `src/gl/`; `src/gpu/` holds the full `vk_*` set),
  and a new **27th** ctest target (`faction_relations_test`) had landed. Verified
  first-hand: `ctest --test-dir build` → **27/27 green**; exactly **14
  `if(NOT EMSCRIPTEN)` + 1 `if(EMSCRIPTEN)`** guards in CMakeLists.txt; **11**
  biomes (`Water=9`, `Mountain=10`); `kSaveVersion=10`. **README.md corrected**
  (authorised — it is documentation): backend framing → "migration complete in
  `src/`"; WASM build block → "no browser build" note; Project Layout `gl/`→`gpu/`
  + `app/` "Vulkan window"; Controls table filled in from the verified `SDLK_*`
  handler block (I/Tab, P, B, E, V, Space, F5/F9); all six CTest "25/25 / 26"
  counts → 27 (+ `faction_relations_test` added to the list); Integration-Ledger
  date reconciled (Windows-build 2026-05-15 vs macOS ctest 2026-07-29). Then
  **corrected my own audit** to match new ground truth (§2.1/§2.2/§2.3/§2.7) and
  fixed the §11.3 self-inconsistency (code strings are flag-only, not mine to
  edit).
- `2026-07-29` — **Pass 4 (re-audit after another tree move + AGENTS/ARCH/README
  doc corrections completed).** Before touching any doc I re-ran ground truth and
  found the tree had moved **again** since Pass 3: three new commits (`68bc669`
  data-driven seasons, `a6c9eb7` positional point-light SSBO foundation, `9e5b666`
  universal faction-hostility fix) plus **in-flight uncommitted code edits** by the
  owner/another agent (`git status`: `src/ecs/components.h`,
  `src/sub/vk_renderer_3d.{cpp,h}`, `src/ui/macro_overlay.cpp` — untouched, code).
  Re-verified first-hand from a **clean reconfigure**: `ctest -N` → 28; `ctest
  --output-on-failure -j 8` → **28/28 passed**; the new 28th is `seasons_test`, and
  the previously-cited "unregistered 28th binary" `character_paperdoll_gl_smoke_test`
  is **gone** (target+source removed; stale artifact only). **Completed authorised
  doc corrections:** AGENTS.md (§8 recon arsenal rewritten to PRESENT/ABSENT
  reality, §11 `tokei`→`rg`/`wc` fallback, GLOB `gl`-removed, Layer-Discipline
  `gl/`→`gpu/`, build note Vulkan+no-WASM, Portable-build WASM block deleted);
  ARCHITECTURE.md (~17 edits — title `OpenGL`→`Vulkan`, source-layout `gl/`→`gpu/`,
  GPU-crowd-sim "⛔ NOT YET IMPLEMENTED" banner, module-map `vk_*` paths, biome
  10→11, v9→v10); README.md (all nine ctest sites 27→28, `seasons_test` added,
  `gl_smoke_test` deleted, `river_generation_test` caveat resolved, paper-doll row
  fixed). Then **corrected my own audit** (§2.3/§2.4/§2.7/§3 + this log) to the new
  28/28 ground truth.
- **Next:** remaining authorised doc corrections in the OTHER `.md` files —
  MASTER_PROMPT.md (biome 10→11 at :155, ctest 25/25→**28** at :740, stale
  "ctest registers none" at :382-392/:1015/:1066), translation.md (186-line
  mojibake re-encode + v8→v10 + "historical" stamp), vulkan_plan.md (mark migration
  complete / note the retired "slow is fast"). New leads to chase next pass: the
  landed **point-light SSBO** (`a6c9eb7`) vs. `vulkan.md` P7 wording, and whether
  `seasons` is documented anywhere. All **code** findings — incl. the surviving
  `character_paperdoll_gl.{cpp,h}` legacy source, the 4 OpenGL source strings,
  `SDL_GL_GetDrawableSize`, the §4.3 token, the §4.4 CI — remain **flag-only**.
  Periodic re-audit continues.

### 13.1 Sweep provenance & my corrections to them (T.A.R.S.)

The subsystem findings originate in read-only sweeps; I treat those as *leads*, not
truth. Corrections I made after first-hand re-check:
- **Macro sweep** claimed faction `.relations` are "read by nothing outside
  create_factions/save." FALSE — `sub/engine.cpp:229` reads them for subworld
  hostility. The accurate finding (no *macro-strategic* consumer) is in §7.7.
- **UI sweep** framed "Enter City" as a silently-dead button. It is
  `BeginDisabled()`-wrapped with an explanatory tooltip (`overlays.cpp:1700`) —
  intentional, not a bug. Corrected in §11.4.
- **Doc-census** said "18 `if(NOT EMSCRIPTEN)` blocks"; a second count found 14.
  **RESOLVED in Pass 3** by a first-hand recount: exactly **14 `if(NOT EMSCRIPTEN)`
  + 1 `if(EMSCRIPTEN)`**. My Pass-1 "18" was WRONG; the doc-census "14" was right.
  §2.2 now carries the exact count.
- **CTest count moved twice under the audit.** Pass 1 counted **26**; Pass 3 saw a
  27th (`faction_relations_test`) → 27/27; **Pass 4 sees a 28th (`seasons_test`,
  commit `68bc669`) → 28/28** (verified from a clean reconfigure). None were errors
  at the time — the code moved under the audit each pass. This is the periodic-
  re-audit mandate working as intended; README/audit counts are now 28, not 27/26.
- **My Pass-3 claim "the 28th binary is the unregistered
  `character_paperdoll_gl_smoke_test`" is now WRONG and is retracted.** That target
  and its source were removed (commits `a63ed38`/`94ffadb`); by Pass 4 the only
  trace is a stale binary artifact in `build/` (pre-reconfigure). Lesson reinforced:
  a "build/*_test binaries" file listing can lag the actual CMake targets — count
  `add_executable` + the `foreach`, not the build dir. (A *different* legacy GL
  paperdoll survivor — the `character_paperdoll_gl.{cpp,h}` **source**, no longer
  built into any target — remains, flagged as a code finding, not edited.)
- **My own §11.3 wording** earlier called the `screens.cpp:82` version string and
  `main.cpp:1` banner "corrections I can make." WRONG — they are `.cpp` **code**,
  outside the docs-only mandate. Corrected to flag-only in Pass 3.
- Everything else promoted to CONFIRMED was reproduced with my own grep/read.
