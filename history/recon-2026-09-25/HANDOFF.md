# Сырьё переписи M-107 (2026-09-25) — как этим пользоваться в следующей сессии

Файлы этой папки — структурированные возвраты Opus-срезов по коду (dag / memory /
matrix / ledger / counts / open) с `file:line` и rg-командами. Верификация
рефутерами НЕ прошла (остановлена по бюджету); строки, вошедшие в SKELETON,
выборочно проверены по месту (см. коммиты 8c0569e, cf77d89). Остальное — свидетель,
не судья.

## Что сделано (ветка claude/dazzling-hamilton-gryf6t)
README переписан; CANON — структура + вердикты владельца 2026-09-25 + перенесённые
цитаты + ответы на 14 вопросов; SKELETON — DOD-карта: макромир (I.0–I.12), стык (II),
субмир по срезам тик/рендер (III), сводный реестр (IV); history/ — архив; снесено 24
дока (MANIFEST, MASTER_PROMPT, playtest, trailer + 20 макро-доков).

## Что НЕ сделано — по порядку цены/пользы
1. Срезы `S2-рождение-клетки` (sub/base_generator, gens/, kit/, dgn/, spawn.cpp) и
   `A1-кадр-приложения` (main.cpp frame целиком, smoke, ui) — дампов нет. Промпты
   срезов — в скрипте воркфлоу этой сессии (skeleton-code-recon), повторить
   двумя Opus-агентами и дописать SKELETON III.2/III.5.
2. Верификация 15 доков субмира/оболочки (context, controls, debug, dungeons, ground,
   macro-lighting, microcombat, microworld, population, render, seamless-crossing,
   shell-screens, sprites, ui-settings, vulkan; + proposals/): извлечь дословные
   цитаты владельца → CANON, факты → SKELETON III, снести. Промпт — docs-verify-extract.
3. Реестр: завести наряды из SKELETON часть IV (25 расхождений + вердикты владельца
   2026-09-25). Готовые формулировки — в SKELETON IV «Предложения нарядов».
4. Комментарии кода, ссылающиеся на снесённые доки (править в сессии кода):
   src/sub/engine.cpp:3491 (macrosim), src/macro/npc.h:29 и npc_ai.cpp:685,
   tests/woodcutter_gather_test.cpp:573 (resources), macro/squad.h:2,169,
   ecs/components.h:557, macro/auto_battle.h:1, macro/npc_ai.cpp:146,2631,
   macro/map_subject.h:62, app/main.cpp:561 (macrosim/economy),
   tests/sheet_registry_test.cpp:6,11 (rpg), tests/spell_registry_test.cpp:1
   (ARCHITECTURE → history/), tests/{spell_casting_effects,spawn_entity_event,
   celestial}_test.cpp (audit → history/), src/sub/engine.h:553 (postdemoaudit →
   history/), src/sub/vk_renderer_3d.h:3 (vulkan_plan.md — мёртвая давно).
5. AGENTS.md (правит владелец): ссылки на несуществующий sub/battle.h /
   kMaxBattleUnits / UnitGrid (теперь sub/movement.h: kMaxBodyCrowd, BodyCrowd,
   UnitGrid); пример «на 144 Гц — 57 секунд» в §3 ложен (цикл ждёт дедлайн тика).
6. Шапки кода, описывающие вырезанное (список в SKELETON «ЧТО ВЫРЕЗАНО»).
