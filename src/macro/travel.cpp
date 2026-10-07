#include "macro/travel.h"

#include "macro/anketa.h"

namespace sm {

OverloadCharge overload_charge(const CharacterSheet& sheet,
                               const Inventory& inventory) {
    return overload_charge_from_capacity(
        get_carry_capacity(sheet.attributes, sheet.skills), inventory);
}

} // namespace sm
