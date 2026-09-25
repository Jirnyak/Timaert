// Свидетель MacroStore (macro/store.h, эпик 2 шаг 1б): рождение/смерть/
// переиспользование слота под gen-защитой; обнуление ВСЕХ колонок при
// рождении (негативный контроль призрака — детектор обязан сперва УВИДЕТЬ
// грязь, §8 п.6); отказ на капе — вслух и невалидным хэндлом.
#include "check.h"

#include "macro/store.h"

#include <cstdint>

int main() {
    using namespace sm;
    auto store = make_macro_store();
    MacroStore& s = *store;

    // ── Рождение: слот жив, счёт растёт, хэндл валиден ───────────────────
    const MacroHandle a = store_birth(s);
    const MacroHandle b = store_birth(s);
    CHECK(s.valid(a) && s.valid(b), "рождённые валидны");
    CHECK(a.slot != b.slot, "два рождения — два слота");
    CHECK(s.aliveCount == 2, "счёт живых = 2");

    // ── Негативный контроль призрака: грязь видна ДО перерождения ───────
    s.inventory[a.slot].inv.slots[0].count = 777;
    s.pools[a.slot].hp = -123;
    s.dead[a.slot] = 1;
    CHECK(s.inventory[a.slot].inv.slots[0].count == 777,
          "детектор видит грязь: без обнуления мусор ЛЕЖИТ в колонке");

    // ── Смерть: хэндл мертвеет мгновенно, двойная смерть — no-op ────────
    const std::uint16_t oldGen = a.gen;
    store_death(s, a);
    CHECK(!s.valid(a), "старый хэндл мёртв (поколение выросло)");
    CHECK(s.aliveCount == 1, "счёт живых = 1");
    const std::uint32_t freeBefore = s.freeCount;
    store_death(s, a);   // двойная смерть
    CHECK(s.freeCount == freeBefore, "двойная смерть не портит freelist");

    // ── Перерождение того же слота: колонки чисты ПО ПОСТРОЕНИЮ ─────────
    const MacroHandle c = store_birth(s);
    CHECK(c.slot == a.slot, "freelist-стек вернул тот же слот");
    CHECK(c.gen == std::uint16_t(oldGen + 1), "поколение выросло ровно на 1");
    CHECK(!s.valid(a) && s.valid(c), "старый хэндл на тот же слот — мёртв");
    CHECK(s.inventory[c.slot].inv.slots[0].count == 0,
          "инвентарь слота обнулён рождением (призрака нет)");
    CHECK(s.pools[c.slot].hp == 0, "полосы обнулены рождением");
    CHECK(s.dead[c.slot] == 0, "байт судьбы обнулён рождением");

    // ── Кап: рождение до полного, следующий отказ — невалидный хэндл ────
    std::uint32_t born = 0;
    while (s.aliveCount < std::uint32_t(kMacroEntityCap)) {
        if (!s.valid(store_birth(s))) break;
        ++born;
    }
    CHECK(s.aliveCount == std::uint32_t(kMacroEntityCap) && born > 0,
          "массив заполняется до капа целиком");
    const MacroHandle overflow = store_birth(s);
    CHECK(!s.valid(overflow) && overflow.slot == kMacroNoSlot,
          "переполнение = громкий отказ невалидным хэндлом, не тихая порча");

    return sm::test::report("macro_store_test");
}
