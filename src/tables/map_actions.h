// The macro map's interaction VERBS — the shared vocabulary of the universal
// menu (меню-сессия, owner verdicts 2026-09-11): «объект объявляет свои
// ДЕЙСТВИЯ строками, реестр решает какие доступны, рендер один».
//
// Bits of ONE u16, because a subject OFFERS a set: a city offers trade and
// hire and its contract board at once. Deliberately its own tiny header —
// the landmark registry declares its column in these bits and the squad side
// answers in the same bits, and neither should have to include the other.
//
// DECLARED here is not AVAILABLE: the bit says the kind of object speaks the
// verb at all (a ruin never trades); whether THIS one does RIGHT NOW —
// hostility for Attack, a live counterparty for Trade — is the menu row's
// availability predicate (the kPreBattleActions form), never a second table.
//
// ENTER НЕ ГЛАГОЛ СУБЪЕКТА ВОВСЕ, И БИТА У НЕГО БОЛЬШЕ НЕТ (M-290,
// 2026-10-19). Владелец, дословно: «вход теперь у нас агностичный
// универсальный просто в клетку где стоишь это единстывыенный вход в субмир
// (всё агностично и потом строится по модулям генератором 3х3 контекстно но
// вход просто в клетку)». Код это и делал: `enter_subworld@src/app/main.cpp`
// берёт `player_anchor_cell` и субъекта не спрашивает. Бит `kMapActEnter` и
// колонка `walkable`, из которой он выводился, имели НОЛЬ прод-читателей на
// двоих — сирота DOD п.9, снесена целиком.
//
// Talk is the hook of CANON S27 (one interaction window for people and
// props): today it opens the stub line, tomorrow the graph's start node.
#pragma once
#include <cstdint>

namespace sm {

inline constexpr std::uint16_t kMapActTalk   = 1u << 0;
inline constexpr std::uint16_t kMapActTrade  = 1u << 1;
inline constexpr std::uint16_t kMapActAttack = 1u << 2;
inline constexpr std::uint16_t kMapActHire   = 1u << 4;   // creatures_of ↔ hire_npc
inline constexpr std::uint16_t kMapActQuests = 1u << 5;   // the contract board

} // namespace sm
