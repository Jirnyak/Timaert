#include "macro/economy.h"
#include "bus/anketa.h"
#include "macro/state.h"
#include "ecs/components.h"
#include "macro/econ_day.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm {

namespace {
inline float jround(float v) { return std::round(v); }

} // namespace

float stock_scarcity(int supply, int demandSeason) {
    // КРИВАЯ ПРОИЗВОДСТВА (CANON S10, пять законов): `value × (спрос+1)/(запас+1)`,
    // в обе стороны, КОРИДОРА НЕТ. С 2026-11-12 её читают только решения о
    // труде; сделка меряет оценкой ядра (`stock_worth@src/macro/economy.h`).
    // Спрос приходит СЕЗОННЫМ ЧИСЛОМ, и календаря в этой
    // двери больше нет (CANON S10, долг, 2026-09-19): прямая часть спроса =
    // остаток СЧЁТА места — уже сезонная величина, тающая по мере оплаты, —
    // производная по рецептам считается той же меркой (season_demand_for),
    // так что горизонт у обеих половин один по построению (ловушка S10
    // «переводить обе половины» закрыта дверью, а не дисциплиной).
    // Равновесие читаемо: склад == непокрытая нужда ⇒ цена ровно по базе;
    // любое отклонение цены от базы означает «здесь не хватает» или «здесь
    // завал» — для каждого товара и места без исключений.
    // Коридор [0.25…4.0] умер вердиктом 2026-09-18: он не защищал от
    // «щедрого купца», а СОЗДАВАЛ его — в насыщении нет слиппеджа, и только
    // там прокрутка была прибыльной (price_law_test держит обратное).
    const long long s = (long long)(supply < 0 ? 0 : supply) + 1;
    const long long d = (long long)(demandSeason < 0 ? 0 : demandSeason) + 1;
    return float(d) / float(s);
}

int stock_price(int baseValue, int supply, int demandSeason) {
    const float p = float(baseValue) * stock_scarcity(supply, demandSeason);
    const int v = int(jround(p));
    return v < 1 ? 1 : v;
}

namespace {

// Demand is DIRECT (the place's DEBT, CANON S10 2026-09-19: «спрос кривой
// дефицита читается из ДОЛГА» — непогашенный остаток счёта И ЕСТЬ
// непокрытая нужда, сезонной меркой по построению) plus DERIVED (owner
// track 2026-08-30): a town that wears cloth demands fibre, because cloth
// is MADE of it — the demand of every recipe output flows down to its
// inputs × qty. Without this a starving city priced the raw INPUT at base
// (nobody "eats" a raw input), its caravans saw no profit in hauling it, and
// stone outbid food (measured, balance_run 2026-08-30 — на тогдашней цепи
// зерно→хлеб; цепь умерла с производством еды, замер остался в силе для
// всякой другой). Recursive over the recipe table
// with a small depth cap: chains are data and may grow (ore → metal →
// tool), cycles must not hang.
// Без счёта (needDebt == nullptr — снимок чужого дома, фикстура) прямая
// часть честно падает на лестницу населения × сезон.
int demand_for_(int itemIdx, const std::int32_t* needDebt,
                int population, const Skills& hands,
                const Inventory* store, int depth) {
    if (itemIdx < 0 || population <= 0) return 0;
    int demand = 0;
    // ПРЯМАЯ ПОЛОВИНА — строка лестницы, найденная ТОВАРНЫМ ординалом: тот
    // же вопрос, что решал `strcmp` по восьми строкам на каждый вызов. Строка
    // каталога, товаром не являющаяся (оружие, монета), лестницы не касается
    // и честно получает ноль прямого спроса — производный ниже у неё есть.
    const int commodity = commodity_of_item(itemIdx);
    if (commodity >= 0) {
        // Со счётом — ОСТАТОК счёта (он и есть непокрытая нужда, сезонная
        // мерка по построению). Без счёта (снимок чужого дома, фикстура) —
        // та же формула, которой счёт и выставляется: голод житель-днями,
        // комфорт долей бюджета (M-137). Строка, которой нужды нет вовсе
        // (сырьё, монета), даёт ноль обоими путями — её счёт никогда не
        // выставляется.
        demand += needDebt
            ? int(needDebt[commodity])
            : (commodity == hunger_commodity_ordinal()
                   ? season_hunger_units(population)
                   : season_comfort_units(population, commodity));
    }
    if (depth > 0) {
        const int target = itemIdx;
        for (int ri = 0; ri < kRecipeCount; ++ri) {
            const RecipeDef& r = kRecipes[ri];
            // Derived demand exists only where the recipe CAN run: hands
            // that weave nothing want no fibre beyond their own needs,
            // however hungry their future loom would be — without this
            // gate the growers' own granaries priced at the scarcity
            // ceiling and the caravans' loans bought a quarter of the lot
            // (measured, balance_run 2026-08-30).
            // Право рецепта — колонки его Craft-строки (одна строка на оба
            // мира; M-146: гейт один).
            const ActionDef& act = action_def(int(r.action));
            if (!recipe_known(hands, act.gateSkill, int(act.minRank)))
                continue;
            // The recipe's matter = its output row's composition (the one
            // matter table, items.h). The mint's output is no catalog row
            // (-1 → empty span), and its silver demand was always zero:
            // nothing NEEDS coin down the needs ladder.
            const int outIdx = recipe_out_item(ri);
            for (const ItemPart& part : item_parts(outIdx)) {
                if (int(part.def) != target) continue;
                // НЕТТИНГ СКЛАДОМ ВЫХОДА (владелец 2026-09-18, «смотреть
                // и на сезон, и на склад текущий»): вход нужен только на
                // НЕДОПЕЧЁННЫЙ остаток нужды выхода. Полный амбар хлеба не
                // хочет зерна — его пустая зерновая полка больше не
                // «дефицит» и не взрывает ни цену, ни скор рейса; пустой
                // амбар хочет в полную силу. С долгом обе величины —
                // СЕЗОННЫЕ ЧИСЛА по построению, дробь «туда-обратно» через
                // дневную мерку умерла вместе с календарём кривой.
                long long outSeason = demand_for_(outIdx, needDebt,
                                                  population, hands,
                                                  store, depth - 1);
                if (store) {
                    outSeason -= store->count_of(outIdx);
                    if (outSeason < 0) outSeason = 0;
                }
                demand += int(outSeason) * int(part.count);
            }
        }
    }
    return demand;
}

}  // namespace

// НУЛЕВОГО СПРОСА НЕ БЫВАЕТ (владелец, 2026-09-18, дословно: «надо сделать,
// чтобы не было нулевого спроса — все ресурсы имеют спрос, универсальная
// система ресурсов; другой вопрос — большой или малый. И естественно спрос
// падает, если на складе много, но это уже должно быть тот же закон, что с
// ценой — единый»).
//
// ПОЧЕМУ ЭТО БЫЛО ДЕФЕКТОМ, И ЧИСЛОМ: спрос 0 у всего, что здесь не едят,
// ронял цену такого товара на ПОЛ кривой дефицита (0.25 × базы), а по этой
// цене аукцион артелей оценивает рейс. Поэтому деревня, СТОЯЩАЯ НА ЖЕЛЕЗЕ,
// никогда его не копала: зерно, которое она ест, всегда било металл, которым
// она не питается. Мир не добывал ни железа, ни серебра за 256 дней.
//
// ПОЛ СПРОСА ВЫВЕДЕН, А НЕ НАЗНАЧЕН: он равен самой СЛАБОЙ нужде лестницы
// (у статуи 1 на 512 жителей-дней) — «то, что никому не нужно, нужно так же
// редко, как самое редкое из нужного». Растёт лестница — двигается и пол;
// новой константы не рождается. Дальше всё делает ОДНА уже живущая кривая:
// склад полон — цена падает, склад пуст — растёт.
int season_demand_for(int itemIdx, const std::int32_t* needDebt,
                      int population, const Skills& hands,
                      const Inventory* store) {
    // Depth 4 covers chains far past today's one-step recipes (ore → metal
    // → part → tool) and caps any future accidental cycle.
    // ПОЛ СПРОСА УМЕР ВМЕСТЕ С ТРЕТЬИМ ПОТОКОМ (владелец, 2026-09-20: «у
    // ресурсов да, нет нужды, они только на крафт»). Он выражал «нулевого
    // спроса не бывает» и был выведен из слабейшей строки лестницы — но той
    // же арифметикой давал РОВНО НОЛЬ у всякого места мельче 512 душ, то есть
    // у всех деревень мира: закон работал только для городов, а деревня,
    // стоящая на жиле, всё равно не копала.
    // Теперь ноль у РЕСУРСА — это закон, а не дыра: ресурс нужен станку, а
    // не рту, и спрос на него производный, от рецептов. Чтобы деревня всё же
    // копала НА ЭКСПОРТ, решение о труде читает не только свою цену — см.
    // аукцион добычи (npc_ai.cpp, цена сюзерена).
    return demand_for_(itemIdx, needDebt, population, hands, store, 4);
}

// ── МОСТ «НУЖДА → ЯДРО» ──────────────────────────────────────────────────
// Нужда строки — остаток счёта сквада по ней, и больше ничего (закон и шрам —
// у объявления). Строка вне словаря товаров счёта не имеет.
RowSide row_side(int itemIdx, std::int64_t base, const std::int32_t* needDebt,
                 const Inventory& inv, std::int64_t unitMassG,
                 std::int64_t backG) {
    RowSide r{};
    r.base = base;
    r.unitMassG = unitMassG;
    r.backG = backG;
    if (itemIdx < 0) return r;
    const int commodity = commodity_of_item(itemIdx);
    const std::int64_t debt =
        (needDebt && commodity >= 0 && needDebt[commodity] > 0)
            ? std::int64_t(needDebt[commodity]) : 0;
    r.net = std::int64_t(inv.count_of(itemIdx)) - debt;
    return r;
}

} // namespace sm
