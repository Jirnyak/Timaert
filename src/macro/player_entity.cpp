#include "macro/player_entity.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "macro/entry_context.h"
#include "macro/landmark_iter.h"   // for_each_place — места по слотам
#include "tables/faction.h"
#include "tables/npc.h"
#include "macro/spell_book_state.h"
#include "macro/anketa.h"
#include "macro/squad.h"
#include "macro/store.h"
#include <algorithm>
#include <array>

namespace sm {

// (PlayerSquadCache и find_player_squad умерли в 1е кластере 5: ответ «кто
// оригинал» стал распаковкой `gs.playerSquadBits` — кэшировать распаковку
// поля нечего, а скан остался ровно один, на границе резолва ниже, где биты
// ещё не назначены.)

namespace {
// ЕДИНСТВЕННЫЙ СКАН «ГДЕ ЕГО ДОМ» — по КОЛОНКЕ, и только на границе (буте и
// загрузке), где кэш-биты ещё не назначены или протухли. До M-255 этот же
// скан шёл по ЗАРЕЗЕРВИРОВАННОМУ ОРДИНАЛУ-ЛИТЕРАЛУ, то есть у вопроса «кто
// оригинал» носителем было магическое число, а не колонка анкеты.
// kMacroNoSlot — «дома нет вовсе» (мир ещё не родил игрока).
MacroHandle find_player_home_column(const MacroStore& st) {
    for (std::size_t s = 0; s < kUnifiedCap; ++s) {
        if (!st.alive[s] || st.playerFlag[s].home == 0) continue;
        return handle_at(st, std::uint16_t(s));
    }
    return MacroHandle{};
}

// СИД ЛИЦА ИГРОКА ПО УМОЛЧАНИЮ — СВОЁ ЧИСЛО, А НЕ ЧЕЙ-ТО ОРДИНАЛ (M-255).
// Вывод: он ОДИН И ТОТ ЖЕ ЧЕЛОВЕК В ЛЮБОМ МИРЕ, пока экран создания не
// перерисует его, — значит лицо не вправе зависеть ни от сида мира, ни от
// выданного ему эмитентом ординала. Отсюда константа, а не ролл.
//
// Значение ЗАМОРОЖЕНО СОЗНАТЕЛЬНО: это ровно `0x7FFFFFFF ^ 0x9E3779B9`, то
// есть то, чем лицо сеялось, пока «кто оригинал» был литералом-ординалом.
// Сид был ПОБОЧНЫМ ЖИТЕЛЕМ того литерала, и снос ординала без этой строки
// молча перерисовал бы персонажа — дефект, которого игрок не заказывал.
// Само число — контент (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ: «все числа это баланс»),
// и менять его можно молча; нельзя — ВЫВОДИТЬ его из чужой идентичности.
constexpr std::uint32_t kPlayerDefaultFaceSeed = 0xE1C88646u;
static_assert(kPlayerDefaultFaceSeed == (0x7FFFFFFFu ^ 0x9E3779B9u),
              "сид лица игрока обязан остаться тем, чем был до сноса "
              "kPlayerSquadOrdinal — иначе снос перерисовал персонажа");
} // namespace

void ensure_macro_player_entity(GameState& gs, MacroStore& st) {

    // ── The player's squad: an ORDINARY macro squad ────────────────────────
    // Owner's ruling, 2026-08-27: «игрок = обычный сквад, просто с флажком
    // игрока». It used to be a bare husk recreated every macro tick, while
    // the real squad lived beside it as PlayerState::army — a second kind of
    // squad with its own projection into the subworld, its own auto-battle
    // side, its own casualty path and its own (absent) cap. Four
    // player-specific paths, which CANON S4 forbids by name.
    //
    // «Кто оригинал» — хэндл GameState; протухший после загрузки или пустой
    // на буте хэндл резолвится КОЛОНКОЙ ОДИН раз (граница, не тик: валидный
    // хэндл — чтение без скана). Ревалидация спрашивает ту же колонку:
    // бит дома на том слоте, на который хэндл показывает.
    MacroHandle home = player_squad_handle(gs);
    if (!st.valid(home) || st.playerFlag[home.slot].home == 0) {
        home = find_player_home_column(st);
    }
    if (home.id == 0) {
        // THE spawn cell, derived from the world itself (подпосадка 4 — no
        // position scalar exists to seed from): the realm's first city, the
        // map centre when the world has none. A LOADED world never reaches
        // this branch — the snapshot restores his squad whole.
        int sx = gs.mapW / 2, sy = gs.mapH / 2;
        // ПЕРВЫЙ ГОРОД ПОПУЛЯЦИИ (ломтик F): места рождаются в порядке плана
        // генератора, и слоты выдаются по возрастанию, поэтому первый
        // City-слот и есть тот город, который здесь стоял списком
        // `politik.cities[0]`. Второго списка городов у мира больше нет —
        // план умер вместе с генезисом.
        bool cityFound = false;
        for_each_place(st, [&](std::uint16_t slot) {
            if (cityFound) return;
            if (SquadType(st.runtime[slot].squadType) != SquadType::City)
                return;
            sx = ecs::cell_x(st.cell[slot], gs.mapW);
            sy = ecs::cell_y(st.cell[slot], gs.mapW);
            cityFound = true;
        });
        const MacroHandle h = store_birth(st);
        if (!st.valid(h)) return;   // отказ капа уже прозвучал вслух
        // ОРДИНАЛ ВЫДАЛА ДВЕРЬ РОЖДЕНИЯ, КАК ВСЯКОМУ СКВАДУ (M-255/M-220):
        // здесь стоял литерал 0x7FFFFFFF, делавший игрока единственной
        // сущностью мира с идентичностью-литералом; ручная запись spawnId
        // умерла вместе с эмитентом GameState.
        st.cell[h.slot]    = ecs::MacroCell{ecs::cell_index(sx, sy, gs.mapW)};
        st.sub[h.slot]     = {};   // рождение — центр клетки (M-244)
        st.kind[h.slot]    = ecs::NPCKind{
            std::uint16_t(NPCType::Adventurer),
            std::uint16_t(faction_index(kPlayerFactionId))};
        // His OWNED sheet, born WITH the body like every named character's
        // (посадка Б, v91) — the default creation-screen build (the same
        // default_* trio default_player used to copy into PlayerState);
        // apply_creation overwrites it through player_sheet() right after
        // boot. On a LOADED world this branch is never reached: the snapshot
        // restores the record whole (hasSheet, v90).
        CharacterSheet birth{};
        birth.attributes = default_attributes();
        birth.skills     = default_skills();
        birth.levelData  = default_level_data();
        st.sheet[h.slot] = birth;
        // Тело игрока — маска строки Adventurer (M-183).
        gear_init(st.gear[h.slot].gear, npc_def(NPCType::Adventurer).slots);
        const CharacterSheet& sheet = st.sheet[h.slot];
        ecs::Pools& pools = st.pools[h.slot];
        {
            Rng faceRng(kPlayerDefaultFaceSeed);
            st.character[h.slot] = ecs::roll_npc_character(faceRng);
        }
        // His book, born WITH the body like every squad's (v89) — with the
        // starter spell the old PlayerState default carried (state.cpp).
        // Память/контейнер/сумка/черты уже обнулены рождением слота.
        spellbook_learn(st.spellBook[h.slot], spell_ordinal("magic_bolt"));
        // The march caches come from the player's OWN sheet, through the same
        // door every leader's do.
        ecs::MacroNpcRuntime rt{};
        rt.targetSettlementId = 0;
        rt.targetX = float(sx);
        rt.targetY = float(sy);
        rt.state = std::uint8_t(NPCState::Idle);
        {
            // One assembly of what stands on him, used for both halves: the
            // sheet copy (attr/skill cells) and the derived cells the cache
            // door reads past it (MovePct/CarryKg). Through the universal
            // doors (squad.h) — he is their ordinary case.
            const BonusTotals bt = standing_bonuses_of(st, h);
            refresh_body_from_sheet(pools, &rt,
                                    effective_sheet(sheet, bt),
                                    NPCType::Adventurer, &bt);
        }
        // Born whole — creation is a moment that SAYS it heals. Every bar,
        // not a subset: a subset is exactly how mana stayed private property.
        pools.hp = pools.maxHp;
        pools.mp = pools.maxMp;
        pools.sp = pools.maxSp;
        st.runtime[h.slot] = rt;
        home = h;
    }
    // ДОМ СТАВИТСЯ ОДНОЙ ДВЕРЬЮ (M-255): колонка + кэш одним движением,
    // ровно как флажок выше. Звонок на уже отмеченном слоте идемпотентен,
    // невалидный хэндл — no-op (кэш не сжигается о протухшие биты).
    mark_player_home(st, gs.playerSquadRef, home);

    // ── No position projection any more ──────────────────────────────────
    // The MacroCell of this record IS where he stands (подпосадка 4): the
    // walker steps it, the jump door writes it, the snapshot restores it.
    //
    // ЗДЕСЬ СТОЯЛО ЗЕРКАЛО УРОВНЯ В КОЛОНКУ `ecs::NpcLevel` (M-246а) — оно
    // переписывало вывод из анкеты на КАЖДОМ звонке этой двери, то есть и
    // после загрузки. Колонки больше нет: уровень игрока читается там же,
    // где уровень всякого сквада, — `squad_level` над его анкетой.

    // ── The flag ──────────────────────────────────────────────────────────
    // Exactly one flag holder exists at a time — «кем я на карте» — and it
    // is MACRO ONLY since the scale split (2026-09-10): his own squad by
    // default, a possessed lord while he wears one (the scene body carries
    // AvatarTag, a different question). Истина — колонка playerFlag анкеты
    // (5б); носитель, не переживший мир (невалидные биты), честно
    // складывается домой ДВЕРЬЮ переноса — мира без флага не бывает.
    if (!st.valid(player_flag_handle(gs))) {
        transfer_player_flag(st, gs.playerFlagRef, home);
    }

    // ЗДЕСЬ СТОЯЛ БЕЗУСЛОВНЫЙ ПЕРЕСЧЁТ ЛИСТА КАЖДЫЙ МАКРО-ТИК, И ОН СНЕСЁН
    // (M-301). Эта дверь зовётся из главного цикла каждым макро-тиком вне
    // субмира, так что звонок был ЧЕТВЁРТЫМ ответом на «свежа ли производная
    // анкеты» — и самым откровенным: его прежняя шапка прямо называла себя
    // страховкой за забытые звонки («a sheet-change moment anywhere that
    // forgets its own refresh_player_body call heals within one macro tick
    // instead of drifting forever»).
    //
    // Страховка была и неполной, и маскирующей. НЕПОЛНОЙ — под землёй эта
    // дверь не зовётся вовсе, значит внизу забытый звонок не лечился ничем.
    // МАСКИРУЮЩЕЙ — именно она делала невидимыми три класса изменений, у
    // которых пересчёта не было ни одного (надевание, квестовый опыт,
    // гашение сустейна): наверху дефект сам заживал через тик, и ни один
    // свидетель его не видел.
    //
    // Теперь пересчёт живёт ВНУТРИ дверей классов изменений
    // (`refresh_squad_body@src/macro/squad.h` и её звонящие), поэтому
    // страховать нечего: место, где анкета изменилась, и место, где она
    // пересчитана, — одно и то же место.
}

bool wake_player_in_original_body(GameState& gs, MacroStore& st) {
    const MacroHandle flag = player_flag_handle(gs);
    const MacroHandle home = player_squad_handle(gs);
    // He was never wearing anyone — the man who died is himself. Оба хэндла
    // обязаны быть живыми: осиротевший флаг чинит ensure, не wake.
    if (!st.valid(flag) || !st.valid(home)
        || gs.playerFlagRef == gs.playerSquadRef) return false;
    // ВОЗВРАЩАТЬСЯ ЕСТЬ КУДА, ТОЛЬКО ПОКА ЖИВ ОРИГИНАЛ. Asked of the record
    // the same way the world asks it of every other body — the dead column
    // the reaper stamps, and the bars themselves, because a body can be at
    // zero for a tick before anything marks it.
    if (st.dead[home.slot] != 0) return false;
    if (st.pools[home.slot].hp <= 0.0f) return false;
    // One displacement of one flag — вселение, проигранное назад, той же
    // дверью (колонка + кэш одним движением; sub/possess.h ходит ею же).
    transfer_player_flag(st, gs.playerFlagRef, home);
    return true;
}

void resolve_player_handles_after_load(GameState& gs, MacroStore& st) {
    // ОДИН СКАН КОЛОНКИ, ДВА БИТА (5б + M-255): истина ОБОИХ вопросов
    // приехала колонкой playerFlag записей снапшота, оба кэша GameState
    // пересобираются из неё. Дом искался здесь ВТОРЫМ проходом по store —
    // сканом по зарезервированному ординалу-литералу; теперь он бит рядом.
    MacroHandle flag{}, home{};
    std::uint32_t flagHolders = 0, homeHolders = 0;
    for (std::size_t s = 0; s < kUnifiedCap; ++s) {
        if (!st.alive[s]) continue;
        if (st.playerFlag[s].on != 0) {
            ++flagHolders;
            flag = handle_at(st, std::uint16_t(s));
        }
        if (st.playerFlag[s].home != 0) {
            ++homeHolders;
            home = handle_at(st, std::uint16_t(s));
        }
    }
    // Порченый файл не рождает ни мира с двумя домами, ни безфлажного мира,
    // ни двух игроков: колонка чистится, носитель каждого бита остаётся ОДИН.
    if (homeHolders > 1) {
        for (std::size_t s = 0; s < kUnifiedCap; ++s)
            st.playerFlag[s].home = 0;
        if (st.valid(home)) st.playerFlag[home.slot].home = 1;
    }
    if (flagHolders != 1) {
        // Флаг честно складывается домой — мира без флага не бывает.
        for (std::size_t s = 0; s < kUnifiedCap; ++s)
            st.playerFlag[s].on = 0;
        flag = home;
        if (st.valid(home)) st.playerFlag[home.slot].on = 1;
    }
    gs.playerSquadRef = home;
    gs.playerFlagRef  = flag;
}

// ── ВСЁ НИЖЕ СПРАШИВАЕТ ФЛАЖОК, А НЕ ОРДИНАЛ (2026-09-14) ────────────────
//
// Девять дверей: лист, эффективный лист, сумка, spCarry, полосы, книга,
// память, refresh. До этого дня каждая шла к «записи с зарезервированным
// номером», то есть к ТВОЕМУ РОДНОМУ ТЕЛУ, всегда. Это и был корень: вопрос
// «чьи это полосы» имел ответ «мои», даже когда ты стоял в чужом теле, и
// потому выпитое в теле лорда зелье лечило оставленную оболочку
// (problems.md §48).
//
// Теперь они распаковывают `gs.playerFlagBits` — «тот, на ком флажок». Ты
// ПОЛНОСТЬЮ тот, в чьём теле стоишь: его лист, его люди, его сумка, его
// книга (вердикт владельца 2026-09-14 — «вся семья»).
//
// `player_squad_handle` остаётся ординальной семантикой — и это не
// исключение, а другой вопрос: «КТО ОРИГИНАЛ», обратный адрес одержимости
// (wake_player_in_original_body выше).

namespace {
// Один резолв на все колоночные двери: валидный слот носителя флажка или
// kMacroNoSlot — «мира нет», и дверь честно отвечает nullptr.
inline std::uint16_t flag_slot_or_none(const GameState& gs,
                                       const MacroStore& st) {
    const MacroHandle h = player_flag_handle(gs);
    return st.valid(h) ? h.slot : kMacroNoSlot;
}
} // namespace

CharacterSheet* player_sheet(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.sheet[slot];
}
const CharacterSheet* player_sheet(const GameState& gs,
                                   const MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.sheet[slot];
}

CharacterSheet player_effective_sheet(const GameState& gs,
                                      const MacroStore& st) {
    const MacroHandle h = player_flag_handle(gs);
    if (!st.valid(h)) return CharacterSheet{};
    return effective_sheet_of(st, h);
}

Inventory* player_inventory(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.inventory[slot].inv;
}
const Inventory* player_inventory(const GameState& gs,
                                  const MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.inventory[slot].inv;
}

float* player_sp_carry(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.pools[slot].spCarry;
}

ecs::Pools* player_pools(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.pools[slot];
}
const ecs::Pools* player_pools(const GameState& gs, const MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.pools[slot];
}

// (Тело `refresh_player_body` снесено — M-301; см. некролог в шапке. Строка
//  существа, которую она читала НЕЗАЩИЩЁННО (`NPCType(kind.type)`), спрашивается
//  теперь дверью `squad_row_type@src/macro/squad.h`.)

SpellBook* player_spellbook(const GameState& gs, MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.spellBook[slot];
}
const SpellBook* player_spellbook(const GameState& gs,
                                  const MacroStore& st) {
    const std::uint16_t slot = flag_slot_or_none(gs, st);
    return slot == kMacroNoSlot ? nullptr : &st.spellBook[slot];
}

} // namespace sm
