// === Armageddon — ultimate devastation ===
import type {Spell} from './spell-types';

export const armageddon: Spell = {
	id: 'armageddon',
	name: 'Armageddon',
	icon: '☠',
	tags: ['fire', 'dark'],
	tier: 5,
	rarity: 'mythic',
	manaCost: 1000,
	cooldown: 120,
	castTime: 2,
	sustained: false,
	manaDrain: 0,
	scaling: {power: 2, duration: 0.5, radius: 1},
	micro: {
		shape: 'nova',
		baseDamage: 80,
		baseHeal: 0,
		baseRadius: 160,
		chainCount: 0,
		chainDecay: 0,
		speed: 0,
		duration: 0,
		friendlyFire: true,
		statusEffect: 'burning',
		statusDuration: 8,
	},
	macro: {
		type: 'damage_region',
		power: 50,
		duration: 0,
	},
	pros: ['Massive AoE', 'Battle-ending power', 'Burns everything'],
	cons: ['Friendly fire', '2s cast time', 'Enormous mana cost', 'Faction reputation hit', '2 min cooldown'],
	description: 'Rain fire and ruin upon the world. Everything burns — enemies, allies, buildings, reputation. The ultimate expression of magical supremacy and moral bankruptcy.',
};
