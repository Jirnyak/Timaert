// === Haste — speed buff for both layers ===
import type {Spell} from './spell-types';

export const haste: Spell = {
	id: 'haste',
	name: 'Haste',
	icon: '💨',
	tags: ['body', 'air'],
	tier: 2,
	rarity: 'uncommon',
	manaCost: 0,
	cooldown: 0,
	castTime: 0,
	sustained: true,
	manaDrain: 10,
	scaling: {power: 0.5, duration: 1.2, radius: 0},
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
		statusEffect: 'hasted',
		statusDuration: 0,
	},
	macro: {
		type: 'travel_speed',
		power: 1.5,
		duration: 8,
	},
	pros: ['Move + attack speed up', 'Great for kiting', 'Works on world map'],
	cons: ['No direct damage', 'Continuous mana drain', 'Buff upkeep tax'],
	description: 'Accelerates body and mind. In combat, you move and strike faster. On the world map, your party covers ground at supernatural speed.',
};
