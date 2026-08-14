// THE guard for enum-indexed parallel tables. A table whose row order must
// mirror an enum is a silent-drift hazard: kInteractRows once ran three verbs
// out of enum order for months (the well prompted "Search"), and kNpcPurse
// quietly zero-filled the gatherer professions when NPCType grew. The cure is
// structural: every such row carries its own enum as a column, and the table
// declares static_assert(rows_in_enum_order(...)) — a drifted table refuses
// to compile instead of mis-keying at runtime.
#pragma once
#include <cstddef>

namespace sm {

template <class Row, std::size_t N, class Enum>
constexpr bool rows_in_enum_order(const Row (&rows)[N], Enum Row::*key) {
    for (std::size_t i = 0; i < N; ++i)
        if (rows[i].*key != Enum(i)) return false;
    return true;
}

} // namespace sm
