// === Random Events System ===

import type {PlayerState} from './state';
import {NPCType} from './npc';

export type EventChoice = {
	label: string;
	effect: (player: PlayerState) => EventResult;
};

export type EventResult = {
	message: string;
	startBattle?: {enemyName: string; enemyType: NPCType; enemyLevel: number};
};

export type RandomEvent = {
	title: string;
	description: string;
	choices: EventChoice[];
};

function rng(): number {
	return Math.random();
}

const EVENT_DB: RandomEvent[] = [
	// --- Discovery & Fortune ---
	{
		title: 'Hidden Cache',
		description: 'You stumble upon a hollow tree with a leather pouch inside.',
		choices: [
			{
				label: 'Take it',
				effect(player) {
					const gold = 15 + Math.floor(rng() * 30);
					player.gold += gold;
					return {message: `Found ${gold} gold!`};
				},
			},
			{
				label: 'Leave it',
				effect() {
					return {message: 'You walk away.'};
				},
			},
		],
	},
	{
		title: 'Herb Patch',
		description: 'Fragrant medicinal herbs grow by the roadside.',
		choices: [
			{
				label: 'Gather herbs (+HP)',
				effect(player) {
					const heal = 20 + Math.floor(rng() * 20);
					player.combatStats.currentHp = Math.min(
						player.combatStats.currentHp + heal,
						player.combatStats.maxHp,
					);
					return {message: `Restored ${heal} HP.`};
				},
			},
			{
				label: 'Ignore',
				effect() {
					return {message: 'You continue on your way.'};
				},
			},
		],
	},
	{
		title: 'Abandoned Campfire',
		description: 'A still-warm campfire with leftover rations.',
		choices: [
			{
				label: 'Rest and eat',
				effect(player) {
					player.combatStats.currentSp = player.combatStats.maxSp;
					player.combatStats.currentHp = Math.min(
						player.combatStats.currentHp + 15,
						player.combatStats.maxHp,
					);
					return {message: 'Stamina restored. Gained 15 HP.'};
				},
			},
			{
				label: 'Search the area',
				effect(player) {
					if (rng() > 0.5) {
						player.gold += 25;
						return {message: 'Found 25 gold hidden nearby!'};
					}

					return {message: 'Nothing else of value.'};
				},
			},
		],
	},

	// --- Encounters & Trade ---
	{
		title: 'Traveling Merchant',
		description: 'A merchant rests by the road. "Care to trade or share a meal?"',
		choices: [
			{
				label: 'Buy rations (50g)',
				effect(player) {
					if (player.gold < 50) {
						return {message: 'Not enough gold!'};
					}

					player.gold -= 50;
					player.combatStats.currentHp = Math.min(
						player.combatStats.currentHp + 50,
						player.combatStats.maxHp,
					);
					return {message: 'Bought rations. Restored 50 HP.'};
				},
			},
			{
				label: 'Rob him',
				effect() {
					return {
						message: 'The merchant draws a blade!',
						startBattle: {
							enemyName: 'Angry Merchant',
							enemyType: NPCType.Merchant,
							enemyLevel: 3,
						},
					};
				},
			},
			{
				label: 'Leave',
				effect() {
					return {message: 'You tip your hat and move on.'};
				},
			},
		],
	},
	{
		title: 'Beggar',
		description: 'A ragged man asks for a coin. "Bless you, traveler."',
		choices: [
			{
				label: 'Give 10g',
				effect(player) {
					if (player.gold < 10) {
						return {message: 'You have nothing to give.'};
					}

					player.gold -= 10;
					return {message: 'The beggar blesses you warmly.'};
				},
			},
			{
				label: 'Ignore',
				effect() {
					return {message: 'You walk past.'};
				},
			},
		],
	},
	{
		title: 'Lost Child',
		description: 'A crying child has lost their parents.',
		choices: [
			{
				label: 'Help (+XP)',
				effect(player) {
					player.levelData.exp += 25;
					return {message: 'You reunite the child. Gained 25 XP.'};
				},
			},
			{
				label: 'Ignore',
				effect() {
					return {message: 'You continue walking.'};
				},
			},
		],
	},
	{
		title: 'Bard',
		description: 'A bard offers to sing a song of your deeds.',
		choices: [
			{
				label: 'Listen',
				effect(player) {
					player.combatStats.currentMp = player.combatStats.maxMp;
					return {message: 'The music restores your spirit. MP fully restored.'};
				},
			},
			{
				label: 'Tip 20g',
				effect(player) {
					if (player.gold < 20) {
						return {message: 'You enjoy the song but have no coin to spare.'};
					}

					player.gold -= 20;
					player.levelData.exp += 15;
					return {message: 'The bard composes a verse about you. Gained 15 XP.'};
				},
			},
		],
	},

	// --- Combat & Danger ---
	{
		title: 'Bandit Ambush',
		description: 'You hear a twig snap. "Your money or your life!"',
		choices: [
			{
				label: 'Fight!',
				effect() {
					return {
						message: 'You draw your weapon!',
						startBattle: {
							enemyName: 'Highwayman',
							enemyType: NPCType.Bandit,
							enemyLevel: 2,
						},
					};
				},
			},
			{
				label: 'Pay 100g',
				effect(player) {
					if (player.gold >= 100) {
						player.gold -= 100;
						return {message: 'You hand over the gold. The bandits vanish.'};
					}

					return {
						message: 'Not enough gold! They attack!',
						startBattle: {
							enemyName: 'Highwayman',
							enemyType: NPCType.Bandit,
							enemyLevel: 2,
						},
					};
				},
			},
		],
	},
	{
		title: 'Wolf Pack',
		description: 'Growling shadows emerge from the bushes. Starving wolves.',
		choices: [
			{
				label: 'Defend yourself',
				effect() {
					return {
						message: 'The alpha lunges at you!',
						startBattle: {
							enemyName: 'Alpha Wolf',
							enemyType: NPCType.Bandit,
							enemyLevel: 2,
						},
					};
				},
			},
			{
				label: 'Run (-SP)',
				effect(player) {
					const cost = 30;
					player.combatStats.currentSp = Math.max(0, player.combatStats.currentSp - cost);
					return {message: `You sprint away! Lost ${cost} SP.`};
				},
			},
		],
	},
	{
		title: 'Trap!',
		description: 'You step into a snare trap!',
		choices: [
			{
				label: 'Break free (-15 HP)',
				effect(player) {
					player.combatStats.currentHp -= 15;
					return {message: 'You wrench free, bruised but alive.'};
				},
			},
			{
				label: 'Wait for help',
				effect() {
					return {
						message: 'A trapper appears... with hostile intent!',
						startBattle: {
							enemyName: 'Trapper',
							enemyType: NPCType.Bandit,
							enemyLevel: 3,
						},
					};
				},
			},
		],
	},
	{
		title: 'Duel Challenge',
		description: 'A wandering knight challenges you to a duel for honor.',
		choices: [
			{
				label: 'Accept',
				effect() {
					return {
						message: 'En garde!',
						startBattle: {
							enemyName: 'Knight Errant',
							enemyType: NPCType.Peasant,
							enemyLevel: 5,
						},
					};
				},
			},
			{
				label: 'Decline',
				effect() {
					return {message: 'The knight scoffs and rides away.'};
				},
			},
		],
	},

	// --- Mystery & Magic ---
	{
		title: 'Mysterious Shrine',
		description: 'An ancient stone shrine pulses with faint light.',
		choices: [
			{
				label: 'Pray',
				effect(player) {
					player.combatStats.currentHp = player.combatStats.maxHp;
					player.combatStats.currentMp = player.combatStats.maxMp;
					return {message: 'Divine energy restores you fully!'};
				},
			},
			{
				label: 'Take the offering',
				effect(player) {
					if (rng() > 0.4) {
						player.gold += 50;
						return {message: 'Found 50 gold among the offerings.'};
					}

					player.combatStats.currentHp -= 25;
					return {message: 'A curse strikes you! Lost 25 HP.'};
				},
			},
		],
	},
	{
		title: 'Imperial Mage-Hunters',
		description: 'A patrol of armored men with golden sun sigils blocks your path. "By order of the Great Eunuchs, all travelers must be purged of arcane taint!"',
		choices: [
			{
				label: 'Submit to the search',
				effect(player) {
					// If player has high MP, they are suspected
					if (player.combatStats.currentMp > 20) {
						return {
							message: '"I smell the rot of Pure Magic on you!" They attack!',
							startBattle: { enemyName: 'Mage-Hunter Captain', enemyType: NPCType.Guard, enemyLevel: 4 }
						};
					}
					player.reputation.empire = (player.reputation.empire ?? 0) + 2;
					return { message: 'They find nothing and let you pass with a cold nod. (+2 Empire Rep)' };
				},
			},
			{
				label: 'Bribe them (100g)',
				effect(player) {
					if (player.gold < 100) return { message: '"You mock us with empty pockets?"', startBattle: { enemyName: 'Enraged Guard', enemyType: NPCType.Guard, enemyLevel: 3 } };
					player.gold -= 100;
					return { message: 'The armor clinks as they pocket your gold. "Move along, citizen."' };
				},
			}
		],
	},
	{
		title: 'The Black Monolith',
		description: 'A jagged shard of obsidian rises from the earth, consuming the light around it. You feel a void pulling at your very thoughts.',
		choices: [
			{
				label: 'Study the runes (Requires INT 5)',
				effect(player) {
					if (player.attributes.int >= 5) {
						// Unlock lore
						if (!player.codexUnlocked.includes('cosmology')) player.codexUnlocked.push('cosmology');
						player.levelData.exp += 50;
						return { message: 'The whispers speak of Dead Gods. You gain dark insight and 50 XP.' };
					}
					return { message: 'The script is incomprehensible, leaving you with a splitting headache.' };
				},
			},
			{
				label: 'Smash the shard',
				effect(player) {
					player.reputation.empire = (player.reputation.empire ?? 0) + 5;
					player.reputation.cults = (player.reputation.cults ?? 0) - 10;
					return { message: 'The stone shatters with a scream. You feel the world breathe easier. (+5 Empire Rep, -10 Cults Rep)' };
				},
			}
		],
	},
	{
		title: 'Magical Flux',
		description: 'The air begins to shimmer like oil on water. A surge of Pure Magic is erupting from a nearby leyline!',
		choices: [
			{
				label: 'Absorb the energy',
				effect(player) {
					player.combatStats.currentMp = Math.min(player.combatStats.currentMp + 40, player.combatStats.maxMp);
					player.reputation.magika = (player.reputation.magika ?? 0) + 3;
					return { message: 'The raw power burns, but your mind feels expanded. (+40 MP, +3 Magika Rep)' };
				},
			},
			{
				label: 'Channel into a spell',
				effect(player) {
					player.levelData.exp += 30;
					return { message: 'You safely dissipate the rift. Your technique improves. (+30 XP)' };
				},
			}
		],
	},
	{
		title: 'Witch\'s Hut',
		description: 'Smoke curls from a crooked chimney. A witch peers at you.',
		choices: [
			{
				label: 'Ask for a potion (30g)',
				effect(player) {
					if (player.gold < 30) {
						return {message: 'The witch cackles. "Come back with coin!"'};
					}

					player.gold -= 30;
					player.combatStats.currentHp = player.combatStats.maxHp;
					return {message: 'The potion restores you to full health!'};
				},
			},
			{
				label: 'Attack the witch',
				effect() {
					return {
						message: 'The witch shrieks and raises her staff!',
						startBattle: {
							enemyName: 'Forest Witch',
							enemyType: NPCType.Witch,
							enemyLevel: 7,
						},
					};
				},
			},
			{
				label: 'Leave',
				effect() {
					return {message: 'You hurry past the hut.'};
				},
			},
		],
	},
];

// Returns a random event, or undefined if no event triggers
export function rollForEvent(stepsTaken: number): RandomEvent | undefined {
	// Low base chance; ramps up after many steps without an event
	// ~1% at step 0, reaches ~8% around step 80, caps at 12%
	if (stepsTaken < 15) {
		return undefined; // Guaranteed grace period after last event
	}

	const chance = 0.01 + Math.min((stepsTaken - 15) * 0.001, 0.11);
	if (rng() > chance) {
		return undefined;
	}

	return EVENT_DB[Math.floor(rng() * EVENT_DB.length)];
}
