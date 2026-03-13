// === Flight — terrain bypass ===
import type {Spell} from './spell-types';

export const flight: Spell = {
	id: 'flight',
	name: 'Flight',
	icon: '🕊',
	tags: ['air', 'arcane'],
	tier: 3,
	rarity: 'rare',
	manaCost: 0,
	cooldown: 0,
	castTime: 0,
	sustained: true,
	manaDrain: 20,
	scaling: {power: 0, duration: 1, radius: 0},
	micro: {
		shape: 'self',
		baseDamage: 0,
		baseHeal: 0,
		baseRadius: 0,
		chainCount: 0,
		chainDecay: 0,
		speed: 0,
		duration: 0,
		friendlyFire: false,
		statusEffect: 'flying',
		statusDuration: 0,
	},
	macro: {
		type: 'ignore_terrain',
		power: 1,
		duration: 12,
	},
	pros: ['Ignore all terrain penalties', 'Fly over obstacles', 'Strategic repositioning'],
	cons: ['High mana drain', 'No combat benefit', 'Blocked indoors'],
	description: 'Rise above the ground and soar. Walls, rivers, mountains — none of it matters while you fly. But the magic fades fast, and the fall is unforgiving.',
};
