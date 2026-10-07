// МОДУЛЬ ШПИЛЯ: расстановка в генезисе и ОДИН вывод о его орбе в живом мире.
//
// Spire placement — worldgen pass for the Spire landmark (landmark_registry.h
// row "spire"). One learnable spell = one spire the world must offer; the
// spire's site is decided by the danger-zone field, not by dice against
// civilization: zones already encode "far from cities" (civ BFS pulls danger
// down), so the zone gate IS the distance law. The spell's tier picks how deep
// into the wild band the spire must stand — read straight from the spell
// registry (macro/spells.h), which lives in the world layers precisely so
// worldgen can ask it (history/ARCHITECTURE.md Rule 13).
#pragma once
#include <cstdint>

namespace sm {

struct GameState;
struct TerrainData;
struct ZoneLayer;

// Fill gs.spires (assumed cleared by populate_landmarks_from_politik) — one
// spire per kSpellDefs row; the spell is the spire cell's WORKED number
// (state.h gs.worked: row ordinal + 1, 0 = drained — закон нуля-ординала).
// Deterministic from gs.worldSeed. Requires zones — call AFTER
// generate_zones. A spell whose zone band does not exist on this world gets
// its gate relaxed down to the table minimum; a world with no admissible land
// at all simply lacks that spire (logged).
struct MacroStore;   // fwd — тело места рождается в store (M-90 шаг 5)
void generate_spires(GameState& gs, MacroStore& st, const ZoneLayer& zones,
                     const TerrainData& terrain);

// ── ЗАРЯД ШПИЛЯ — ОДИН ОТВЕТ НА ОДИН ВОПРОС МИРА (M-233 п.8) ─────────────
//
// МЕРА, А НЕ ВПЕЧАТЛЕНИЕ: `worked_read` ПРО ШПИЛЬ звали из СЕМИ мест, и
// каждое само решало, что `worked_read == 0` значит у НЕ-шпиля. По вопросам:
// «выкачан ли» — пять написаний (сборщик фактов клетки, визитор мест, панель
// поселения, макро-оверлей, гейт орба в субмире), «какой тир» — два (сборщик
// и дневной тик), «какое число держит орб» — два сырых чтения в субмире.
// Это ровно DOD п.6, и ни одно написание не было видно остальным. Теперь
// вывод живёт здесь, а сборщик фактов клетки (macro/cell_facts.h) его
// ПУБЛИКУЕТ колонками `spell`/`tier`/`depleted` — кто держит в руках
// `CellFacts`, читает их оттуда и сюда не ходит.
// (Наряд M-233 обещал ЧЕТЫРЕ написания; перепись при правке дала семь —
// два лишних нашлись в субмире, куда наряд не смотрел.)
//
// ЦЕНА НУЛЯ У НЕ-ШПИЛЯ НАЗВАНА, ПОТОМУ ЧТО ОНА И ЕСТЬ ЛОВУШКА: worked-число
// клетки у ПОСЕЛЕНИЯ — это его ПАСТВА (macro/labour.h souls_flock), поэтому
// «выкачан == worked 0» без проверки рода объявило бы выкачанным каждый
// обезлюдевший город. `depleted` у не-шпиля ЛОЖЬ всегда, и это закон, а не
// частный случай; свидетель — tests/spire_generation_test.cpp.
struct SpireOrb {
    int  spell = 0;         // worked-число клетки: ординал kSpellDefs + 1;
                            //   0 = выкачан ИЛИ клетка не шпиль
    int  tier  = 0;         // колонка силы строки спелла (1..5); 0 у
                            //   выкачанного и у не-шпиля
    bool depleted = false;  // шпиль, чей орб забран; у НЕ-шпиля ВСЕГДА false
};

// Род и адрес — колонки ТОГО ЖЕ слота (идиома `souls_flock(gs, st, slot)`,
// стоящая рядом на прежних сайтах макро-стороны), поэтому вопрос задаётся
// слотом, а не парой координат: пара потребовала бы от звонящего знать, что
// это за сквад, то есть завела бы написание номер восемь.
SpireOrb spire_orb(const GameState& gs, const MacroStore& st,
                   std::uint16_t slot);

} // namespace sm
