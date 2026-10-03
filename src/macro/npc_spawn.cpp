#include "macro/npc_spawn.h"
#include "macro/agent_memory.h"
#include "macro/characters.h"
#include "tables/faction.h"
#include "tables/biomes.h"
#include "tables/npc.h"
#include "macro/deposit_layer.h"
#include "macro/npc_ai.h"
#include "macro/politik.h"
#include "macro/squad.h"
#include "macro/store.h"
#include "macro/anketa.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "core/torus.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace sm {

namespace {

// ── ВЕДОМОСТЬ ПАМЯТИ МАКРО-СКВАДА (AGENTS п.10, владелец 2026-09-21) ───────
// Стоит ЗДЕСЬ, потому что make_npc ниже — единственная дверь рождения сквада:
// из чего он собирается, там и сказано, сколько он весит. Любая новая
// колонка на скваде валит этот ассерт ГРОМКО и заставляет назвать цену.
//
//   структура              Б       ×16384 сквадов
//   NpcInventory       40 960      640.00 МиБ   ← 99 % всей памяти сквада
//   AgentMemory            136        2.13 МиБ
//   MacroNpcRuntime         92        1.44 МиБ  (92, не 96: −4 Б снос
//                                               морского хода 2026-09-22;
//                                               таблица врала до с.18)
//   SpellBook               80        1.25 МиБ
//   SquadRoster (Roster)    72        1.13 МиБ  (обвязка счетов, M-71)
//   Pools                   36        0.56 МиБ
//   MacroVisual             12        0.19 МиБ
//   NpcCharacter            12        0.19 МиБ
//   MacroCell/NPCKind/MacroSpawnId  4+4+4   0.19 МиБ
//   NpcLevel 2 + NpcTraits 3         5       0.08 МиБ
//   ИТОГО безусловно    41 417      647.1 МиБ  (замер с.18, 512 дней:
//                                              ~8 900 сквадов = ~352 МиБ)
//   + CharacterSheet       144  — только именным (npc_named)
//   + SquadOrders           34  — только маршрутным
//
// ЯДРО СУБЪЕКТА — inventory + roster, 41 032 Б — то же самое, что у места
// (state.h, Landmark): «ландмарк есть неподвижный сквад» (CANON S4) в памяти
// УЖЕ выполнено, расходятся они только на своей обвязке.
inline constexpr int kMacroSquadBytes =
    sizeof(ecs::NpcInventory) + sizeof(ecs::SquadRoster) + sizeof(AgentMemory)
    + sizeof(ecs::MacroNpcRuntime) + sizeof(SpellBook) + sizeof(ecs::Pools)
    + sizeof(ecs::MacroVisual) + sizeof(ecs::NpcCharacter)
    + sizeof(ecs::MacroCell) + sizeof(ecs::NPCKind) + sizeof(ecs::MacroSpawnId)
    + sizeof(ecs::NpcLevel) + sizeof(ecs::NpcTraits);
// 2026-09-24: слот единой таблицы 36 → 40 Б (level + entityId, слот В) —
// инвентарь 9216 → 10240, сквад 12 745 → 13 769 Б; 16384 таких = 215 МиБ.
// 2026-09-24, шаг А слияния: ёмкость контейнера 256 → 1024 (32×32, вердикт
// владельца) — инвентарь 10 240 → 40 960; 16384 таких = 695 МиБ. Цена
// названа и принята (CANON:5615, ~1.8 ГиБ по капам вместе со слиянием).
// 2026-09-24, шаг Б слияния: существа УЕХАЛИ В КОНТЕЙНЕР, ростер стал
// обвязкой счетов (3144 → 72) — сквад 44 489 → 41 417 Б; 16384 = 647 МиБ.
static_assert(kMacroSquadBytes == 41417,
              "макро-сквад весит 41 417 Б; 16384 таких = 647 МиБ (AGENTS п.10)");
static_assert(sizeof(ecs::NpcInventory) + sizeof(ecs::SquadRoster) == 41032,
              "ядро субъекта — то же, что у Landmark (CANON S4)");

// Переходник `RngFn` для реестра лута стоял здесь (`tl_rng`/`tl_rng_f01`) и
// умер вместе с броском профиля при спавне (M-139): рождению больше нечего
// катить, сумка выходит пустой.

inline bool is_land(const TerrainData& t, int mapW, int mapH, int x, int y) {
    if (t.width != mapW || t.height != mapH || !t.has_rgba_storage())
        return true;
    // ОДИН ПРЕДИКАТ, А НЕ ДВА: здесь стояла маска A < 128 И каскад биома —
    // проверка «маска говорит суша, а биом вода» (речная клетка). Каскад
    // биома теперь САМ отвечает порогом карты (`is_water`), значит второй
    // вопрос был вопросом к спеллингу, а не к миру.
    // Шрам, который эту двойную проверку поставил, никуда не делся и остаётся
    // охраняемым: дневная смена рождалась В РЕКИ своих же деревень — ~11 душ
    // в день на землю, где не стоит лагерь (замер 2026-08-31: 46 в море к
    // четвёртому дню). Один стоящий предикат для спавна и для шага.
    // ВОДУ СПРАШИВАЕМ У ПОРОГА РЕЛЬЕФА (M-212). Здесь стоял `biome_at_cell(...)
    // != Water`, то есть вопрос о КЛЕТКЕ задавался ПОЛЮ БИОМА — а поле есть
    // производное того же порога, испечённое позже. Два спеллинга совпадают
    // ровно пока мир дорождён: `biome_at_cell` отвечает `Water` и тогда, когда
    // поля нет вовсе (fail-closed), то есть на недопечённом мире объявляет
    // водой ВЕСЬ мир и останавливает спавн целиком.
    return !t.is_water(x, y);
}

struct XY { int x, y; };
XY find_valid_spawn(int cx, int cy, int radius, Rng& rng,
                    int mapW, int mapH, const TerrainData& terr,
                    int maxAttempts = 20) {
    for (int i = 0; i < maxAttempts; ++i) {
        int x = wrap_axis(cx + int(rng.next_u32() % std::uint32_t(radius * 2)) - radius, mapW);
        int y = wrap_axis(cy + int(rng.next_u32() % std::uint32_t(radius * 2)) - radius, mapH);
        if (is_land(terr, mapW, mapH, x, y)) return {x, y};
    }
    return {cx, cy};
}

// `levelOverride > 0` pins the level (quest spawns name their difficulty);
// the level draw is consumed either way, so the boot RNG stream is untouched.
// Returns the created entity (spawn_squad decorates it with roster/orders).
MacroHandle make_npc(MacroStore& st, NPCType type,
                     std::uint16_t factionIdx,
                     int x, int y, int mapW, int homeId, Rng& rng,
                     std::uint32_t& spawnIndex, int levelOverride = -1) {
    // 6.3 (M-106 1е): сквад рождается ТОЛЬКО колонками store — entt-моста
    // больше нет, единственная дверь рождения отвечает хэндлом.
    const MacroHandle h = store_birth(st);
    if (!st.valid(h)) return {};   // отказ капа уже прозвучал вслух
    st.cell[h.slot]   = ecs::MacroCell{ecs::cell_index(x, y, mapW)};
    st.visual[h.slot] = ecs::MacroVisual{float(x), float(y), 0.0f};
    st.kind[h.slot]   = ecs::NPCKind{std::uint16_t(type), factionIdx};

    const auto& def = npc_def(type);
    // The same draw the jittered hp used to consume, now used as the seed of the
    // sheet that decides it — so the boot RNG stream is untouched and worlds
    // generate as before, but there is no longer a THIRD way to compute what a
    // body is worth. `baseHp + rng % 15` was that third way (the subworld
    // derives hp from the character sheet at both of its births), and the two
    // scales disagreed enough that a macro lord's wound had to be converted to
    // reach his body. Reading the same row on both layers is what makes a wound
    // crossable at all (sub/spawn.h, the tracked form).
    const std::uint32_t sheetSeed = rng.next_u32();
    int lvl = def.baseLevel + int(rng.next_u32() % 4u);
    if (levelOverride > 0) lvl = levelOverride;
    const CharacterSheet sheet = make_character_sheet(type, lvl, sheetSeed);
    // Тело экземпляра — от маски СТРОКИ (M-183): какие слоты экипировки есть.
    gear_init(st.gear[h.slot].gear, def.slots);
    const int hp = body_max_hp(sheet, def.combat);

    // Stable identity for possession persistence (Inc 5e-2): the Nth macro NPC
    // created gets ordinal N. Deterministic because spawn_macro_npcs walks a
    // fixed spawn sequence seeded off `worldSeed`. Taken BEFORE the runtime
    // block below because the march caches derive from it.
    const std::uint32_t ordinal = spawnIndex++;

    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId   = homeId;   // ONE subject-ordinal space (M-37); 0 = без дома
    // ЛЕТУН (v93): кэш колонки строки по образцу travelRank — try_move
    // читает per-think, лукап строки там не нужен.
    rt.flying             = def.combat.cruiseM > 0.0f ? 1 : 0;
    rt.targetSettlementId = 0;
    rt.targetX            = float(x);
    rt.targetY            = float(y);
    rt.state              = std::uint8_t(NPCState::Idle);
    rt.stateTimer         = 0;
    rt.teleportCooldown   = 0;
    rt.visualSpeed        = 0.0f;
    rt.tickAccum          = std::uint32_t(rng.next_int(0, int(kAiTicks)));  // de-sync
    // The march caches (maxSp/travel/marathon/pace) come from the ORDINAL
    // sheet — the one every other consumer of "the leader as a sheet" derives
    // (leader_sheet_seed: auto-resolve, level-up recompute) — NOT from the
    // birth sheet above, whose seed is an unstored RNG draw. The two sheets
    // differ by seed only; hp stays with the birth sheet so the boot RNG
    // stream and every world stays byte-identical, and cross-layer state
    // travels as FRACTIONS (wound law, fatigue) so the seams never notice.
    // ALL THREE POOLS, built through the one «sheet → body» door (squad.h
    // refresh_body_from_sheet) with the ORDINAL sheet, THEN the hp/mp
    // ceilings overwritten from the BIRTH sheet above — that override is the
    // authored quirk of this site, stated here rather than hidden in the
    // door: hp stays with the birth sheet so the boot RNG stream and every
    // world stays byte-identical, and cross-layer state travels as FRACTIONS
    // (wound law, fatigue) so the seams never notice. Named fields, not a
    // positional list: this block grows, and a body born short of a bar is
    // the defect the pools landing exists to make impossible.
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt,
        make_character_sheet(type, lvl, leader_sheet_seed(ordinal)), type);
    pools.hp = pools.maxHp = hp;
    pools.mp = pools.maxMp = body_max_mp(sheet, npc_def(type).combat);
    pools.sp = pools.maxSp;   // born rested
    st.pools[h.slot]   = pools;
    st.runtime[h.slot] = rt;

    st.spawnId[h.slot] = ecs::MacroSpawnId{ordinal};

    // АНКЕТА ПО-ММОРПГ (владелец 2026-09-25, CANON): у КАЖДОГО сквада своя
    // анкета-данные, рождённая из шаблона; опыт и уровень идут в неё. Формула
    // — ровно та ординальная, что именные хранили, а транзиенты ДЕРИВИРОВАЛИ
    // на каждое чтение (sheet_of): хранение с рождения = байт в байт то же
    // поведение, дуализм owned/derived умер здесь.
    st.sheet[h.slot] =
        make_character_sheet(type, lvl, leader_sheet_seed(ordinal));

    // Every macro entity IS a squad; born alone, it is a squad of one and its
    // own leader (ecs::SquadRoster doctrine). Draws no RNG — streams untouched.
    // Roster/память/книга/приказы/гир/тег-анкеты/судьба уже обнулены
    // рождением слота (store_birth, единый X-список колонок).

    st.level[h.slot] = ecs::NpcLevel{std::int16_t(lvl)};

    // TS `makeNpc`: 1-2 trait rolls, duplicates skipped.
    ecs::NpcTraits traits{};
    const std::uint8_t traitRolls =
        std::uint8_t(1u + (rng.next_u32() % 2u));
    for (std::uint8_t i = 0; i < traitRolls; ++i) {
        const std::uint8_t raw = std::uint8_t(
            rng.next_u32() % std::uint32_t(NPCTrait::Count));
        bool duplicate = false;
        for (std::uint8_t j = 0; j < traits.count; ++j) {
            duplicate = duplicate || traits.traits[j] == raw;
        }
        constexpr std::uint8_t kMaxTraits =
            std::uint8_t(sizeof(traits.traits) / sizeof(traits.traits[0]));
        if (!duplicate && traits.count < kMaxTraits) {
            traits.traits[traits.count++] = raw;
        }
    }
    st.traits[h.slot] = traits;

    // СУМКА РОЖДАЕТСЯ ПУСТОЙ (M-139, вердикт владельца 2026-09-26). Здесь
    // стояли ДВЕ выдачи из воздуха: бросок хардкод-профиля лута по роли и
    // печать монет по колонкам `purseMin`/`purseMax`. Снесены обе вместе со
    // своими таблицами. Макро-энтити носит своё добро как СОСТОЯНИЕ — но
    // добро это приходит работой, обменом и, когда он придёт, РЕЖИССЁРОМ
    // ЛУТА по стоимости и контексту; с рождения у него ноль.
    ecs::NpcInventory bag{};
    st.inventory[h.slot] = bag;

    // Per-NPC visual identity (TS `generateNpcCharacter(type)` -
    // redesigned as a compact POD seed per relaxed translation policy).
    st.character[h.slot] = ecs::roll_npc_character(rng, 160);
    // ИМЯ АНКЕТЫ — колонка каждого сквада (вердикт 3, ход 2): рождение
    // рендерит дефолт генерации из names[nameIdx] один раз; дальше колонка
    // мутируема и читается ТОЛЬКО она (nameIdx — вход генератора, не имя).
    {
        const ecs::NpcCharacter& face = st.character[h.slot];
        const char* born = def.nameCount > 0
            ? def.names[face.nameIdx % def.nameCount] : def.label;
        std::snprintf(st.name[h.slot].text, sizeof st.name[h.slot].text,
                      "%s", born);
    }
    return h;
}

// A settlement's faction is its OWN column now (Landmark::factionIdx — owner
// 2026-09-11: «королевств нет, только фракции»); this helper is just the
// ownerless-ground fallback applied to it, so every consumer keeps one
// spelling of «whose place». It replaced the kingdomIdx indirection, which
// itself replaced two legacy hacks (a latitude-band heuristic and a
// first-letter matcher that dressed north-eastern towns in bandit colours).
std::uint16_t settlement_faction_index(const Landmark& lm) {
    return faction_or_freefolk(lm.factionIdx);
}

} // namespace


void spawn_macro_npcs(GameState& gs, ecs::World& w, MacroStore& st,
                      const TerrainData& terrain, std::uint32_t seed,
                      const DepositLayer* deposits) {
    Rng rng(seed + 7777u);
    // The ONE ordinal stream (v23) begins EARLIER now (§42 Инк 7): the
    // landmark genesis already issued identities to every born garrison
    // (state.cpp), so the boot spawn CONTINUES the counter instead of
    // resetting it — a reset here would re-deal the garrisons' names to
    // walkers. The counter rides the save; an identity is never issued
    // twice in a world's whole life.
    std::uint32_t& spawnIndex = gs.nextMacroSpawnOrdinal;
    const int mw = gs.mapW;
    const int mh = gs.mapH;
    if (mw <= 0 || mh <= 0)
        return;

    // Per-settlement spawns.
    std::vector<Landmark*> cities;
    for (auto& lm : gs.landmarks)
        if (lm.type == LandmarkType::City) cities.push_back(&lm);
    for (Landmark* cp : cities) {
        auto& s = *cp;
        const std::uint16_t fIdx = settlement_faction_index(s);

        // No eternal gatherers here any more (owner 2026-08-30, CANON S10):
        // working crews are TRANSIENT — raised from the population by the
        // daily labour rotation (npc_ai.h rotate_worker_squads), returned
        // to it at dusk. The genesis town wakes on day one and raises its
        // own hands.
        if (rng.next_f01() > 0.4f) {
            // Residents are born ON the town cell (owner 2026-08-31).
            make_npc(st, NPCType::Merchant, fIdx, s.x, s.y, gs.mapW, s.id,
                     rng, spawnIndex);
        }
        // ГЕНЕЗИСНЫЕ ОДИНОЧКИ-СТРАЖНИКИ (1-2 на город) ВЫРЕЗАНЫ 2026-09-22
        // по вердикту владельца («пока никаких стражников, это усложняет
        // систему»). Они и до того нарушали §42 Инк 7: именное тело идёт
        // через анкеты, массовое — через ростер места, а вечный одиночка
        // вне ротации не был ни тем ни другим. Строка `NPCType::Guard` в
        // каталоге тел жива — вырезан не вид, а то, что город его спавнит.
    }

    if (cities.empty()) return;
    const std::size_t nSet = cities.size();

    // ЗАСЕВ КАРАВАНОВ ВЫРЕЗАН 2026-09-21 вместе с родом NPCType::Caravan:
    // караван — это СКВАД, а не вид существа, и его обоз считает ростер
    // (сумма спин: люди плюс лошади), а не вписанные в породу 32 спины.
    // Торговый канал мира держат артели рейсами сбыта (ai_vendor).

    // БАНДИТСКИЙ ЗАСЕВ ВЫРЕЗАН 2026-09-21 (владелец: «вырезаем бандитов… щас
    // не до них»). Строка NPCType::Bandit в таблице существ остаётся — мир
    // просто перестал их рождать, пока экономика не станет безупречной
    // базой (problems.md §54: идти от минимума системы).

    // Witches: max(1, 0.1 * settlements)
    int witchCount = int(nSet / 10); if (witchCount < 1) witchCount = 1;
    for (int i = 0; i < witchCount; ++i) {
        auto& ref = *cities[rng.next_u32() % nSet];
        float angle = rng.next_f01() * 6.2831853f;
        int dist  = 25 + int(rng.next_u32() % 35u);
        int cx = wrap_axis(ref.x + int(std::lround(std::cos(angle) * dist)), mw);
        int cy = wrap_axis(ref.y + int(std::lround(std::sin(angle) * dist)), mh);
        auto p = find_valid_spawn(cx, cy, 15, rng, mw, mh, terrain);
        std::uint16_t f = rng.next_f01() > 0.3f
                        ? std::uint16_t(faction_index("magika")) : std::uint16_t(faction_index("cults"));
        make_npc(st, NPCType::Witch, f, p.x, p.y, gs.mapW, -1, rng, spawnIndex);
    }

    // Sorceresses: max(1, 0.05 * settlements)
    int sorcCount = int(nSet / 20); if (sorcCount < 1) sorcCount = 1;
    for (int i = 0; i < sorcCount; ++i) {
        auto& ref = *cities[rng.next_u32() % nSet];
        float angle = rng.next_f01() * 6.2831853f;
        int dist  = 30 + int(rng.next_u32() % 40u);
        int cx = wrap_axis(ref.x + int(std::lround(std::cos(angle) * dist)), mw);
        int cy = wrap_axis(ref.y + int(std::lround(std::sin(angle) * dist)), mh);
        auto p = find_valid_spawn(cx, cy, 15, rng, mw, mh, terrain);
        std::uint16_t f = rng.next_f01() > 0.5f
                        ? std::uint16_t(faction_index("magika")) : std::uint16_t(faction_index("cults"));
        make_npc(st, NPCType::Sorceress, f, p.x, p.y, gs.mapW, -1, rng, spawnIndex);
    }

    // Villages seed no eternal gatherers either (owner 2026-08-30): the
    // daily labour rotation raises every profession whose worksite is live
    // — the same find_worksite the working AI walks by, so presence of ore
    // IS still the presence of miners, just souls-deep and mortal.
    (void)deposits;

    // СТОЛ АНКЕТ — авторские фигуры мира, после массовки: их ординалы
    // продолжают тот же поток идентичности.
    spawn_design_characters(gs, w, st, terrain, rng, spawnIndex);
}

// Вершины горных массивов — дома драконьих анкет: K высочайших клеток
// карты с разносом ≥ 1/8 стороны (иначе три «вершины» — три камня одной
// горы). O(N) по клеткам, один проход генезиса; высота = красный канал
// terrain (та же власть, которой смотрит биом).
static void resolve_mountain_peaks(const TerrainData& terrain,
                                   int mapW, int mapH,
                                   XY* peaks, int peakCount) {
    for (int i = 0; i < peakCount; ++i) peaks[i] = {-1, -1};
    // Мир без рельефа (headless-фикстуры зовут генезис с пустым terrain)
    // вершин не имеет — тот же ответ, что у мира без гор.
    if (terrain.rgba.size() < std::size_t(mapW) * std::size_t(mapH) * 4u) {
        return;
    }
    const int minSpacing = std::max(8, mapW / 8);
    // Вершина обязана быть ГОРОЙ — порог из одной власти биома
    // (biomes.h kMountainBiomeLevel): мир без гор вершин не имеет, и
    // драконья строка честно не рождается.
    const int mountainFloor = int(kMountainBiomeLevel * kFieldWordMax);
    for (int slot = 0; slot < peakCount; ++slot) {
        int bestH = mountainFloor - 1, bx = -1, by = -1;
        for (int y = 0; y < mapH; ++y) {
            for (int x = 0; x < mapW; ++x) {
                const int h =
                    terrain.rgba[(std::size_t(y) * mapW + x) * 4u + 0];
                if (h <= bestH) continue;
                bool tooClose = false;
                for (int i = 0; i < slot; ++i) {
                    const int dx = std::abs(peaks[i].x - x);
                    const int dy = std::abs(peaks[i].y - y);
                    const int cheb = std::max(std::min(dx, mapW - dx),
                                              std::min(dy, mapH - dy));
                    if (cheb < minSpacing) { tooClose = true; break; }
                }
                if (tooClose) continue;
                bestH = h; bx = x; by = y;
            }
        }
        peaks[slot] = {bx, by};
    }
}

void spawn_design_characters(GameState& gs, ecs::World& w, MacroStore& st,
                             const TerrainData& terrain, Rng& rng,
                             std::uint32_t& spawnIndex) {
    const int mw = gs.mapW;
    const int mh = gs.mapH;
    if (mw <= 0 || mh <= 0) return;

    // Вершины резолвятся один раз на генезис — только если стол их просит.
    constexpr int kMaxPeaks = 8;
    XY peaks[kMaxPeaks];
    bool peaksResolved = false;
    for (std::int16_t ord = 0; ord < kDesignCharacterCount; ++ord) {
        const DesignCharacterDef& row = kDesignCharacterDefs[ord];

        // Дом из контекста мира: N-й ландмарк рода (homeIndex ≥ 0, заворот
        // по счёту) или случайный сидом (homeIndex < 0), ряд опционально
        // сужен префиксом фракции ландмарка («случайный варварский город»).
        // Мир без такого дома — без этой анкеты.
        int hx = row.cellX, hy = row.cellY;
        int homeId = 0;
        const Landmark* home = nullptr;
        if (row.homePeak) {
            // Дом — вершина горного массива (драконья строка): homeIndex-я
            // из высочайших с разносом. Мир без гор анкету не рождает —
            // тот же закон «нет дома — нет тела».
            if (!peaksResolved) {
                resolve_mountain_peaks(terrain, mw, mh, peaks, kMaxPeaks);
                peaksResolved = true;
            }
            const int slot =
                int(row.homeIndex) % kMaxPeaks >= 0
                    ? int(row.homeIndex) % kMaxPeaks : 0;
            if (peaks[slot].x < 0) continue;
            hx = peaks[slot].x;
            hy = peaks[slot].y;
        } else if (row.homeType != LandmarkType::None) {
            std::vector<const Landmark*> ofKind;
            for (const auto& lm : gs.landmarks) {
                if (lm.type != row.homeType) continue;
                if (row.homeFactionPrefix != nullptr) {
                    const char* fid = faction_id_for_index(
                        settlement_faction_index(lm));
                    if (std::strncmp(fid, row.homeFactionPrefix,
                                     std::strlen(row.homeFactionPrefix))
                        != 0) {
                        continue;
                    }
                }
                ofKind.push_back(&lm);
            }
            if (ofKind.empty()) continue;
            home = row.homeIndex >= 0
                ? ofKind[std::size_t(row.homeIndex) % ofKind.size()]
                : ofKind[rng.next_u32() % std::uint32_t(ofKind.size())];
            hx = home->x; hy = home->y; homeId = home->id;
        }
        const XY p = find_valid_spawn(hx, hy, 10, rng, mw, mh, terrain);

        // Фракция: строка стола, или — nullptr — фракция ДОМА: царь чужого
        // города был бы вторым ответом на «чей это человек».
        const std::uint16_t factionIdx = row.factionId != nullptr
            ? std::uint16_t(faction_index(row.factionId))
            : (home != nullptr
                   ? settlement_faction_index(*home)
                   : std::uint16_t(faction_index("freefolk")));
        const MacroHandle h = make_npc(
            st, row.body, factionIdx,
            p.x, p.y, gs.mapW, homeId, rng, spawnIndex, int(row.level));
        if (!st.valid(h)) continue;
        const std::uint16_t slot = h.slot;

        // АНКЕТА ПО-ММОРПГ: колонка уже рождена ординальной формулой в
        // make_npc; авторские числа строки ПЕРЕКРЫВАЮТ её, и полосы/марш-
        // кэши пересобираются от них через ту же дверь, что у всех.
        if (row.authoredSheet) {
            st.sheet[slot] = row.sheet;
            refresh_body_from_sheet(st.pools[slot], &st.runtime[slot],
                                    effective_sheet_of(st, handle_at(st, slot)),
                                    row.body);
            // Рождение целым — как make_npc рождает всех.
            st.pools[slot].hp = st.pools[slot].maxHp;
            st.pools[slot].mp = st.pools[slot].maxMp;
            st.pools[slot].sp = st.pools[slot].maxSp;
        }
        st.designTag[slot] = ecs::DesignCharacterTag{ord};

        // Логово — дом-клетка модели вылетов: вершина и есть его дом.
        if (row.homePeak) {
            st.runtime[slot].lairX = std::int16_t(hx);
            st.runtime[slot].lairY = std::int16_t(hy);
        }

        // Маршрут «дом ↔ ближайший ландмарк рода из агенды» — резолв
        // контекста, как find_valid_spawn: строка называет РОД цели, мир
        // называет клетки. Наличие маршрута И ЕСТЬ приказ (лестница
        // effective_behaviour, ступень 1).
        if (row.agenda.routeToNearest >= 0 && home != nullptr) {
            const Landmark* best = nullptr;
            float bestD = 0.0f;
            for (const auto& lm : gs.landmarks) {
                if (std::int8_t(lm.type) != row.agenda.routeToNearest)
                    continue;
                const float d = torus_dist_sq(float(home->x), float(home->y),
                                              float(lm.x), float(lm.y),
                                              float(mw), float(mh));
                if (!best || d < bestD) { best = &lm; bestD = d; }
            }
            if (best) {
                ecs::SquadOrders orders{};
                orders.waypointCount = 2;
                orders.waypoints[0] = std::int16_t(home->x);
                orders.waypoints[1] = std::int16_t(home->y);
                orders.waypoints[2] = std::int16_t(best->x);
                orders.waypoints[3] = std::int16_t(best->y);
                st.orders[slot] = orders;
            }
        }
    }
}

bool spawn_npc_at(GameState& gs, ecs::World& w, MacroStore& st,
                  const TerrainData& terrain,
                  const char* typeToken, int x, int y, int level) {
    NPCType type{};
    if (!npc_type_from_label(typeToken, type)) return false;
    if (gs.mapW <= 0 || gs.mapH <= 0) return false;

    // Deterministic from the world seed and the named cell — independent of
    // when in the session the event arrives.
    Rng rng(hash3(std::uint32_t(x), std::uint32_t(y),
                  gs.worldSeed ^ 0x51AE57u));
    const XY p = find_valid_spawn(wrap_axis(x, gs.mapW), wrap_axis(y, gs.mapH),
                                  6, rng, gs.mapW, gs.mapH, terrain);

    // The possession-identity ordinal (MacroSpawnId) comes from the ONE
    // persistent counter (v23). The old max-over-living scan reissued a dead
    // NPC's ordinal — the 19.24 hole; runtime spawns now persist in the macro
    // snapshot, so the identity has to be for life.

    // Faction: an overworld-aggressive type is an outlaw ("bandits", exactly
    // like the boot spawner's bandit pool); every civil type belongs to the
    // realm whose LAND it stands on — the same "земля решает" rule the
    // subworld spawner uses.
    const std::uint16_t f = npc_def(type).ai == AIBehaviour::Aggressive
        ? std::uint16_t(faction_index("bandits"))
        : faction_index_for_cell(gs.cellOwner, gs.mapW, gs.mapH, p.x, p.y);

    return st.valid(make_npc(st, type, f, p.x, p.y, gs.mapW, /*homeId*/ 0,
                             rng, gs.nextMacroSpawnOrdinal, level));
}

MacroHandle spawn_squad(GameState& gs, MacroStore& store,
                        const TerrainData& terrain, const SquadSpec& spec) {
    if (gs.mapW <= 0 || gs.mapH <= 0) return {};

    // Deterministic from the world seed and the named cell, like spawn_npc_at.
    Rng rng(hash3(std::uint32_t(spec.x), std::uint32_t(spec.y),
                  gs.worldSeed ^ 0x50AD5EEDu));
    // Born ON the named cell (owner 2026-08-31: «пусть все рождаются именно
    // на клетке ландмарка») — a landmark is dry by construction, so a crew
    // raised by one starts exactly where its home stands. The scatter
    // fallback survives only for a WET spec cell (a blood-field band whose
    // field peak lies on a river).
    const XY named{wrap_axis(spec.x, gs.mapW), wrap_axis(spec.y, gs.mapH)};
    const XY p = is_land(terrain, gs.mapW, gs.mapH, named.x, named.y)
        ? named
        : find_valid_spawn(named.x, named.y, 4, rng, gs.mapW, gs.mapH,
                           terrain);

    // Ordinals from the ONE persistent counter (v23) — the max-over-living
    // scan and its 19.24 reuse hole are gone.
    //
    // ФРАКЦИЯ ЖИТЕЛЯ — СОБСТВЕННИК ЕГО ЛАНДМАРКА (CANON S24, владелец
    // 2026-09-02: «у каждого ландмарка уже есть собственник»): артель носит
    // фракцию ДОМА — теперь это собственная колонка ландмарка
    // (Landmark::factionIdx, королевства вырезаны 2026-09-11). Клеточный
    // резолвер на границах одевал деревню и её же город в воюющие фракции —
    // крестьяне резали крестьян на общей дороге (измерено, [death-1299]
    // сид 7). «Земля решает» остаётся правилом БЕЗДОМНЫХ и контекстных
    // спавнов.
    const Landmark* home = landmark_by_id(gs, spec.homeSettlementId);
    const std::uint16_t f = spec.factionIndex >= 0
        ? std::uint16_t(spec.factionIndex)
        : home ? settlement_faction_index(*home)
               : faction_index_for_cell(gs.cellOwner, gs.mapW, gs.mapH, p.x, p.y);

    const MacroHandle leader =
        make_npc(store, spec.leaderType, f, p.x, p.y, gs.mapW,
                 spec.homeSettlementId, rng, gs.nextMacroSpawnOrdinal,
                 spec.leaderLevel);
    if (!store.valid(leader)) return {};

    // The roster rows — through the same append every other producer uses:
    // души встают в область существ ЕДИНОГО контейнера лидера (M-71),
    // генерик-заявка — стаком, душа с историей — записью.
    auto& bag = store.inventory[leader.slot];
    for (const SquadSpecMembers::Stack& st : spec.members) {
        if (!valid_npc_kind(st.rec.kind)) continue;
        const bool ok = st.rec.entityId == 0
            ? creatures_push_stack(bag.inv,
                                   soldier_npc_type(st.rec.kind),
                                   st.rec.level, st.n)
            : creatures_push(bag.inv, st.rec);
        if (!ok) break;   // the ceiling refuses out loud
    }
    // The squad's carry is the SUM of its backs (CANON S10, literal: «берёт
    // по своей грузоподъёмности — СУММА ЛИСТОВ ЧЛЕНОВ») — through THE door
    // (squad.h refresh_squad_carry), which weighs every soul by ITS OWN row's
    // haulMult column. The inline `*= 1 + size` that stood here counted heads
    // and was computed once: a horse in the roster added nothing, and any
    // later добор/ссадка/дезертирство left the number lying.
    refresh_squad_carry(store, leader);

    // A route, only if the spec actually gives one (opt-in like the
    // component; its presence is the order).
    if (spec.waypointCount > 0) {
        ecs::SquadOrders orders{};
        orders.waypointCount = std::uint8_t(
            std::min<int>(spec.waypointCount, 8));
        orders.waypoints = spec.waypoints;
        store.orders[leader.slot] = orders;
    }
    return leader;
}


} // namespace sm
