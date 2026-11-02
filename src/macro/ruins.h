// Ruin placement — worldgen pass for the Ruin place (SquadType::Ruin,
// tables/squad_type.h), §42 Инк 5. Half the axis could never be born: the row,
// the subworld route (строка `FT_Ruin` → `gen_ruin`) and the surface generator
// (gen_ruin) all stood ready while no pass pushed a Ruin record — which is
// why the demo's scene 4 («разведать руины») had no target in the world.
// The site law mirrors generate_spires: the danger-zone field already
// encodes "far from civilization", so the zone band below IS the
// distance law, and a Mitchell best-candidate pick spreads the ruins over
// it without a tuned separation constant. (Souls at birth DIED with
// `place_born_population`, M-320: a ruin is a pure feature with no squad,
// and a dungeon's population comes from its own module, CANON S28.)
#pragma once
#include <cstdint>

namespace sm {

struct GameState;
struct FeatureLayer;
struct TerrainData;
struct ZoneLayer;

// ПОЛОСА ОПАСНОСТИ РУИНЫ — ЧИСЛА ГЕНЕРАТОРА (ломоть 5б M-235, 2026-11-02:
// колонки `minZone`/`maxZone` уехали из умирающего реестра мест — их читала
// ТОЛЬКО генерация; ЗАКОН АГНОСТИЧНОСТИ). Вывод: байт опасности 0..255 —
// перевод старых полос 0..9 краями min = band·256/10,
// max = (band+1)·256/10 − 1; руина жила в полосе «2..8» ⇒ 51..229 — ближе
// к жилью, чем шпиль, но не в самом огне. Числа БАЛАНС.
inline constexpr std::uint8_t kRuinMinZone = 51;
inline constexpr std::uint8_t kRuinMaxZone = 229;

// Push Ruin places into gs.places — one per city the world raised
// plus a base handful, so ruin density follows civilization's own scale.
// Deterministic from gs.worldSeed. Requires zones — call AFTER
// generate_zones (and after the settlement passes, whose cells it avoids).
// A world with no admissible land simply gets fewer ruins (logged).
struct MacroStore;   // fwd — тело места рождается в store (M-90 шаг 5)
void generate_ruins(GameState& gs, MacroStore& st, const ZoneLayer& zones,
                    const TerrainData& terrain,
                    FeatureLayer& features);

} // namespace sm
