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

// СРЕЗ ВРЕМЕНИ ОДНОГО ТЕЛА — ТЕПЕРЬ ДВА ПРОЦЕССА, А НЕ ОДИН (вердикт
// владельца 2026-10-06, дословно: «2 процесса агностичных системных — реген
// который всегда одинаковый НЕ от веса … и жжение которое от веса»; вывод и
// компиляторные стражи лестницы — movement_cost.h burn_stamina_per_hour):
//   · ЖЖЕНИЕ — ВСЕГДА, и его ставку (`burnPerHour`) считает звонящий из веса
//     клетки под телом, своего навыка и своего перегруза;
//   · РЕГЕН — всегда ОДИНАКОВЫЙ, от веса НЕ зависит, и выключен движением
//     (`regenerates == false`), причём у ВСЕХ ТРЁХ планок: марш не лечит
//     НИЧЕГО, и это одна строка, а не форма звонящих.
// Складываются ЗНАКОМ, а не веткой: ветка «если планка неполна — реген» дала
// бы полной планке на дороге жжение без возмещения, и она текла бы вниз на
// ровном месте. Один закон, один скаляр, один квант — ЧАС.
//
// ЗАЧЕМ ЭТО ЗАМЕНИЛО `rest_pools`: прежняя дверь была ОДНИМ процессом и
// требовала снаружи ответа «а можно ли тут вообще встать лагерем», то есть
// целого отдельного предиката (`nav_can_stand`/`player_can_make_camp`) —
// второго ответа на вопрос, на который уже отвечал ВЕС. Предиката больше нет:
// вставать можно где угодно, а что океан топит, выходит само из ЗНАКА
// разности двух процессов. Марафон множит ТОЛЬКО реген, через тот же
// скилловый закон, что читают оба масштаба. SP идёт через ЗНАКОВЫЙ перенос
// (movement_cost.h settle_sp_carry), и полная планка не копит отдых.
//
// ВОЗВРАЩАЕТ HP, ОТНЯТЫЕ ИСТОЩЕНИЕМ за этот срез (0, пока планка держит) —
// укус живёт ЗДЕСЬ, потому что это та же дверь, что тратит: цена и
// восстановление ставятся в одном месте (ЗАКОН СПОСОБНОСТИ п.4). Он билится
// ПОЧКОВО, по очку долга (movement_cost.h bite_continuous_debt), иначе
// глубина моря стала бы функцией каденса водителя — у игрока ход 0.176
// игровой минуты, у сквада think 5.625, ровно 32×.
//
// Под землёй у тела нет НИ ОДНОГО из двух процессов — ни жжения, ни регена
// (вердикт владельца 2026-10-06: «ща sp за движение в субмире не тратится и
// регена нет»; прежняя половина — «не восстанавливается НИЧТО», 2026-09-10).
// Решает это вызывающий тем, что просто не зовёт.
int settle_pools_over_time(ecs::Pools& pools, float hours, int marathonRank,
                           float burnPerHour, bool regenerates);

} // namespace sm
