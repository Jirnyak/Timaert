// City settlement generator slice.
// Mirrors src/game/subworld/city-generator.ts at the C++ map-data level.
#pragma once
#include "sub/map_data.h"

namespace sm::sub {

void generate_city(const CellContext& ctx,
                   const std::uint8_t nbFeature[9],
                   SubworldMapData& out);

} // namespace sm::sub
