// ДВЕРИ АНКЕТЫ НАД ЕДИНЫМ КОНТЕЙНЕРОМ (наряд M-181, разрез items):
// тела дверей НАД СОСТОЯНИЕМ — Inventory анкеты (craft/scrap/use/вес).
// Каталог и его тела уехали в tables/items.{h,cpp}; направление
// одностороннее — эти двери ЧИТАЮТ каталог (item_parts, item_yield,
// value_of, item_def), каталог о них не знает (AGENTS §11).

#include "macro/anketa.h"

#include "tables/commodity.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace sm {

bool craft_item(Inventory& inv, int defIdx, int n) {
    if (n <= 0) return false;
    const auto parts = item_parts(defIdx);
    if (parts.empty()) return false;             // terminal: nothing composes it
    // All-or-nothing on a copy (the barter_swap idiom): remove_of can succeed
    // partially across stacks before a later part runs short, and add_of can
    // refuse a full bag after the materials already left it.
    Inventory work = inv;
    for (const ItemPart& part : parts) {
        if (!work.remove_of(int(part.def), n * int(part.count))) return false;
    }
    // n batches make n × yield items — white base: seed 0, plain.
    if (!work.add_of(defIdx, n * item_yield(defIdx))) return false;
    inv = work;
    return true;
}

bool scrap_at(Inventory& inv, int slot, int n) {
    if (slot < 0 || slot >= kMaxInventorySlots || n <= 0) return false;
    const ItemRef ref = inv.slots[std::size_t(slot)];
    if (ref.empty() || ref.count < n) return false;
    const auto parts = item_parts(int(ref.def));
    if (parts.empty()) return false;             // terminal: no reverse
    Inventory work = inv;
    if (!work.remove_at(slot, n)) return false;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        int mat = int(parts[i].def);
        // The instance's material byte substitutes part 0 (ItemRef.material:
        // 1 + raw commodity row) — a steel sword returns steel.
        if (i == 0 && ref.material != 0
            && int(ref.material) <= kRawCommodityCount) {
            const int sub = item_index(kCommodities[ref.material - 1].id);
            if (sub >= 0) mat = sub;
        }
        // POOLED entropy (owner 2026-09-12): half the TOTAL matter of the n
        // units, floored — one sword still pays 1 iron, a lone dagger's
        // handle still burns whole, two daggers pool into 1 iron, and 64
        // coins (yield 32: each is 1/32 silver) melt to exactly 1 silver.
        // Affix cells are simply never read: they burn.
        const int back = n * int(parts[i].count) / (2 * item_yield(int(ref.def)));
        if (back > 0 && !work.add_of(mat, back)) return false;
    }
    inv = work;
    return true;
}

int auto_scrap_overflow(Inventory& inv) {
    int scrapped = 0;
    while (inv.used_slots() > kAutoScrapSlots) {
        // The cheapest non-fungible stack that HAS a reverse. Plain rows are
        // never candidates: they merge into one slot and cannot clog. A
        // linear pick per freed slot is the honest cost of a once-a-day tick
        // over the 1024 fixed slots.
        int best = -1;
        long bestValue = 0;
        for (int s = 0; s < kMaxInventorySlots; ++s) {
            const ItemRef& r = inv.slots[std::size_t(s)];
            if (r.empty()) continue;
            const bool fungible = r.seed == 0 && r.material == 0
                && r.level == 0 && r.entityId == 0u && affix_count(r) == 0;
            if (fungible) continue;
            if (item_parts(int(r.def)).empty()) continue;
            const long v = long(value_of(r)) * r.count;
            if (best < 0 || v < bestValue) {
                best = s;
                bestValue = v;
            }
        }
        if (best < 0) break;   // nothing scrappable: the law falls silent
        if (!scrap_at(inv, best, inv.slots[std::size_t(best)].count)) break;
        ++scrapped;
    }
    return scrapped;
}

float inventory_weight(const Inventory& inv) noexcept {
    float total = 0.0f;
    for (const ItemRef& s : inv.slots) {
        if (s.empty()) continue;
        if (const ItemDef* d = item_def_at(int(s.def))) {
            total += d->weight * static_cast<float>(s.count);
        }
    }
    return total;
}

std::string use_item(Inventory& inv, const std::string& itemId, PlayerCombatSlice& pc) {
    // Find stack
    ItemRef* stack = nullptr;
    const int idx = item_index(itemId);
    for (ItemRef& s : inv.slots) {
        if (!s.empty() && s.def == std::uint16_t(idx)) { stack = &s; break; }
    }
    if (!stack || stack->count <= 0) return {};

    const ItemDef* def = item_def(itemId);
    if (!def) return {};
    if (!item_type_consumable(def->type)) return {};

    std::string msg;
    auto append = [&](const std::string& part) {
        if (!msg.empty()) msg += ", ";
        msg += part;
    };

    // Every cell of the row goes through the ONE instant door (macro/bonus.h),
    // which reports what actually MOVED rather than what was asked for — so
    // the line the player reads says 5 when only 5 fitted. The three
    // hand-written clamp-and-append blocks that stood here were the same
    // arithmetic three times, and they could only ever speak about the three
    // pools they happened to name.
    PoolSlice pools{};
    pools.current[int(PoolId::Hp)] = &pc.currentHp;
    pools.maximum[int(PoolId::Hp)] = pc.maxHp;
    pools.current[int(PoolId::Mp)] = &pc.currentMp;
    pools.maximum[int(PoolId::Mp)] = pc.maxMp;
    pools.current[int(PoolId::Sp)] = &pc.currentSp;
    pools.maximum[int(PoolId::Sp)] = pc.maxSp;

    for (const Bonus& b : def->bonus) {
        if (b.row == 0) continue;
        const int moved = apply_instant(pools, b);
        // A zero move is REPORTED, not swallowed: "+0 HP" is how the player
        // learns the potion he just drank on a full bar was wasted.
        std::string part = moved >= 0 ? "+" : "";
        part += std::to_string(moved);
        part += " ";
        part += bonus_def(BonusId(b.row)).label;
        append(part);
    }

    inv.remove(itemId, 1);

    std::string head = "Used ";
    head += def->name;
    if (msg.empty()) return head;
    return head + ": " + msg;
}


// ── Экипировка: тела дверей (M-183) ────────────────────────────────────────

namespace {

// Пересчёт блоков одной надетой строки: каждая ПУСТАЯ ячейка, чей тип назван
// blocksMask и чей слот у тела есть, помечается производным маркером.
void mark_blocks_of(Gear& g, int origin, std::uint64_t blocks) {
    if (blocks == 0) return;
    for (int j = 0; j < kEquipCells; ++j) {
        if (j == origin) continue;
        if (!g.has.has(j)) continue;
        if (g.worn[std::size_t(j)] != kWornNothing) continue;
        if ((blocks & part_bit(equip_cell_part(j))) == 0) continue;
        g.worn[std::size_t(j)] = kWornBlocked;
    }
}

// Одна штука из стака в СВОЙ предметный слот (истина — инвентарь; ячейка
// держит ровно один экземпляр по построению). Возврат: слот этой штуки или
// -1 — в предметной области нет пустого места, отказ громкий.
int split_one(Inventory& inv, int slot) {
    ItemRef& s = inv.slots[std::size_t(slot)];
    if (s.count == 1) return slot;
    const int first = inv.creature_first();
    for (int i = 0; i < first; ++i) {
        ItemRef& d = inv.slots[std::size_t(i)];
        if (!d.empty()) continue;
        d = s;
        d.count = 1;
        s.count -= 1;
        return i;
    }
    return -1;
}

bool try_equip_cell(Gear& g, int invSlot, const ItemDef& def, int cell) {
    if (!item_fits_cell(g, cell, def)) return false;
    if (g.worn[std::size_t(cell)] != kWornNothing) return false;
    // Двуручник хочет свои блокируемые ячейки ПУСТЫМИ, не просто наличными:
    // он не смеет столкнуть щит с руки, которую носитель занял сам.
    if (def.blocksMask != 0) {
        for (int j = 0; j < kEquipCells; ++j) {
            if (j == cell) continue;
            if (!g.has.has(j)) continue;
            if ((def.blocksMask & part_bit(equip_cell_part(j))) == 0) continue;
            if (g.worn[std::size_t(j)] != kWornNothing) return false;
        }
    }
    g.worn[std::size_t(cell)] = std::uint16_t(invSlot);
    mark_blocks_of(g, cell, def.blocksMask);
    return true;
}

// Общий пролог обеих дверей надевания: валидный слот, вещь, строка, не надета
// ли уже. Возвращает def или nullptr-отказ.
const ItemDef* equip_prologue(const Gear& g, const Inventory& inv, int invSlot) {
    if (invSlot < 0 || invSlot >= kMaxInventorySlots) return nullptr;
    const ItemRef& s = inv.slots[std::size_t(invSlot)];
    if (s.empty()) return nullptr;
    if (slot_is_worn(g, invSlot)) return nullptr;   // экземпляр уже на теле
    const ItemDef* def = item_def_at(int(s.def));
    return (def && def->slotMask != 0) ? def : nullptr;
}

} // namespace

void gear_init(Gear& g, const SlotMask& bodySlots) {
    g.has = bodySlots;
    g.worn = worn_empty();
}

bool item_fits_cell(const Gear& g, int cell, const ItemDef& def) {
    if (cell < 0 || cell >= kEquipCells) return false;
    if (def.slotMask == 0) return false;
    if (!g.has.has(cell)) return false;
    return (def.slotMask & part_bit(equip_cell_part(cell))) != 0;
}

int equip(Gear& g, Inventory& inv, int invSlot) {
    const ItemDef* def = equip_prologue(g, inv, invSlot);
    if (!def) return -1;
    const int use = split_one(inv, invSlot);
    if (use < 0) return -1;
    for (int i = 0; i < kEquipCells; ++i) {
        if (try_equip_cell(g, use, *def, i)) return i;
    }
    // Тело отказало: расщеплённая штука сливается назад (консервация).
    if (use != invSlot) {
        inv.slots[std::size_t(invSlot)].count += 1;
        inv.slots[std::size_t(use)] = ItemRef{};
    }
    return -1;
}

int equip_at(Gear& g, Inventory& inv, int invSlot, int cell) {
    const ItemDef* def = equip_prologue(g, inv, invSlot);
    if (!def) return -1;
    const int use = split_one(inv, invSlot);
    if (use < 0) return -1;
    if (try_equip_cell(g, use, *def, cell)) return cell;
    if (use != invSlot) {
        inv.slots[std::size_t(invSlot)].count += 1;
        inv.slots[std::size_t(use)] = ItemRef{};
    }
    return -1;
}

bool unequip(Gear& g, const Inventory& inv, int cell) {
    if (cell < 0 || cell >= kEquipCells) return false;
    const std::uint16_t v = g.worn[std::size_t(cell)];
    if (v >= kWornBlocked) return false;   // пусто или блокер
    g.worn[std::size_t(cell)] = kWornNothing;
    remark_gear_blocks(g, inv);
    return true;
}

bool slot_is_worn(const Gear& g, int invSlot) {
    if (invSlot < 0 || invSlot >= kMaxInventorySlots) return false;
    for (int i = 0; i < kEquipCells; ++i) {
        if (g.worn[std::size_t(i)] == std::uint16_t(invSlot)) return true;
    }
    return false;
}

void remark_gear_blocks(Gear& g, const Inventory& inv) {
    for (int i = 0; i < kEquipCells; ++i) {
        if (g.worn[std::size_t(i)] == kWornBlocked)
            g.worn[std::size_t(i)] = kWornNothing;
    }
    for (int i = 0; i < kEquipCells; ++i) {
        const std::uint16_t v = g.worn[std::size_t(i)];
        if (v >= kWornBlocked) continue;
        // Индекс, чей слот опустел, протух: вещи нет — она не надета.
        if (inv.slots[std::size_t(v)].empty()) {
            g.worn[std::size_t(i)] = kWornNothing;
            continue;
        }
        if (const ItemDef* def = item_def_at(int(inv.slots[std::size_t(v)].def))) {
            mark_blocks_of(g, i, def->blocksMask);
        }
    }
}

int worn_cells(const Gear& g) {
    int n = 0;
    for (int i = 0; i < kEquipCells; ++i) {
        if (g.worn[std::size_t(i)] < kWornBlocked) ++n;
    }
    return n;
}

BonusTotals worn_bonuses(const Gear& g, const Inventory& inv) {
    BonusTotals t{};
    for (int i = 0; i < kEquipCells; ++i) {
        const std::uint16_t v = g.worn[std::size_t(i)];
        if (v >= kWornBlocked) continue;
        const ItemRef& r = inv.slots[std::size_t(v)];
        if (r.empty()) continue;
        if (const ItemDef* def = item_def_at(int(r.def))) {
            accumulate(t, def->bonus, kMaxItemBonuses);
        }
        for (int a = 0; a < kMaxItemAffixes; ++a) {
            accumulate(t, r.affix_at(a));
        }
    }
    return t;
}

// Надето ли на теле ХОТЬ ЧТО-ТО из доспеха — гейт строки «Без брони»
// (CANON S14, вердикт владельца 2026-09-30: «но если качаешь то броню не
// носишь иначе он неработает»). ОРУЖИЕ доспехом не считается — владелец
// сказал это прямо («даже с учётом надетого оружия норм»), поэтому монах с
// посохом остаётся без брони, а монах в кольчуге нет. Вопрос задаётся
// КОЛОНКЕ строки (`ItemType::Armor`), а не имени вещи.
bool wears_armor(const Gear& g, const Inventory& inv) {
    for (int i = 0; i < kEquipCells; ++i) {
        const std::uint16_t v = g.worn[std::size_t(i)];
        if (v >= kWornBlocked) continue;
        const ItemRef& r = inv.slots[std::size_t(v)];
        if (r.empty()) continue;
        if (const ItemDef* def = item_def_at(int(r.def))) {
            if (def->type == ItemType::Armor) return true;
        }
    }
    return false;
}

DefenseSum worn_defense(const Gear& g, const Inventory& inv,
                        const Skills& skills, DamageType type) {
    DefenseSum sum{};
    for (int i = 0; i < kEquipCells; ++i) {
        const std::uint16_t v = g.worn[std::size_t(i)];
        if (v >= kWornBlocked) continue;
        const ItemRef& r = inv.slots[std::size_t(v)];
        if (r.empty()) continue;
        const ItemDef* def = item_def_at(int(r.def));
        if (!def) continue;
        // ONE piece, ONE verdict: its row's columns and its own rolled
        // affixes are summed HERE, per piece, and the rank of the skill this
        // row names multiplies the pair. МНОЖИТЕЛЬ ВЫБРАН ЗА СВОЙСТВО
        // (вердикт владельца 2026-09-30): он усиливает ровно то, что в строке
        // УЖЕ СТОИТ, поэтому ранг Тяжёлой брони не рождает огнестойкости в
        // стальной плите, и ветка «складывай только там, где не ноль» не нужна.
        // Накрывает ОБЕ колонки — и процентную броню, и плоский блок.
        BonusTotals mine{};
        for (int a = 0; a < kMaxItemAffixes; ++a) accumulate(mine, r.affix_at(a));
        const int pct = def->skill != SkillId::Count
                            ? skill_mult_pct(skills, def->skill) : 100;
        // Аффиксы адресуют только КОЛОНКИ БРОНИ (вардов блока в реестре
        // бонусов нет — блок пока чистая колонка строки), поэтому слагаемое
        // аффикса стоит у брони и отсутствует у блока. Это не дыра, а
        // отсутствие строк: появятся — встанут той же суммой.
        const int mineArmor = def->defense.armor_of(type)
                            + int(mine.armor[std::size_t(type)]);
        sum.armor += mineArmor * pct / 100;
        sum.block += def->defense.block_of(type) * pct / 100;
    }
    return sum;
}

float worn_armor_weight(const Gear& g, const Inventory& inv) {
    float kg = 0.0f;
    for (int i = 0; i < kEquipCells; ++i) {
        const std::uint16_t v = g.worn[std::size_t(i)];
        if (v >= kWornBlocked) continue;
        const ItemRef& r = inv.slots[std::size_t(v)];
        if (r.empty()) continue;
        const ItemDef* def = item_def_at(int(r.def));
        if (def == nullptr || def->type != ItemType::Armor) continue;
        kg += def->weight;
    }
    return kg;
}

int armor_recovery_steps(const Gear& g, const Inventory& inv,
                         const Attributes& a, const Skills& s) {
    const float kg = worn_armor_weight(g, inv);
    if (kg <= 0.0f) return 0;
    // ТА ЖЕ ДВЕРЬ ТЕМПА, что у замаха и каста (`recovery_steps`), и тот же
    // ГЕНЕРИК-скилл: `Armsmaster`. Типовому доспешному роду вход сюда ЗАПРЕЩЁН
    // — он рычаг СИЛЫ (множит обе колонки защиты), и посчитанный ещё и в темп
    // дал бы скрытый квадрат, ровно тот, из-за которого генерик-пару убрали из
    // урона 2026-09-07. Итог: типовой множит защиту, Армсмастер сокращает
    // простой, у каждой ручки ровно одна работа.
    return recovery_steps(kg * kArmorRecoverySecondsPerKg, a, s,
                          SkillId::Armsmaster);
}

DefenseSum body_defense(const Defense& row, const Skills& skills,
                        const Gear* g, const Inventory* inv, DamageType type) {
    // Строка существа (шкура, выданные латы) множится обучением НОСИТЕЛЯ:
    // род живёт в выучке тела, а не на шкуре. Обе колонки, симметрично.
    const int rowPct = sheet_armor_mult_pct(skills);
    DefenseSum out{};
    out.armor = row.armor_of(type) * rowPct / 100;
    out.block = row.block_of(type) * rowPct / 100;
    if (g != nullptr && inv != nullptr) {
        const DefenseSum worn = worn_defense(*g, *inv, skills, type);
        out.armor += worn.armor;
        out.block += worn.block;
        if (wears_armor(*g, *inv)) return out;
    }
    // ГОЛОЕ ТЕЛО: «Без брони» СОЗДАЁТ защиту, а не множит её (вердикт
    // владельца 2026-09-30: «процентный точно не вариант для анармореда …
    // блок и процент доабвляет скил по единичке»). Это не второй закон, а
    // честный предельный случай первого: множитель усиливает ТО, ЧТО ЕСТЬ, а у
    // голого тела нет ничего — множить нечего, поэтому строка ПЛЮСУЕТ. Кто
    // «унифицирует» два глагола в один, вернёт строку в ноль, которым она и
    // простояла до 2026-09-30.
    // ПОЛОВИНА РАНГА в процентных пунктах (вывод — на строке скилла,
    // `kSkillDefs@src/tables/attributes.h`): голая ветка садится на 50 против
    // 80 у доспешной, но платит одним скиллом вместо двух и нулевым простоем.
    const int bare = skills.of(SkillId::Unarmored) / 2;
    out.armor += bare;
    out.block += bare;
    return out;
}

const ItemDef* weapon_in_hand(const Gear& g, const Inventory& inv) {
    for (int i = 0; i < kEquipCells; ++i) {
        const BodyPartId part = equip_cell_part(i);
        if (part != BodyPartId::Grip && part != BodyPartId::OffGrip) continue;
        const std::uint16_t v = g.worn[std::size_t(i)];
        if (v >= kWornBlocked) continue;
        const ItemRef& r = inv.slots[std::size_t(v)];
        if (r.empty()) continue;
        if (const ItemDef* def = item_def_at(int(r.def))) {
            if (def->type == ItemType::Weapon) return def;
        }
    }
    return nullptr;
}

StrikeFields hand_strike_fields(const Attributes& attributes,
                                const Skills& skills, const Gear* g,
                                const Inventory* inv) {
    const ItemDef* w = (g && inv) ? weapon_in_hand(*g, *inv) : nullptr;
    StrikeFields out{};
    out.dice     = w ? w->dice : kFistDice;
    out.dmgType  = w ? w->dmgType : DamageType::Blunt;
    out.delivery = w ? w->delivery : Delivery::Melee;
    out.range    = w ? w->range : 0.0f;
    const SkillId skill =
        (w && w->skill != SkillId::Count) ? w->skill : SkillId::Unarmed;
    out.multPct = std::int16_t(skill_mult_pct(skills, skill));
    const DerivedBonuses d = calculate_derived(attributes, skills);
    const BonusTotals worn = (g && inv) ? worn_bonuses(*g, *inv) : BonusTotals{};
    // A MISSILE row takes NO attribute add (owner verdict 2026-09-09): range
    // is the compensation. What the GEAR says (worn DmgFlat affixes) still
    // speaks: that is equipment's voice, not the body's.
    const int flat = (out.delivery == Delivery::Missile
                          ? 0 : int(std::floor(d.rawPhysDamage)))
                   + worn.derived_of(DerivedModId::DmgFlat);
    out.flatAdd = std::int16_t(flat < 0 ? 0 : flat);
    out.luck    = std::uint8_t(attributes.of(AttributeId::Lck));
    // The TEMPO half (CANON S14 «один рычаг»): the MASS law's base through
    // the recovery door; the worn SwingPct rows speak LAST, clamped po2.
    const int steps = recovery_steps(weapon_swing_seconds(w),
                                     attributes, skills,
                                     SkillId::Armsmaster);
    const int swingPct = 100 + std::clamp(
        worn.derived_of(DerivedModId::SwingPct),
        kDerivedPctFloor, kDerivedPctCeil);
    const int paced = steps * 100 / swingPct;
    out.recoverySteps = paced < 1 ? 1 : paced;
    return out;
}

} // namespace sm
