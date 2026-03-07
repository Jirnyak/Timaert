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
	type GameEvent,
	type DialogChoice,
} from './event-types';
import {type LogicNode, createNode} from './logic-nodes';
import {NPCType} from './npc';

// ── Encounter data table — pure data, no logic ──

type EncounterDef = {
	title: string;
	description: string;
	choices: DialogChoice[];
};

function buildEncounterTable(): EncounterDef[] {
	const rng = () => Math.random();

	const shrineOffering: GameEvent = rng() > 0.4
		? {
			tag: EventTag.PlayerGoldChange,
			delta: 50,
			newTotal: 0,
		}
		: {
			tag: EventTag.ApplyEffect,
			target: 'player',
			effectType: 'damage_hp',
			value: 25,
		};

	return [
		{
			title: 'Hidden Cache',
			description: 'You stumble upon a hollow tree with a leather pouch inside.',
			choices: [
				{
					label: 'Take it',
					effects: [{
						tag: EventTag.PlayerGoldChange,
						delta: 15 + Math.floor(rng() * 30),
						newTotal: 0,
					}],
				},
				{label: 'Leave it'},
			],
		},
		{
			title: 'Herb Patch',
			description: 'Fragrant medicinal herbs grow by the roadside.',
			choices: [
				{
					label: 'Gather herbs (+HP)',
					effects: [{
						tag: EventTag.ApplyEffect,
						target: 'player',
						effectType: 'heal_hp',
						value: 20 + Math.floor(rng() * 20),
					}],
				},
				{label: 'Ignore'},
			],
		},
		{
			title: 'Abandoned Campfire',
			description: 'A still-warm campfire with leftover rations.',
			choices: [
				{
					label: 'Rest and eat',
					effects: [
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'restore_sp',
							value: 9999,
						},
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'heal_hp',
							value: 15,
						},
					],
				},
				{
					label: 'Search the area',
					effects: [{
						tag: EventTag.PlayerGoldChange,
						delta: rng() > 0.5 ? 25 : 0,
						newTotal: 0,
					}],
				},
			],
		},
		{
			title: 'Traveling Merchant',
			description: 'A merchant rests by the road. "Care to trade or share a meal?"',
			choices: [
				{
					label: 'Buy rations (50g)',
					effects: [
						{
							tag: EventTag.PlayerGoldChange,
							delta: -50,
							newTotal: 0,
						},
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'heal_hp',
							value: 50,
						},
					],
				},
				{
					label: 'Rob him',
					effects: [{
						tag: EventTag.BattleStart,
						enemyName: 'Angry Merchant',
						enemyType: NPCType.Merchant,
						enemyLevel: 3,
					}],
				},
				{label: 'Leave'},
			],
		},
		{
			title: 'Beggar',
			description: 'A ragged man asks for a coin. "Bless you, traveler."',
			choices: [
				{
					label: 'Give 10g',
					effects: [{
						tag: EventTag.PlayerGoldChange,
						delta: -10,
						newTotal: 0,
					}],
				},
				{label: 'Ignore'},
			],
		},
		{
			title: 'Lost Child',
			description: 'A crying child has lost their parents.',
			choices: [
				{
					label: 'Help (+XP)',
					effects: [{
						tag: EventTag.ApplyEffect,
						target: 'player',
						effectType: 'grant_xp',
						value: 25,
					}],
				},
				{label: 'Ignore'},
			],
		},
		{
			title: 'Bard',
			description: 'A bard offers to sing a song of your deeds.',
			choices: [
				{
					label: 'Listen',
					effects: [{
						tag: EventTag.ApplyEffect,
						target: 'player',
						effectType: 'restore_mp',
						value: 9999,
					}],
				},
				{
					label: 'Tip 20g',
					effects: [
						{
							tag: EventTag.PlayerGoldChange,
							delta: -20,
							newTotal: 0,
						},
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'grant_xp',
							value: 15,
						},
					],
				},
			],
		},
		{
			title: 'Bandit Ambush',
			description: 'You hear a twig snap. "Your money or your life!"',
			choices: [
				{
					label: 'Fight!',
					effects: [{
						tag: EventTag.BattleStart,
						enemyName: 'Highwayman',
						enemyType: NPCType.Bandit,
						enemyLevel: 2,
					}],
				},
				{
					label: 'Pay 100g',
					effects: [{
						tag: EventTag.PlayerGoldChange,
						delta: -100,
						newTotal: 0,
					}],
				},
			],
		},
		{
			title: 'Wolf Pack',
			description: 'Growling shadows emerge from the bushes. Starving wolves.',
			choices: [
				{
					label: 'Defend yourself',
					effects: [{
						tag: EventTag.BattleStart,
						enemyName: 'Alpha Wolf',
						enemyType: NPCType.Bandit,
						enemyLevel: 2,
					}],
				},
				{
					label: 'Run (-SP)',
					effects: [{
						tag: EventTag.ApplyEffect,
						target: 'player',
						effectType: 'drain_sp',
						value: 30,
					}],
				},
			],
		},
		{
			title: 'Trap!',
			description: 'You step into a snare trap!',
			choices: [
				{
					label: 'Break free (-15 HP)',
					effects: [{
						tag: EventTag.ApplyEffect,
						target: 'player',
						effectType: 'damage_hp',
						value: 15,
					}],
				},
				{
					label: 'Wait for help',
					effects: [{
						tag: EventTag.BattleStart,
						enemyName: 'Trapper',
						enemyType: NPCType.Bandit,
						enemyLevel: 3,
					}],
				},
			],
		},
		{
			title: 'Duel Challenge',
			description: 'A wandering knight challenges you to a duel for honor.',
			choices: [
				{
					label: 'Accept',
					effects: [{
						tag: EventTag.BattleStart,
						enemyName: 'Knight Errant',
						enemyType: NPCType.Peasant,
						enemyLevel: 5,
					}],
				},
				{label: 'Decline'},
			],
		},
		{
			title: 'Mysterious Shrine',
			description: 'An ancient stone shrine pulses with faint light.',
			choices: [
				{
					label: 'Pray',
					effects: [
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'restore_hp',
							value: 9999,
						},
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'restore_mp',
							value: 9999,
						},
					],
				},
				{
					label: 'Take the offering',
					effects: [shrineOffering],
				},
			],
		},
		{
			title: 'Black Monolith',
			description: 'A jagged shard of obsidian rises from the earth, consuming the light around it.',
			choices: [
				{
					label: 'Study the runes',
					effects: [
						{
							tag: EventTag.CodexUnlock,
							entryId: 'cosmology',
						},
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'grant_xp',
							value: 50,
						},
					],
				},
				{
					label: 'Smash the shard',
					effects: [
						{
							tag: EventTag.ReputationChange,
							faction: 'empire',
							delta: 5,
							newValue: 0,
						},
						{
							tag: EventTag.ReputationChange,
							faction: 'cults',
							delta: -10,
							newValue: 0,
						},
					],
				},
			],
		},
		{
			title: 'Magical Flux',
			description: 'The air shimmers like oil on water. Pure Magic erupts from a leyline!',
			choices: [
				{
					label: 'Absorb the energy',
					effects: [
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'restore_mp',
							value: 40,
						},
						{
							tag: EventTag.ReputationChange,
							faction: 'magika',
							delta: 3,
							newValue: 0,
						},
					],
				},
				{
					label: 'Channel into a spell',
					effects: [{
						tag: EventTag.ApplyEffect,
						target: 'player',
						effectType: 'grant_xp',
						value: 30,
					}],
				},
			],
		},
		{
			title: 'Witch\'s Hut',
			description: 'Smoke curls from a crooked chimney. A witch peers at you.',
			choices: [
				{
					label: 'Ask for a potion (30g)',
					effects: [
						{
							tag: EventTag.PlayerGoldChange,
							delta: -30,
							newTotal: 0,
						},
						{
							tag: EventTag.ApplyEffect,
							target: 'player',
							effectType: 'restore_hp',
							value: 9999,
						},
					],
				},
				{
					label: 'Attack the witch',
					effects: [{
						tag: EventTag.BattleStart,
						enemyName: 'Forest Witch',
						enemyType: NPCType.Witch,
						enemyLevel: 7,
					}],
				},
				{label: 'Leave'},
			],
		},
	];
}

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
