// === Lightning Chain — adaptive multi-target ===
import type {Spell} from './spell-types';

export const lightningChain: Spell = {
	id: 'lightning_chain',
	name: 'Lightning Chain',
	icon: '⛧',
	tags: ['lightning'],
	tier: 3,
	rarity: 'rare',
	manaCost: 60,
	cooldown: 4,
	castTime: 0.1,	sustained: false,
	manaDrain: 0,	scaling: {power: 1, duration: 0, radius: 0.4},
	micro: {
		shape: 'chain',
		baseDamage: 22,
		baseHeal: 0,
		baseRadius: 0,
		chainCount: 4,
		chainDecay: 0.7,
		speed: 0,
		duration: 0,
		friendlyFire: false,
		statusEffect: 'shocked',
		statusDuration: 2,
	},
	macro: {
		type: 'damage_region',
		power: 5,
		duration: 0,
	},
	pros: ['Hits up to 5 targets', 'Shock interrupts', 'Fast cast'],
	cons: ['Unpredictable jumps', 'Damage decays per jump', 'High mana'],
	description: 'Lightning arcs from the first target to nearby enemies, losing force with each jump. Brilliant against scattered groups — unreliable when you need precision.',
};
