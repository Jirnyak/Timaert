// === World Tick — time advancement + settlement simulation ===
//
// Layer 1 (Macroworld). Pure game-state mutation, no UI dependencies.

import type {WorldTime, Settlement} from './state';
import type {EventBus} from './event-bus';
import {EventTag} from './event-types';
import {generateGarrison, addArmy} from './army';

/**
 * Advance world time by one minute.
 * On hour changes emits TimeAdvance.
 * On day changes runs per-settlement population simulation.
 */
export function advanceWorldMinute(
	worldTime: WorldTime,
	settlements: Settlement[],
	bus: EventBus,
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
		if (Math.random() > 0.5) {
			const change = s.mood === 'Prosperous'
				? 1
				: (s.mood === 'Revolt'
					? -2
					: 0);
			s.population = Math.max(
				0,
				s.population + change + Math.floor(Math.random() * 3) - 1,
			);
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
