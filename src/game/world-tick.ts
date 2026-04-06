// === World Tick — time advancement + settlement simulation ===
//
// Layer 1 (Macroworld). Pure game-state mutation, no UI dependencies.

import type {WorldTime, Settlement, Village} from './state';
import type {EventBus} from './event-bus';
import {EventTag} from './event-types';
import {generateGarrison, addArmy} from './army';
import {
	type TradeRoute,
	gatherResources, produceGoods,
	updatePrices, consumeAndGrow,
	findBestTradeRoute, settleTradeRoute,
} from './economy';

/**
 * Advance world time by one minute.
 * On hour changes emits TimeAdvance.
 * On day changes runs per-settlement population simulation.
 */
export function advanceWorldMinute(
	worldTime: WorldTime,
	settlements: Settlement[],
	bus: EventBus,
	villages?: Village[],
	activeTradeRoutes?: TradeRoute[],
): void {
	worldTime.minute += 1;

	if (worldTime.minute < 60) {
		return;
	}

	worldTime.minute = 0;
	worldTime.hour += 1;

	if (worldTime.hour >= 24) {
		worldTime.hour = 0;
		worldTime.day += 1;
		tickSettlements_(settlements, worldTime.day);
		if (villages) {
			tickVillages_(villages, worldTime.day);
		}

		if (villages && activeTradeRoutes) {
			tickEconomy_(settlements, villages, activeTradeRoutes, worldTime.day);
		}
	}

	bus.emit({
		tag: EventTag.TimeAdvance,
		day: worldTime.day,
		hour: worldTime.hour,
	});
}

// ── Settlement daily tick (population fluctuation) ──

function tickSettlements_(settlements: Settlement[], day: number): void {
	for (const s of settlements) {
		// Economy: cities produce goods from resources
		produceGoods(s.eco, s.population);
		updatePrices(s.eco, s.population, true);
		const {popDelta} = consumeAndGrow(s.eco, s.population);

		// Population: economic growth + mood influence
		const moodChange = s.mood === 'Prosperous'
			? 1
			: (s.mood === 'Revolt'
				? -2
				: 0);
		s.population = Math.max(
			10,
			s.population + popDelta + moodChange + Math.floor(Math.random() * 3) - 1,
		);

		// Cap city population
		s.population = Math.min(10_000, s.population);

		// Update mood from happiness
		if (s.eco.happiness > 0.7) {
			s.mood = 'Prosperous';
		} else if (s.eco.happiness > 0.5) {
			s.mood = 'Stable';
		} else if (s.eco.happiness > 0.3) {
			s.mood = 'Tense';
		} else if (s.eco.happiness > 0.15) {
			s.mood = 'Unrest';
		} else {
			s.mood = 'Revolt';
		}

		// Generate garrison from population (soldiers cost pop)
		if (s.population >= 20) {
			const {garrison, popCost} = generateGarrison(s.population, Math.random);
			if (popCost > 0) {
				addArmy(s.garrison, garrison);
				s.population = Math.max(0, s.population - popCost);
			}
		}

		s.history.days.push(day);
		s.history.population.push(s.population);
		if (s.history.days.length > 30) {
			s.history.days.shift();
			s.history.population.shift();
		}
	}
}

// ── Village daily tick ──

function tickVillages_(villages: Village[], day: number): void {
	for (const v of villages) {
		// Gather resources based on local terrain
		gatherResources(v.eco, v.population);
		updatePrices(v.eco, v.population, false);
		const {popDelta} = consumeAndGrow(v.eco, v.population);

		const moodChange = v.mood === 'Prosperous'
			? 1
			: (v.mood === 'Tense'
				? -1
				: 0);
		v.population = Math.max(
			5,
			v.population + popDelta + moodChange + Math.floor(Math.random() * 2) - 1,
		);
		v.population = Math.min(1000, v.population);

		if (v.eco.happiness > 0.6) {
			v.mood = 'Prosperous';
		} else if (v.eco.happiness > 0.35) {
			v.mood = 'Stable';
		} else {
			v.mood = 'Tense';
		}

		v.history.days.push(day);
		v.history.population.push(v.population);
		if (v.history.days.length > 30) {
			v.history.days.shift();
			v.history.population.shift();
		}
	}
}

// ── Economy tick: trade routes, caravan dispatch ──

// Track last trade day per city (avoids adding field to Settlement type)
const cityLastTradeDay = new Map<number, number>();

function tickEconomy_(
	settlements: Settlement[],
	villages: Village[],
	activeRoutes: TradeRoute[],
	day: number,
): void {
	// 1. Settle arrived trade routes
	for (let i = activeRoutes.length - 1; i >= 0; i--) {
		const route = activeRoutes[i];
		if (day >= route.arrivalDay) {
			// Find destination and origin economies
			const dest = settlements.find(s => s.id === route.destId)
				?? villages.find(v => v.id + 10_000 === route.destId);
			const origin = settlements.find(s => s.id === route.originId)
				?? villages.find(v => v.id + 10_000 === route.originId);
			if (dest && origin) {
				settleTradeRoute(route, dest.eco, origin.eco);
			}

			activeRoutes.splice(i, 1);
		}
	}

	// 2. Villages dispatch peasant traders to nearest city
	for (const v of villages) {
		const city = settlements.find(s => s.id === v.nearestCityId);
		if (!city) {
			continue;
		}

		const route = findBestTradeRoute(
			{
				id: v.id + 10_000, x: v.x, y: v.y, eco: v.eco,
			},
			[{
				id: city.id, x: city.x, y: city.y, eco: city.eco,
			}],
			true,
			day,
			v.lastTradeDay,
		);
		if (route) {
			activeRoutes.push(route);
			v.lastTradeDay = day;
		}
	}

	// 3. Cities dispatch caravans to other cities
	for (const city of settlements) {
		const destinations = settlements
			.filter(s => s.id !== city.id)
			.map(s => ({
				id: s.id, x: s.x, y: s.y, eco: s.eco,
			}));

		// Also include villages as potential destinations for goods
		for (const v of villages) {
			destinations.push({
				id: v.id + 10_000, x: v.x, y: v.y, eco: v.eco,
			});
		}

		const lastDay = cityLastTradeDay.get(city.id) ?? 0;
		const route = findBestTradeRoute(
			{
				id: city.id, x: city.x, y: city.y, eco: city.eco,
			},
			destinations,
			false,
			day,
			lastDay,
		);
		if (route) {
			activeRoutes.push(route);
			cityLastTradeDay.set(city.id, day);
		}
	}
}
