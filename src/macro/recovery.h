// THE recovery of a body's bars — one law, one implementation, EVERY body
// (CANON S14 «три ресурса, один закон восстановления»; owner 2026-09-09:
// «никакого особенного игрока и ущербных НПЦ»; owner 2026-09-10: «реген
// только один когда стоишь на месте в макромире (типа привал) и это всё»).
//
// This file was `player_recovery.h`, and the player half of the name was the
// defect: it held an App-side fractional accumulator that never reached the
// save, and a per-minute function that read cached hourly rates off the
// player's private CombatStats while npc_ai re-derived the same arithmetic
// inline. Landing 4 killed all of it — the remainder lives in Pools beside
// its bar, the rate is derived on the spot, and both scales call the one
// function below.
#pragma once

namespace sm {
namespace ecs { struct Pools; }

// THE fractional recovery of ONE bar, and the only implementation of it in
// the game. Whole points move into `current`, the sub-point remainder waits
// in `carry`, and a full bar cannot bank rest — that last rule is what stops
// an hour spent at full health from paying out the moment the first step is
// taken. `amount` is per-call, already scaled by the caller's slice of time.
void recover_bar(float amount, float& carry, int& current, int maximum);

// СРЕЗ ВРЕМЕНИ ОДНОГО ТЕЛА — ДВА ПРОЦЕССА, А НЕ ОДИН (вердикт владельца
// 2026-10-06, дословно: «2 процесса агностичных системных — реген который
// всегда одинаковый НЕ от веса … и жжение которое от веса»), и С 2026-10-24
// ВСЕГДА-ВКЛЮЧЁННЫЙ ИЗ НИХ — РЕГЕН (вердикт владельца, дословно: «надо
// наоборот всегда пассивный реген и при передвижение жжот сп по весу»; вывод,
// оба текста владельца и компиляторные стражи лестницы — movement_cost.h,
// шапка файла и burn_stamina_per_hour):
//   · РЕГЕН SP — ВСЕГДА, по одной ставке на весь мир
//     (`rest_stamina_per_hour`), от веса клетки НЕ зависит, движением НЕ
//     выключается;
//   · ЖЖЕНИЕ SP — ТОЛЬКО В ПУТИ (`resting == false`), и его ставку
//     (`burnPerHour`) считает звонящий из веса клетки под телом, своего
//     навыка и своего перегруза-МНОЖИТЕЛЯ;
//   · РЕГЕН HP И MP — по-прежнему только у стоящего: «марш не лечит НИЧЕГО»
//     (CANON S14) у этих двух планок ЖИВ, у SP он вердиктом СНЯТ.
// Складываются ЗНАКОМ, а не веткой: ветка «если планка неполна — реген» дала
// бы полной планке на дороге жжение без возмещения, и она текла бы вниз на
// ровном месте. Один закон, один скаляр, один квант — ЧАС.
//
// ЗАЧЕМ ГЕЙТ ЖЖЕНИЯ ЖИВЁТ ВНУТРИ ДВЕРИ, А НЕ У ЗВОНЯЩИХ: иначе каждый из двух
// водителей времени обязан был бы не забыть передать ноль стоящему телу, то
// есть закон «стоящий не жжёт» держался бы дисциплиной двух мест. Звонящий
// отвечает на вопрос о СЕБЕ («в пути я или стою»), дверь — на вопрос о МИРЕ
// («что из этого чего стоит»).
//
// ЗАЧЕМ ЭТО ЗАМЕНИЛО `rest_pools`: прежняя дверь была ОДНИМ процессом и
// требовала снаружи ответа «а можно ли тут вообще встать лагерем», то есть
// целого отдельного предиката (`nav_can_stand`/`player_can_make_camp`) —
// второго ответа на вопрос, на который уже отвечал ВЕС. Предиката больше нет:
// вставать можно где угодно, а что океан дорог, выходит само из ЗНАКА разности
// двух процессов В ПУТИ. Его последний наследник — вопрос «окупается ли час
// привала» (`camp_repays_its_hour`) — умер переворотом 2026-10-24: у привала
// жжение РОВНО НУЛЬ, значит он окупается всегда, и спрашивать нечего.
// Марафон множит ТОЛЬКО реген, через тот же скилловый закон, что читают оба
// масштаба. SP идёт через ЗНАКОВЫЙ перенос (movement_cost.h settle_sp_carry),
// и полная планка не копит отдых.
//
// ВОЗВРАЩАЕТ HP, ОТНЯТЫЕ ИСТОЩЕНИЕМ за этот срез (0, пока планка держит) —
// укус живёт ЗДЕСЬ, потому что это та же дверь, что тратит: цена и
// восстановление ставятся в одном месте (ЗАКОН СПОСОБНОСТИ п.4). Он билится
// ПОЧКОВО, по очку долга (movement_cost.h bite_spent_debt), иначе
// глубина моря стала бы функцией каденса водителя — у игрока ход 0.176
// игровой минуты, у сквада think 5.625, ровно 32×.
//
// Под землёй у тела нет НИ ОДНОГО из двух процессов — ни жжения, ни регена
// (вердикт владельца 2026-10-06: «ща sp за движение в субмире не тратится и
// регена нет»; прежняя половина — «не восстанавливается НИЧТО», 2026-09-10).
// Решает это вызывающий тем, что просто не зовёт.
int settle_pools_over_time(ecs::Pools& pools, float hours, int marathonRank,
                           float burnPerHour, bool resting);

} // namespace sm
