// Свидетель MacroStore (macro/store.h, эпик 2 шаг 1б; M-220): рождение/
// смерть/переиспользование слота под ID-защитой — валидность ссылки судит
// ОРДИНАЛ (spawnId), эмитент у стора монотонен и через смерть мира;
// обнуление ВСЕХ колонок при рождении (негативный контроль призрака —
// детектор обязан сперва УВИДЕТЬ грязь, §8 п.6); отказ на капе — вслух и
// невалидным хэндлом.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (M-220, 2026-10-16, обе полярности):
//  · рождение переиспользует ординал «если колонка 0» — МОЛЧИТ (пустышка:
//    X-список зануляет spawnId ДО выдачи; записана как слабая мутация);
//  · id = f(slot) (перерождение повторяет имя прежнего жильца) — КРАСНЫЙ,
//    4 FAIL: «новый жилец несёт НОВЫЙ ординал», «старый хэндл мёртв: ID не
//    совпал», «первое рождение нового мира выше всех прошлых», «хэндл
//    прошлого мира мёртв»;
//  · store_reset сбрасывает эмитент в 1 — КРАСНЫЙ, 2 FAIL: «эмитент пережил
//    смерть мира», «первое рождение нового мира — ординал выше»;
//  · выдача с ДЫРАМИ (+= 2) — МОЛЧИТ (21 OK): плотность выдачи законом не
//    является; первый снимок этой мутации был КРАСНЫМ от пина `b.id == 2`
//    — пере-зажатый свидетель чинён до закона (нуль-ординал + монотонность).
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
    CHECK(a.id == 1u && b.id > a.id,
          "дверь рождения ВЫДАЁТ ординал сама: с 1 (закон нуля-ординала), "
          "монотонно; ПЛОТНОСТЬ выдачи законом не является (мутация «с "
          "дырами» обязана молчать — таблица M-220)");
    CHECK(s.spawnId[a.slot].index == a.id,
          "ординал хэндла и колонка spawnId — одно число");

    // ── Негативный контроль призрака: грязь видна ДО перерождения ───────
    s.inventory[a.slot].inv.slots[0].count = 777;
    s.pools[a.slot].hp = -123;
    s.dead[a.slot] = 1;
    CHECK(s.inventory[a.slot].inv.slots[0].count == 777,
          "детектор видит грязь: без обнуления мусор ЛЕЖИТ в колонке");

    // ── Смерть: хэндл мертвеет мгновенно, двойная смерть — no-op ────────
    const std::uint32_t oldId = a.id;
    store_death(s, a);
    CHECK(!s.valid(a), "старый хэндл мёртв (слот свободен)");
    CHECK(s.aliveCount == 1, "счёт живых = 1");
    const std::uint32_t freeBefore = s.freeCount;
    store_death(s, a);   // двойная смерть
    CHECK(s.freeCount == freeBefore, "двойная смерть не портит freelist");

    // ── Перерождение того же слота: колонки чисты ПО ПОСТРОЕНИЮ ─────────
    const MacroHandle c = store_birth(s);
    CHECK(c.slot == a.slot, "freelist-стек вернул тот же слот");
    CHECK(c.id > oldId, "новый жилец несёт НОВЫЙ ординал — выше прежнего");
    CHECK(!s.valid(a) && s.valid(c),
          "старый хэндл на тот же слот — мёртв: ID не совпал (M-220)");
    CHECK(s.inventory[c.slot].inv.slots[0].count == 0,
          "инвентарь слота обнулён рождением (призрака нет)");
    CHECK(s.pools[c.slot].hp == 0, "полосы обнулены рождением");
    CHECK(s.dead[c.slot] == 0, "байт судьбы обнулён рождением");

    // ── Кап: рождение до полного, следующий отказ — невалидный хэндл ────
    std::uint32_t born = 0;
    while (s.aliveCount < std::uint32_t(kUnifiedCap)) {
        if (!s.valid(store_birth(s))) break;
        ++born;
    }
    CHECK(s.aliveCount == std::uint32_t(kUnifiedCap) && born > 0,
          "массив заполняется до капа целиком");
    const MacroHandle overflow = store_birth(s);
    CHECK(!s.valid(overflow) && overflow.slot == kMacroNoSlot
              && overflow.id == 0,
          "переполнение = громкий отказ невалидным хэндлом, не тихая порча");

    // ── СМЕРТЬ МИРА: эмитент НЕ трогается — хэндл прошлого мира мёртв ПО
    // ПОСТРОЕНИЮ (M-220, арена-форма: nextId переживает clear). До M-220 это
    // держал бамп поколения в store_reset; теперь новый жилец ЛЮБОГО слота
    // получает ординал выше всех прошлых, совпадения не бывает.
    const MacroHandle lastWorld = c;            // живой хэндл прошлого мира
    const std::uint32_t issuerBefore = s.nextSpawnId;
    store_reset(s);
    CHECK(s.aliveCount == 0, "смерть мира: живых ноль");
    CHECK(s.nextSpawnId == issuerBefore,
          "эмитент пережил смерть мира: монотонность через перерождение");
    const MacroHandle newWorld = store_birth(s);
    CHECK(s.valid(newWorld) && newWorld.id > lastWorld.id,
          "первое рождение нового мира — ординал выше всех прошлых");
    CHECK(!s.valid(lastWorld),
          "хэндл прошлого мира мёртв, даже если слот переиспользован");

    return sm::test::report("macro_store_test");
}
