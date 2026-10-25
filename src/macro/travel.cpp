#include "macro/travel.h"

#include "macro/anketa.h"

namespace sm {

float overload_burden(const CharacterSheet& sheet,
                      const Inventory& inventory) {
    return overload_burden_from_capacity(
        get_carry_capacity(sheet.attributes, sheet.skills), inventory);
}

} // namespace sm
