// THE faction registry — the single source of truth for every faction in the
// game: identity (id / name / description / colour), starting
// player reputation, and the default relation between any two factions.
//
// WHY THIS EXISTS (owner decision, 2026-07-30: "единая система истины, никаких
// разделений на королевства"). Before this header the faction system was split
// across FIVE parallel vocabularies, each hand-maintained and each subtly wrong:
//   • npc_faction_id_for   (sub/engine.cpp,  idx→id, 5 entries)
//   • faction_id_for_idx   (macro/npc_spawn.cpp — the SAME table duplicated,
//     with a DIFFERENT fallback: "" in one copy, "empire" in the other)
//   • fauna_faction_id_for (sub/engine.cpp,  idx→id, 4 entries, different ids
//     for the same indices — so ecs::NPCKind.factionIdx meant different
//     factions depending on a type bit, and spell friendly-fire compared the
//     raw indices across that boundary: a bandit NPC (3) and a bandit creature
//     (2) counted as enemies, a bandit NPC (3) and a DEMON (3) as brothers)
//   • FaunaFaction         (sub/fauna.h enum, a fourth spelling)
//   • faction_idx          (macro/npc_spawn.cpp, id→idx by FIRST LETTER —
//     "barbarians" and "bandits" both mapped to 3, so barbarian settlements
//     spawned bandit-faction guards)
// plus a registry split in two (kUniversalFactions + kingdom_defs — with
// "magika" emitted by the vocabularies but never registered, so all its
// relations silently read neutral) and relations decided by an if-chain over
// id strings and kingdom lineages.
//
// All of that collapses to this one table and one matrix:
//   • ONE row per faction — kingdoms are ordinary rows, not a separate class.
//   • ONE index space: ecs::NPCKind.factionIdx is an index into kFactionDefs,
//     for humanoids and monsters alike. 0xFFFF (kNoFaction) = factionless.
//   • ONE relation source: the matrix (relations.h), born NEUTRAL — politics
//     is cut until the core is playable (see the block below); a faction's
//     reputation column still seeds how the world meets the PLAYER.
//
// Header-only POD tables in .rodata; no allocation, no exceptions, no init
// order. The player side is deliberately NOT a row: the player's standing with
// every faction is the reputation map (dynamic per save), and combat resolves
// it through one callback — see SubworldEngine::battle_relation_callback.
#pragma once
#include <cstdint>
#include "macro/interests.h"   // kRelationMin/Max — ОДНА шкала отношений
#include <cstring>

namespace sm {

// THE hostility line: a relation below this is an enemy. One number for every
// consumer — the subworld battle masks (SubworldEngine::battle_relation_
// callback), the subworld aggro checks (sub/ai.h re-exports it), and the macro
// squad threat step (npc_ai.cpp) — so the map and the ground can never
// disagree about who is at war. It lives HERE because relations live here.
// The predicate that applies it is factions_hostile (macro/state.h) — apply
// the threshold through it, not by hand.
// ПОРОГИ — СТЕПЕНИ ДВОЙКИ (владелец, 2026-09-21: «сделай эти пороги 64 32 —
// вдруг там магией духа машины мы его удобрим степенями двойки»). Это не
// украшение: шкала кончается на 127 (CANON S26, закон диапазона), и половина
// её — 64, четверть — 32. Прежние −50 и 50 были круглыми десятичными, то есть
// не делили шкалу ни на что; теперь враждебность это РОВНО половина плеча, а
// союз — четверть, и обе границы выводятся из ширины байта, а не назначаются.
inline constexpr int kHostileThreshold = -64;

// THE friendship line, the other end of the same scale: at or above this a
// faction is an ALLY — it cannot be provoked into a private grudge by a stray
// hit (maybe_flip_temp_hostile), and the stance colours saturate to full
// friend here. Lived as a private constant in engine.cpp while the hostile
// line lived here; the two ends of one scale belong on one shelf.
inline constexpr int kAllyRepThreshold = 32;

// THE price of a kill the world holds against you (a life is a life, whoever
// swung). It lived privately in sub/engine.cpp while only the subworld reaper
// charged it; the auto-resolve charges it now too (damage-door Inc 6), and
// two copies of one price is exactly how the two layers drift apart.
inline constexpr int kKillRepPenalty = -1;

// ── The registry ───────────────────────────────────────────────────────────
struct FactionDef {
    const char*   id;               // stable machine id — THE universal key
    const char*   name;
    const char*   description;
    std::uint32_t color;            // 0xRRGGBB
    // Killing a member of this faction is NOT a crime — no reputation is lost
    // for it (damage-door track Inc 5). Beasts, outlaws and the abyss: nobody
    // mourns them, nobody avenges them by law. It was a strcmp if-chain over
    // three ids inside the subworld reaper — a fourth such faction had to be
    // added in CODE, in a file that has no business knowing who the outlaws
    // are. Now it is this column, and the reaper asks the registry.
    bool          killIsNoCrime = false;
    // The THREE coins this faction trades in — item ids of the one catalog,
    // copper→silver→gold order (nominals 1/10/100 — owner verdict №1 of the
    // second audit, 2026-09-17: «по 3 типа фракционных монет… просто дата в
    // таблицу итемов»). Realms with a mint of their own name them; culture
    // groups name the realm's family they fold onto (the Magica splinters
    // and the Lake Duchy strike the Magika sigils). Empty = no mint →
    // imperial family, the de-facto reserve currency of v1. Nothing about a
    // coin is a mechanic: each id is a plain catalog row whose composition
    // ({металл 1} → 32) IS the mint (CANON S10).
    const char*   mint[3] = {"", "", ""};
};

// Sentinel for "no faction" in ecs::NPCKind.factionIdx: resolves to the empty
// id, which every relation path already treats as neutral / fights-nobody.
inline constexpr std::uint16_t kNoFaction = 0xFFFFu;

// ЛИМИТ МИРА: 64 фракции ЕДИНОЙ КОНСТАНТОЙ (владелец 2026-09-03, CANON S10:
// «у нас был уже кап естественный универсальный для фракций вроде 64 — пиши
// в канон и просто как единая константа»). Естественный он потому, что
// враг-маска фракции — один std::uint64_t (sub/movement.h kMaxCrowdFactions
// ссылается сюда); поля следов (scent_field.h) аллоцируются по kFactionCount
// и упираются в этот же потолок. 65-я строка реестра — осознанное решение
// о ширине маски, не тихий рост.
inline constexpr int kMaxFactions = 64;

// THE player's faction id — one spelling for the whole project. It names an
// ordinary registry row (see the "player" entry below), so it interns, resolves
// and compares exactly like every other faction; nothing about the player is a
// special case in the relation algorithm.
inline constexpr const char* kPlayerFactionId = "player";

inline constexpr FactionDef kFactionDefs[] = {
    // ── Universal factions ────────────────────────────────────────────────
    {"wildlife", "Wildlife",
     "Beasts and roaming creatures. Indifferent to mortal politics.",
     0x6b8e23, /*killIsNoCrime*/true},
    {"bandits",  "Bandit Clans",
     "Outlaws and raiders. Hostile to all civilised folk.",
     0x7a3a1a, /*killIsNoCrime*/true},
    {"demons",   "Demonic Hordes",
     "Forces of the abyss. War against everything.",
     0x8b0000, /*killIsNoCrime*/true},
    {"cults",    "Demonic Cults",
     "Worshippers of the Old Ones. Hunted everywhere.",
     0x581c87, false, {"coin_magika_copper", "coin_magika_silver", "coin_magika_gold"}},
    // The wandering mage orders — previously emitted by the spawn vocabulary
    // but never registered, so every relation involving them silently read
    // neutral. A real faction now, closing that gap by construction.
    {"magika",   "Magika Orders",
     "Itinerant mages sworn to no single realm.",
     0x8b5cf6, false, {"coin_magika_copper", "coin_magika_silver", "coin_magika_gold"}},
    // ── Kingdoms — ordinary rows; politik references them by id ───────────
    {"old_magica",      "Old Magica",
     "Ruled by powerful mages. High magic economy.",
     0xa78bfa, false, {"coin_magika_copper", "coin_magika_silver", "coin_magika_gold"}},
    {"northern_magica", "Northern Magica",
     "Ruled by powerful mages. High magic economy.",
     0x7c3aed, false, {"coin_magika_copper", "coin_magika_silver", "coin_magika_gold"}},
    {"lower_magica",    "Lower Magica",
     "Ruled by powerful mages. High magic economy.",
     0xc4b5fd, false, {"coin_magika_copper", "coin_magika_silver", "coin_magika_gold"}},
    {"lake_duchy",      "Lake Duchy",
     "Ruled by powerful mages. High magic economy.",
     0x60a5fa, false, {"coin_magika_copper", "coin_magika_silver", "coin_magika_gold"}},
    {"empire",          "Empire of Light",
     "Theocratic empire. Magic is forbidden.",
     0xf59e0b, false, {"coin_empire_copper", "coin_empire_silver", "coin_empire_gold"}},
    {"timaert",         "Republic of Timaert",
     "Maritime trade republic. Neutral and wealthy.",
     0x10b981, false, {"coin_timaert_copper", "coin_timaert_silver", "coin_timaert_gold"}},
    {"barbarian_north", "North Barbarians",
     "Feudal lords ruling by might and steel.",
     0x991b1b, false, {"coin_barbar_copper", "coin_barbar_silver", "coin_barbar_gold"}},
    {"barbarian_south", "South Barbarians",
     "Feudal lords ruling by might and steel.",
     0xb91c1c, false, {"coin_barbar_copper", "coin_barbar_silver", "coin_barbar_gold"}},
    {"barbarian_west",  "West Barbarians",
     "Feudal lords ruling by might and steel.",
     0xdc2626, false, {"coin_barbar_copper", "coin_barbar_silver", "coin_barbar_gold"}},
    {"barbarian_east",  "East Barbarians",
     "Feudal lords ruling by might and steel.",
     0xef4444, false, {"coin_barbar_copper", "coin_barbar_silver", "coin_barbar_gold"}},
    // ── The unruled ───────────────────────────────────────────────────────
    // Everyone who answers to no crown: a settlement no faction owns, a town
    // that has thrown its lord out, a landmark held by whoever lives in it.
    // This is what a place WITHOUT an owner is, and it exists so that "unowned"
    // never has to be spelled as "imperial" — the fallback that used to hand
    // every ownerless town to the Empire of Light. Mercantile: at war with
    // raiders and the abyss, wary of cults, neutral toward the realms — free
    // folk trade with everyone and bow to no one. A city whose factionIdx goes
    // to -1 (conquest lost, rebellion, a scripted secession) becomes theirs by
    // construction, with no code anywhere to change.
    {"freefolk",        "Free Folk",
     "Towns and holdings that answer to no crown.",
     0x94a3b8,},
    // ── The player's own realm ────────────────────────────────────────────
    // The player is an ORDINARY ROW (owner's ruling, 2026-08-04: «пусть просто
    // будет фракция игрока в общей матрице фракций… и она и станет королевством
    // игрока»). His soldiers wear this index like any other body wears its
    // faction, and his standing with everyone is his ROW in the relation
    // matrix — the same storage every other pair uses, not a private map on the
    // side (see macro/state.h player_reputation / add_player_reputation).
    //
    // Его отношения НИЧЕМ не особенные: строки авторской таблицы ниже
    // встречают игрока так же, как всякого чужого (бандиты и демоны — по
    // звёздочке насмерть, культы слегка, королевства нейтрально), а игра
    // двигает их дальше через add_player_reputation.
    {"player",          "Your Realm",
     "You, your household, and everyone who marches under your banner.",
     0xfacc15,},
    // ── Дизайн-персонажи стола анкет (macro/characters.h) ─────────────────
    // Вердикт владельца 2026-09-10: фракция царя-крестьянина — ОН САМ
    // (индивид-субъект как игрок: своя строка одной матрицы, не чужое
    // знамя), драконы — ОБЩАЯ фракция рода, не по-драконная. Аппенд —
    // ординалы фракций едут в записях снапшота.
    {"king_peasant",    "King-Peasant",
     "The peasant who crowned himself. His war is with the Magika alone.",
     0xb45309,},
    {"dragons",         "Dragons",
     "The old fire above the peaks. Mortal politics do not reach them.",
     0xdc2626, /*killIsNoCrime*/true},
};
inline constexpr int kFactionCount =
    int(sizeof(kFactionDefs) / sizeof(kFactionDefs[0]));
static_assert(kFactionCount < int(kNoFaction), "sentinel must stay out of range");
static_assert(kFactionCount <= kMaxFactions,
              "world limit: 64 factions (one uint64 enemy mask, CANON S10)");

// The coin family that serves a faction: its registry row's own `mint`
// columns, copper→silver→gold. Everyone without a mint of their own —
// beasts, bandits, the free folk, an index out of range — trades in the
// imperial family, the de-facto reserve currency of v1. Returns a pointer
// to 3 ids, never null.
inline constexpr const char* kImperialCoins[3] = {
    "coin_empire_copper", "coin_empire_silver", "coin_empire_gold"};
inline const char* const* faction_coins(int factionIdx) {
    if (factionIdx >= 0 && factionIdx < kFactionCount) {
        const auto& m = kFactionDefs[factionIdx].mint;
        if (m[0] && m[0][0] != '\0') return m;
    }
    return kImperialCoins;
}

// Index of a faction id, -1 for null/empty/unknown. Pointer-first: ids are
// string literals, so the common path never reaches strcmp.
inline int faction_index(const char* id) {
    if (!id || id[0] == '\0') return -1;
    for (int i = 0; i < kFactionCount; ++i)
        if (kFactionDefs[i].id == id) return i;
    for (int i = 0; i < kFactionCount; ++i)
        if (std::strcmp(kFactionDefs[i].id, id) == 0) return i;
    return -1;
}

// THE idx→id map (replaces npc_faction_id_for / fauna_faction_id_for /
// faction_id_for_idx). Out of range — including kNoFaction — degrades to the
// empty id, which every consumer treats as neutral.
inline const char* faction_id_for_index(std::uint16_t idx) {
    return idx < std::uint16_t(kFactionCount) ? kFactionDefs[idx].id : "";
}

inline const FactionDef* faction_def_by_index(std::uint16_t idx) {
    return idx < std::uint16_t(kFactionCount) ? &kFactionDefs[idx] : nullptr;
}

// THE ownerless-ground law (owner 2026-09-11: «королевств нет, только
// фракции — одна система»): a stored faction index that names nobody — a
// landmark's -1, a cellOwner 0xff, a stale byte — resolves to the FREE FOLK,
// the registry row for everyone who answers to no crown. It used to be the
// tail of faction_index_for_kingdom; the kingdom is gone, the law stays.
inline std::uint16_t faction_or_freefolk(int idx) {
    if (idx >= 0 && idx < kFactionCount) return std::uint16_t(idx);
    return std::uint16_t(faction_index("freefolk"));
}

// Is killing a member of this faction a crime the world holds against you?
// Fail-open for an unknown/empty id: a body whose faction the registry does
// not know is a nobody, and nobody's death is nobody's business.
inline bool kill_is_no_crime(const char* factionId) {
    const int i = faction_index(factionId);
    return i < 0 ? true : kFactionDefs[i].killIsNoCrime;
}

// ── ПОЛИТИКА ВЫРЕЗАНА 2026-09-21 (вердикт владельца) ─────────────────────
// Дословно: «политики пока не будет… политику мы сделаем, но после того как
// будет ядро играбельное»; и про вырезанное: «темперамент надо вырезать
// точно», «вырезаем всё, что не касается фундаментальных систем, и чисто
// нещадно».
//
// ЧТО ЗДЕСЬ СТОЯЛО И ПОЧЕМУ УШЛО. Отношение двух фракций при рождении мира
// СЭМПЛИРОВАЛОСЬ из «банды» [lo, hi], выбранной по паре ТЕМПЕРАМЕНТОВ: шесть
// именованных банд, матрица 8×8 и три авторских оверрайда пар — около семидесяти
// чисел, ни одно из которых не выведено. Ни банд, ни темпераментов, ни
// сэмплинга НЕТ В КАНОНЕ ни одной строкой: это дословный порт прототипа, и
// сам файл state.cpp в этом признавался в своей первой строке («Faithful port
// of state.ts factories… via the band system in state.ts»).
//
// ЧТО ОСТАЛОСЬ ВМЕСТО НИХ: авторская таблица ненулевых пар (ниже) — одно
// число на пару, без броска и без темперамента. Всё неупомянутое нейтрально.
// Личные связи субъектов живут в реестре интересов (macro/interests.h), и
// матрица остаётся их БАЗОЙ.
//
// КОГДА ПОЛИТИКА ВЕРНЁТСЯ (после играбельного ядра), она вернётся ОДНИМ
// законом над реестром интересов, а не второй системой рядом с ним.

// ── ОТНОШЕНИЯ ФРАКЦИЙ — АВТОРСКАЯ ТАБЛИЦА ПАР ✓ (владелец, 2026-09-21) ────
// Дословно: «система такая, я утверждаю: 64 фракции, уже просто матрица 64×64
// всех их отношений, и у каждого воплощённого есть фракция из контекста
// макромира — всё это должно дать в субмире враждебное поведение»; и рамка:
// «помним, что это МИНИМАЛЬНАЯ система, суперлайт, просто чтобы боёвка и РПГ
// работали».
//
// МАТРИЦА — ИСТОЧНИК, ТАБЛИЦА — ЕЁ АВТОРИНГ. Перечислены только НЕНУЛЕВЫЕ
// пары; всё неупомянутое — ноль, то есть нейтралитет, потому что политики в
// мире нет. Ни броска, ни темперамента, ни банды: одно число на пару.
//
// `b == nullptr` значит «КО ВСЕМ ПРОЧИМ» — одна строка вместо восемнадцати.
// Конкретная пара БЬЁТ звёздочку, поэтому «культы против всех слегка, а
// против магов насмерть» — это две строки, а не ветка в коде.
//
// ИГРОК ЗДЕСЬ НЕ ОСОБЫЙ, И ПОЭТОМУ КОЛОНКИ `playerReputation` БОЛЬШЕ НЕТ
// (вердикт владельца того же дня, дословно: «никакой плеер репуташн, вырезать
// уничтожить»). Она отвечала на тот же вопрос вторым голосом: «как этот народ
// относится к чужим», просто чужим был один игрок. Теперь его встречают те же
// строки, что и всякого: бандиты и демоны — насмерть по звёздочке, культы —
// слегка, королевства — нейтрально.
struct FactionRelationDef {
    const char* a;
    const char* b;       // nullptr = ко всем прочим
    int         value;
};
inline constexpr FactionRelationDef kFactionRelations[] = {
    // Абисс и разбой — против всего живого, включая друг друга и себе
    // подобных: банда грабит банду, демону не свой никто.
    {"demons",  nullptr,  -100},
    {"bandits", nullptr,  -100},
    // Охота на магов взаимна и идёт до дна шкалы; со всеми прочими культ —
    // нежеланный гость, но не война.
    {"cults",   "magika", kRelationMin},
    {"cults",   nullptr,   -32},
    // ЗВЕРЬ НЕЙТРАЛЕН, И ЭТО НЕ УПУЩЕНИЕ. Прежняя банда Feral была
    // {-30, 30} при пороге враждебности -50 — то есть фауна НИКОГДА не была
    // враждебной ни на одном сиде, и охота хищника через матрицу не шла
    // никогда. Право атаковать даёт фракция, а ЖЕЛАНИЕ — строка существа:
    // волк охотится потому, что хищник, а стадо оленей не пойдёт на деревню,
    // потому что ему нечем этого хотеть.
};

// Отношение пары по авторской таблице: конкретная пара, иначе звёздочка,
// иначе НОЛЬ. Обе стороны спрашиваются симметрично — матрицу пишет
// set_relation, у которого симметрия в законе.
inline int authored_relation(const char* a, const char* b) {
    if (!a || !b) return 0;
    for (const auto& r : kFactionRelations)          // точная пара — старше
        if (r.b && ((std::strcmp(r.a, a) == 0 && std::strcmp(r.b, b) == 0)
                 || (std::strcmp(r.a, b) == 0 && std::strcmp(r.b, a) == 0)))
            return r.value;
    int worst = 0;                                   // затем «ко всем прочим»
    for (const auto& r : kFactionRelations) {
        if (r.b) continue;
        if ((std::strcmp(r.a, a) == 0 || std::strcmp(r.a, b) == 0)
            && r.value < worst) {
            worst = r.value;
        }
    }
    return worst;
}

} // namespace sm
