// === Built-in Logic Nodes ===
//
// Every node is a single self-linking unit:
//   condition fulfilled → effect runs → self queued → waits again.
// No gate→show pairs. No special flags.
//
// The clock test demonstrates a pure-predicate condition slot:
//   { check: (bus) => ... } — any boolean formula, not tied to a tag.

import {
	EventTag,
	type ShowDialogEvent,
} from './event-types';
import {type LogicNode, createNode} from './logic-nodes';
import {buildEncounterTable} from './plot/encounters';

// ── Built-in nodes ──

export function createBuiltinNodes(): LogicNode[] {
	const encounters = buildEncounterTable();

	return [
		// Random encounter: condition + effect in one node, self-links
		createNode({
			id: 'enc_random',
			label: 'Random Encounter',
			conditions: [{
				tag: EventTag.PlayerMove,
				predicate(ev) {
					const steps = 'steps' in ev ? (ev as {steps: number}).steps : 0;
					if (steps < 15) {
						return false;
					}

					const chance = 0.01 + Math.min((steps - 15) * 0.001, 0.11);
					return Math.random() < chance;
				},
			}],
			mask: [1],
			next: ['enc_random'],
			tags: ['encounter'],
			effect(ctx) {
				const def = encounters[Math.floor(Math.random() * encounters.length)];
				const dialog: ShowDialogEvent = {
					tag: EventTag.ShowDialog,
					title: def.title,
					description: def.description,
					choices: def.choices,
				};
				ctx.bus.emit(dialog);
			},
		}),

		// Level up: single node, self-links
		createNode({
			id: 'sys_level_up',
			label: 'Level Up',
			conditions: [{tag: EventTag.PlayerLevelUp}],
			mask: [1],
			next: ['sys_level_up'],
			tags: ['system'],
			effect(ctx) {
				const ev = ctx.bus.lastTickEvents.find(event => event.tag === EventTag.PlayerLevelUp);
				const level = ev && 'newLevel' in ev
					? (ev as {newLevel: number}).newLevel
					: 0;
				ctx.bus.emit({
					tag: EventTag.ShowDialog,
					title: 'Level Up!',
					description: `You have reached level ${level}! Your abilities grow stronger.`,
					choices: [{label: 'Continue'}],
				});
			},
		}),

		// Settlement greeting: single node, self-links
		createNode({
			id: 'sys_settlement',
			label: 'Settlement Greeting',
			conditions: [{tag: EventTag.PlayerEnterSettlement}],
			mask: [1],
			next: ['sys_settlement'],
			tags: ['system'],
			effect(ctx) {
				const ev = ctx.bus.lastTickEvents.find(event => event.tag === EventTag.PlayerEnterSettlement);
				const name = ev && 'settlementName' in ev
					? (ev as {settlementName: string}).settlementName
					: 'Unknown';
				ctx.bus.emit({
					tag: EventTag.ShowDialog,
					title: `Welcome to ${name}`,
					description: 'The gates open before you. Merchants hawk their wares and guards patrol the walls.',
					choices: [{label: 'Enter'}],
				});
			},
		}),

		// ── Test: hourly clock ──
		// Pure-predicate condition: any math / check that returns 0 or 1.
		// Not tied to a tag — demonstrates universal condition slots.
		createNode({
			id: 'test_clock',
			label: 'Hourly Clock',
			conditions: [{
				check(bus) {
					return bus.lastTickEvents.some(ev => ev.tag === EventTag.TimeAdvance);
				},
			}],
			mask: [1],
			next: ['test_clock'],
			tags: ['test'],
			effect(ctx) {
				const ev = ctx.bus.lastTickEvents.find(event => event.tag === EventTag.TimeAdvance);
				const hour = ev && 'hour' in ev ? (ev as {hour: number}).hour : 0;
				const suffix = hour < 12 ? 'AM' : 'PM';
				const display = hour === 0 ? 12 : (hour > 12 ? hour - 12 : hour);
				ctx.bus.emit({
					tag: EventTag.ShowDialog,
					title: `It is ${display} ${suffix}`,
					description: `The time is now ${display}:00 ${suffix}.`,
					choices: [{label: 'OK'}],
				});
			},
		}),
	];
}

/** Node IDs that should be activated at game start. */
export const INITIAL_ACTIVE_NODES = [
	'enc_random',
	'sys_level_up',
	'sys_settlement',
	'test_clock',
];
