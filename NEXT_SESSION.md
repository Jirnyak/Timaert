# ЭПИК: ПЯТЬ ПАКЕТОВ — КАНОНИЧЕСКИЙ ФРЕЙМ (M-171 + M-244). СЕССИЯ ИДЁТ

ТОЛЬКО MAIN. Каталог `/Users/jirnyak/Mirror/timaert`.
Сборка: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`; вердикт —
`cmake --build /Users/jirnyak/Mirror/timaert/build --target check` плюс
`bash smoke.sh all`. Дефолтный таргет собирает ТОЛЬКО игру.

Читать по порядку: AGENTS.md (ЗАКОН КАНОНИЧЕСКОГО ФРЕЙМА + ЗАКОН ШВА) →
SKELETON.md (часть II «стык») → macro-registry.md (M-171, M-244, M-98, M-152)
→ этот файл.

**БАЗА (2026-10-17):** `check` **127/127**, смоуки **64/64**, предупреждений 0.
**ПРОЛОГ СДАН:** ломоть 8 M-150 закрыт коммитом `3bb48b90` — UnitGrid /
InfluenceField фикс-колонками по капам, эпик M-150 исчерпан ЦЕЛИКОМ.
**ПЕРВОЕ ДЕЙСТВИЕ нового агента:** стражи (`arch_guard_test`, `doc_refs_test`,
`state_census_test`, `law_carrier_test`) + `git log`.

## СОСТОЯНИЕ: ПЛАН ОДОБРЕН 2026-10-17; ЛОМТИ 1-2 СДАНЫ, ИДЁТ 3

Сдано: ломоть 1 `4ae7202e` (гигиена канала 44 → 34, zones_ мёртв; два
«мёртвых» включения оказались ТРАНЗИТАМИ — engine.h×macro_world.h и
spawn.cpp×fauna.h обменяны на честных носителей); ломоть 2 `ebb0946a`
(M-244a: колонка sub/CellSub построена, MacroVisual мёртв, сейв v139,
глайд карты — кэш глаз ui/macro_eyes, свидетель «entryDir ⇒ sub» в
macro_npc_trace). Ломоть 3 СДАН:
src/bus/ рождён (вердикт «шина живёт в src/bus/»; каркас каталогов
arch_guard расширен вердиктом): local_frame.h (LocalContext 142 120 Б,
пять родов, 5×5, static_assert sizeof+copyable), delta.h (StockOp 24 Б +
WorldFact-строки, DeltaBuffer 32 780 Б), cell_row.h (CellFacts/LandmarkFacts
переехали из macro/cell_facts.h), fact.h (WorldFact/FactKind/FactSubject из
macro/chronicle.h); macro_handle.h переехал macro/ → core/; словарь
FeatureType — macro/features.h → tables/feature_type.h; сборщик
fill_local_frame@src/macro/frame_fill.cpp (роды 1/3 — 25×cell_facts +
resource_field_read, род 2 — каркас SquadIndex с фолбэком честного свипа,
род 5 — chronicle_near радиусом 1 индексной клетки + точный отбор 5×5,
род 4 — скаляры gs; пины словарей static_assert у сборщика). Свидетель —
frame_fill_test (9 проверок; фикстура ОБЯЗАНА нести рукотворный терраин:
без него cell_facts отдаёт одинаковый дефолт и паритет строк слеп —
мутация-транспозиция прошла зелёной, поймано вживую).
Дальше: ломоть 4 — ВХОД по группам (субмир получает const LocalContext*);
5 — ВЫХОД-дельта; 6 — встречи 3×3 (остаток M-244); 7 — хвост канала.

Вердикты старта (записаны в M-171): порядок 1→7 одобрен; род 2 на выходе —
WRITE-THROUGH дверью рода 2 (буферятся роды 3 и 5); род 5 вниз — окно фактов
копией; **охват рода 1 — 5×5** (дословно: «ну если шире фрейм лучше то давай
5х5 - 5х5 или больше это же тоже через пакеты берутсы просто из 5 клеток?
если надо больше то это нормально (если что обсудить)») — фрейм несёт 25
строк клеток и генерация тоже читает фрейм; аренда и сквады — закон 3×3.
Ниже итог переписи (три жопуса, 2026-10-17), картина памяти и план.

## ПЕРЕПИСЬ ШВА — ИТОГ (2026-10-17)

**КАНАЛ ВКЛЮЧЕНИЙ:** 44 включения `macro/` из `src/sub` в 14 файлах, 21
заголовок; обратных 0; arch_legacy channel = 44, списку дерево идентично.
**12 включений МЕРТВЫ + 1 дубль** (суд — clang, не греп; пары
файл-включатель × мёртвый заголовок): sub/ai.h × anketa.h; sub/engine.h ×
macro_stock.h; sub/lighting.h × state.h; sub/vk_renderer_3d.cpp × fauna.h и
× state.h; sub/spawn.h × fauna.h и × features.h; sub/spawn.cpp ×
spell_book_state.h и × player_entity.h; sub/map_data.h × landmark_registry.h;
sub/engine.cpp × currency.h и × zones.h (указательный — fwd-декл есть);
дубль anketa.h в sub/engine.cpp. Доминанты живых: store.h 6,
anketa.h 5, macro_stock.h 3 живых, squad.h 2, state.h 1 живое (`GameState` в sub/engine.cpp).

**ЗАПИСЬ-НАЗАД «ПО ХОДУ»: 25 сайтов** (24 в sub/engine.cpp + sub/damage.cpp,
1 в sub/possess.h). Воронка ран ОДНА — `apply_damage@src/sub/damage.cpp`
(через неё 7 путей урона). По родам:
- род 2 (анкета через хэндл, 11 сайтов): рана (`apply_damage`), SP рубки
  (`apply_stamina_cost` у жатвы), godMode-откат, смерть игрока, жнец
  (`resolve_subworld_deaths@src/sub/engine.cpp`: hp=0 + `macro_mark_dead` +
  `kill_fallen_squad_creatures`), тот же свип в `leave`, XP (`award_kill_xp`),
  лут в сумку (`player_bag_of`), вселение (`possess_body@src/sub/possess.h`);
- род 3 (поля, 4): рубка и жатва (`macro_stock_apply` в
  `harvest_prop_near_player@src/sub/engine.cpp`), расписки
  (`settle_macro_debt` в жнеце), шпиль (`worked_write` в
  `learn_from_spire_orb@src/sub/engine.cpp`);
- род 1 (клетка игрока): `player_jump_to_cell` — на КАЖДОМ ре-центре шва
  (`sync_macro_player_to_center@src/sub/engine.cpp`) и на выходе данжа;
  `transfer_player_flag` — вход и вселение;
- род 4 (5): аренда — `lease_window_here_@src/sub/engine.cpp` + два
  `release_window` (ПОСТРОЕНА, M-226); репутация — два
  `add_player_reputation` (это материал РОДА 6 — gs.factions);
- род 5 (2): `record_deed` / `chronicle_record` (воронка
  `record_world_fact@src/sub/engine.cpp`).
НЕГАТИВЫ: `stamp_macro_debt` — колонка АРЕНЫ, не запись-назад; `worldTime` из
субмира НЕ пишется; `deserterPool` никем не читается; `search_chest` макро не
трогает; `mark_player_home` из sub не зовётся.

**ЧТЕНИЯ МАКРО ИЗ СУБМИРА.** В ТИКЕ каждый кадр — ровно пять групп:
`gs_->worldTime` (:1762 гейт помпы, :1964 зоны, :5450), `mapW/mapH` (:1975,
:3072), `playerFlagRef/playerSquadRef` (:5170-5171 в ИИ), и ОБЪЁМНО —
`MacroStore` как носитель ЗАПИСИ (зеркало :1322/:1338 на каждое тело с
бэклинком, ИИ :5159, бой :2359/:3486, снаряды :5180, жнец :3591-3696, игрок
:1363). `cell_facts` из тика — ТОЛЬКО раз в мировой тик (центр, `tick_day_pump`).
Вне тика: генерация клетки (`resolve_context@src/sub/engine.cpp`;
seamless_manager зовёт резолвер до 16 раз/клетку — соседи ЗА 3×3!), вес шага
×9 (`refresh_window_step_weights@src/sub/engine.cpp`, вход+ре-центр), вход
данжа (4 чтения в `enter_dungeon_scene@src/sub/engine.cpp`), акты (рубка,
`learn_from_spire_orb`, `read_sign`). `macro_stock_read` в тике — НОЛЬ.
МЁРТВАЯ КОЛОНКА: вид `zones_` движка (член в src/sub/engine.h) — ноль
читателей, зона едет через `cell_facts().zone`.
`mw_.squads/deposits/pathCost/treeGrid/nav` из sub не названы НИ РАЗУ — весь
доступ уже идёт через двери `cell_facts`/`macro_stock_*`.
Проекция сквадов (`project_macro_npcs_into_subworld@src/sub/spawn.cpp`, вход
:827 / ре-центр :1636) читает 13 колонок store поимённо (alive, dead,
runtime.squadType/entryDir/entryTicks, cell, kind, pools, level, sheet+gear,
character, inventory, spawnId).

**АРЕНДА УЖЕ ПОСТРОЕНА (M-226)** — наряд предыдущей сессии это не знал:
носитель `leasedWindow@src/macro/state.h` (род 4), двери `lease_window_at` /
`release_window` / `cell_is_leased` (центр + 8 румбов `cell_step`); писатели —
вход, ре-центр, подъём, данж-вход, выход данжа через ту же `enter` →
`lease_window_here_` по построению; слушатели — автобой ×2 + шаг угрозы
(три `cell_is_leased` в src/macro/npc_ai.cpp), свип мёртвых (гейт
`cell_is_leased` в src/macro/squad.h).
Остаток аренды в эпике: НОЛЬ (только свидетели, если найдутся дыры).

## КАРТИНА ПАМЯТИ (DOD п.10) — К ОДОБРЕНИЮ

1. LocalContext (НОВЫЙ файл src/bus/local_frame.h), 1 экземпляр, тривиально
   копируем, static_assert(sizeof): род 1 — `FrameCell` ≈64 Б × **25 (5×5,
   вердикт 2026-10-17)** = 1 600 Б (поля сегодняшнего CellFacts + cellOwner);
   род 2 — `MacroHandle[16384]` = 128 КиБ + счёт (кап = kUnifiedCap, вердикт);
   род 3 — `i32[10 рядов][25]` = 1 000 Б (запас полей); род 4 — скаляры ≈96 Б
   (worldTime, worldSeed, side, seaLevel, leasedWindow,
   playerFlagRef/playerSquadRef); род 5 — окно фактов `WorldFact[256]` =
   8 КиБ + счёт (вердикт: копией). **Итого ≈139 КиБ.**
2. DeltaBuffer (НОВЫЙ файл src/bus/delta.h): строка 24 Б {род, дверь, клетка u32,
   MacroHandle 8, величина i32, деталь i32}, кап 1024 = **24 КиБ**, член
   SubworldEngine; сливается КАЖДЫЙ мировой тик (кап = операции за тик).
3. M-244: колонки `subX/subY` i8 × 16384 = **+32 КиБ** store (сейв — дамп).
4. **УМИРАЕТ:** `MacroVisual` колонка store {vx,vy,speed} 12 Б × 16384 =
   **−192 КиБ**; 13 включений канала (44 → 31); вид `zones_`; три скана капа
   на кадр (`detect_forced_encounter@src/app/main.cpp`,
   `settlement_at_player@src/app/main.cpp`, панель соседей macro_overlay).
5. Итог по капам: +196 − 192 ≈ **0 КиБ**; по замеренному миру — то же
   (всё фиксированное, ЗАКОН СТАБИЛЬНОСТИ). Время: −3 скана капа/кадр.

## ПЛАН ЛОМТЯМИ (каждый зелёный, своим коммитом)

1. **Гигиена канала:** снять 12 мёртвых включений + дубль; суд
   `clang++ -fsyntax-only` с флагами проекта пофайлово; канал 44 → 31,
   arch_legacy тает; сюда же снос мёртвого вида `zones_`.
2. **M-244a: подкоординаты + смерть MacroVisual.** Форма решена владельцем:
   `i8 subX, subY` колонками store (0 = центр, −127..127, −128 вне закона);
   три двери записи — рождение (центр), марш при смене клетки (край входа по
   румбу), дельта шва (позже, ломоть 5); читатели — воплощение (точка спавна
   в клетке) и карта (интерполяция клетка+доля+скорость). MacroVisual умирает,
   сейв бамп молча. Проверка глазами владельца: спрайты карты.
3. **Типы фрейма `src/bus/`** (вердикт: шина живёт в src/bus/, L0): LocalContext
   + DeltaBuffer + static_assert'ы; переезд `macro/macro_handle.h` → `core/`
   (файл нищий, включателей 2 — bus не смеет включать macro/); сборщик
   fill_local_frame (НОВЫЙ файл в src/macro/; макро-сторона: 9×cell_facts +
   macro_stock_read + скаляры + окно фактов); свидетель формы.
4. **ВХОД по группам** (субмир получает `const LocalContext*`, конверт тает):
   4а род 4 — worldTime/worldSeed/mapW/mapH/seaLevel/хэндлы игрока из фрейма;
   4б род 1 — читатели клеток из строк фрейма 5×5 (вес шага ×9, день-помпа,
   зоны, данж-вход, орб, знак И генерация клетки — вердикт 5×5; широкий
   biomeResolver проверить отдельно: шире 5×5 — «если что обсудить»);
   4в род 3 — fauna/population данжа из фрейма; 4г род 5 —
   гейт onceEver из окна фактов (Q3). Обновление фрейма — ПЛАНОВОЕ: на границе
   макро-тика + на ре-центре, та же точка, что дельта.
5. **ВЫХОД: дельта.** Четыре сайта рода 3 (рубка, жатва, расписки, шпиль) и
   два рода 5 (`record_deed`/`chronicle_record`) пишут строки в DeltaBuffer;
   применение — ОДНА точка в кадре границы макро-тика (блок
   `tick_world_subworld_steps@src/app/main.cpp`: дельта вверх → тик → свежий
   фрейм вниз); свидетели M-98: коммутативность, свежесть, протухший адресат.
   Род 2 (рана/XP/лут/жнец) — НЕ буферится (ВЕРДИКТ 2026-10-17): это дверь
   рода 2 (зеркало record.h), write-through по хэндлу. Репутация (оба
   `add_player_reputation` в sub/engine.cpp) — материал
   рода 6, остаётся прямой дверью до прихода фракций в пакеты, named-остаток.
   Позиция игрока/аренда — протокол обмена, immediate (как есть).
6. **M-244b: встречи 3×3.** `detect_forced_encounter` → кандидаты рамкой 3×3
   через SquadIndex + гейт ЦЕЛОЙ дистанции в мировом подкоординатном
   пространстве (клетка×256 + sub, dx²+dy² ≤ R² в int32); панель соседей и
   `settlement_at_player` → SquadIndex. Три скана капа умирают.
7. **Хвост:** таяние живого остатка канала фреймом/дверьми (store.h остаётся
   ТОЛЬКО за record.h — дверью рода 2); стена (M-188-форма для sub↔macro) —
   ПОСЛЕДНЕЙ, НЕ в этой сессии: канал не пуст.

## РАЗВИЛКИ — ЗАКРЫТЫ ВЕРДИКТАМИ 2026-10-17 (дословно в M-171)

Q2 → write-through дверью рода 2; Q3 → окно фактов копией; Q4 → фрейм 5×5,
генерация из фрейма («5х5 или больше это же тоже через пакеты берутсы…
если надо больше то это нормально (если что обсудить)»).

## МЕТОД, КУПЛЕННЫЙ ЭТОЙ СЕССИЕЙ

1. Ломоть 8: у стража размерности не было свидетеля, пока его не родили
   вырожденным размахом (1e9) — таблица мутаций в теле коммита `3bb48b90`.
2. Наряд может не знать, что система уже построена: аренда (M-226) стояла
   целиком, перепись это вскрыла ДО того, как её начали бы строить второй раз.
3. «Мёртвое включение» по грепу — гипотеза до суда clang (zones_ держится
   fwd-деклом — снимается; комментарий у мёртвого включения player_entity.h
   в sub/spawn.cpp обещает имя, которого в заголовке нет вовсе).

## ВОПРОСЫ ВЛАДЕЛЬЦУ — ОТКРЫТЫХ НЕТ

План и три развилки закрыты вердиктами 2026-10-17; остаток «если что
обсудить» — только если biomeResolver окажется шире 5×5.
