// === Ice Shard — high burst single-target with slow ===
import type {Spell} from './spell-types';

export const iceShard: Spell = {
	id: 'ice_shard',
	name: 'Ice Shard',
	icon: '❄',
	tags: ['ice'],
	tier: 2,
	rarity: 'uncommon',
	manaCost: 30,
	cooldown: 1.5,
	castTime: 0.2,	sustained: false,
	manaDrain: 0,	scaling: {power: 1.4, duration: 0.3, radius: 0},
	micro: {
		shape: 'projectile',
		baseDamage: 40,
		baseHeal: 0,
		baseRadius: 0,
		chainCount: 0,
		chainDecay: 0,
		speed: 350,
		duration: 0,
		friendlyFire: false,
		statusEffect: 'chilled',
		statusDuration: 4,
	},
	macro: {
		type: 'buff_army',
		power: -5,
		duration: 1,
	},
	pros: ['High single-target burst', 'Chill slows enemy', 'No friendly fire'],
	cons: ['Single target only', 'Short cooldown still matters', 'Weak vs crowds'],
	description: 'A razor-sharp shard of magical ice that pierces flesh and numbs the soul. Excellent against bosses and elites — useless against a horde.',
};
