// === Encounter Content — random event definitions ===
//
// Layer 4 (Plot). Pure data: title + description + choice effects.
// Used by the enc_random node in node-registry.ts.
// Removing this file disables random encounters without breaking the engine.

import {EventTag, type GameEvent, type DialogChoice} from '../event-types';
import {NPCType} from '../npc';

export type EncounterDef = {
	title: string;
	description: string;
	choices: DialogChoice[];
};

export function buildEncounterTable(): EncounterDef[] {
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
