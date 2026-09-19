// The player's trade-price laws — all that remains of the old abstract
// economy (W2b-4, owner's ruling: rip it whole).
//
// What used to live here — EconomyState's float arrays that never produced
// a good (every recipe demanded two resources while every village gathered
// only grain), update_prices nobody could pay, TradeRoutes that minted money
// on arrival — is GONE. The honest economy is macro/econ_day.h (the day's
// laws over the landmark's universal Inventory) plus AGENTS: woodcutters
// and farmers gather (macro/npc_ai.cpp), caravans carry real cargo in their
// own bags. Prices from STOCK arrive with the treasury increment (W2d) —
// until then the ONE player price law below serves every trade screen.
//
// Trade itself is UNIVERSAL (owner): an exchange between two Inventories —
// a settlement's, an NPC's, one day a chest's — priced by this law with the
// caller's ONE context multiplier (settlement mood / merchant temperament).
#pragma once
#include <cstdint>

namespace sm {

// ── Context contributions to the one price law (CANON S6, 2026-08-24) ─────
// The named multiplier columns live HERE, beside the law they feed — they
// used to be private helpers of two different screens (the settlement's mood
// in overlays.cpp, the merchant's temperament in macro_overlay.cpp): the
// same law with two homes, one of them buy-side only by accident of
// location. 1.0 is a system with nothing to say. New context (war, season,
// route danger) joins as another named column, never a screen helper.
enum class SettlementMood : std::uint8_t;
namespace ecs { struct NpcTraits; }
// Both context columns answer the same shape — WHICH SIDE of the deal is being
// priced — because a context that only knows how to make things dearer prices
// one direction and forgets the other. The settlement's mood used to take no
// `buying` flag, so a town in revolt charged the player a premium and then
// paid him the ordinary price for his goods: the context applied to half the
// trade by accident of its signature.
float mood_price_mult(SettlementMood mood, bool buying);
float trait_price_mult(const ecs::NpcTraits* traits, bool buying);

// THE trade price — ОДИН закон и ДВЕ СТОРОНЫ (CANON S25, владелец
// 2026-09-18: «лавки как таковой нет — есть две макросущности… просто
// сравнение харизм двух, и у кого больше, тот и наценивает»).
//
// Обе стороны называют ОДНО число — финальное производное торговой силы
// (attributes.h DerivedBonuses::tradeDiscountPct, атрибут × скилл), и
// наценку берёт тот, у кого оно выше; величина — РАЗНИЦА. Поэтому:
//   · равные стороны торгуют ровно по цене — наценки нет и быть не должно;
//   · сильный покупает дешевле и продаёт дороже ОДНОЙ формулой, без
//     отдельной «маржи лавки»;
//   · спелл обаяния, артефакт и перк входят в цену бесплатно — они меняют
//     лист, а сделка спрашивает только производное.
//
// ЧТО УМЕРЛО ВМЕСТЕ С ЭТИМ (все три — назначенные числа, CANON S26):
// постоянный спред продажи ×0.7 («дом берёт своё» — дома у сделки больше
// нет), пол скидки 0.5 и потолок надбавки 1.5 (граница выводится из анкет
// или её не существует), и `bargaining_edge` — второе место, где считалась
// торговая сила.
//
// Пол цены 1 остаётся: это не кламп наценки, а закон «ничто не бесплатно».
// `contextMult` — настроение места или нрав купца — по-прежнему ОДНА
// колонка, которую разрешает вызывающий.
int trade_price(int baseValue, int myTradePct, int theirTradePct,
                       float contextMult, bool buying);

int trade_buy_price (int basePrice, int myTradePct, int theirTradePct);
int trade_sell_price(int basePrice, int myTradePct, int theirTradePct);

// ── Price FROM STOCK (the starcluster law, owner-approved) ───────────────
//
//     scarcity = (demand × season + 1) / (supply + 1) — NO corridor
//     price    = base × scarcity, floor 1 («ничто не бесплатно»)
//
// `supply` is the counterparty's stock of the item; `demand` its SEASON
// demand — С ДОЛГОМ (CANON S10, 2026-09-19) горизонт живёт в самой мере
// спроса, а не в этой двери: остаток счёта УЖЕ сезонная величина, и
// последняя зависимость цены от календаря снята (supply == остаток счёта
// ⇔ price == base; долг погашен и полка пуста ⇒ цена около базы × пол
// спроса). The caller passes the POST-TRADE supply — what remains after
// a buy, what piles up after a sell — so every deal pays its own
// SLIPPAGE: buying leaves the shelf scarcer and dearer, selling gluts it
// cheaper. That slippage is what extinguishes arbitrage: a buy-then-sell
// round trip can never profit, whatever the charisma and context
// multipliers say (price_law_test pins it across the once-exploitable
// generous pair). The nominal price is UNBOUNDED by design (verdict
// 2026-09-18: цена растёт и падает как угодно); what bounds every DEAL
// is realizability — the payer's inventory value (max_affordable_lot_) —
// and what bounds every DECISION WEIGHT is purchasing power, never a
// corridor.
float stock_scarcity(int supply, int demandSeason);
int stock_price(int baseValue, int supply, int demandSeason);

// A settlement's SEASON demand for an item (CANON S10 «спрос читается из
// ДОЛГА»): прямая часть = ОСТАТОК СЧЁТА места по этой строке лестницы —
// непогашенная нужда и есть спрос, — PLUS the derived demand of every
// recipe ITS HANDS can run (a city that bakes demands grain; a place whose
// cooking rank is zero does not — owner track 2026-08-30, and since
// 2026-09-18 the gate is the place's own ANKETA, not its kind).
//
// `needDebt` — счёт места (Landmark::needDebt). nullptr = читателя без
// счёта (снимок чужого дома, фикстура) — прямая часть честно падает на
// старую лестницу населения × сезон.
//
// `store` — ЭТОГО места склад: производная половина спроса гасится запасом
// ВЫХОДА (владелец 2026-09-18, «смотреть и на сезон, и на склад текущий»):
// зерно нужно только на НЕДОПЕЧЁННЫЙ остаток сезонной нужды хлеба — полный
// амбар не хочет зерна, пустой хочет в полную силу (выгода караванов с
// зерном, ради которой производный спрос строился, живёт ровно там, где
// она настоящая). nullptr = спрос без неттинга — для читателя, у которого
// есть только классовый снимок чужого дома, а не склад (память крю).
//
// Пол спроса («нулевого спроса не бывает», владелец 2026-09-18) выведен из
// слабейшей нужды лестницы, той же сезонной меркой.
struct Skills;
struct Inventory;
int season_demand_for(const char* itemId, const std::int32_t* needDebt,
                      int population, const Skills& hands,
                      const Inventory* store);

} // namespace sm
