// === Energy Beam — piercing line damage ===
import type {Spell} from './spell-types';

export const energyBeam: Spell = {
	id: 'energy_beam',
	name: 'Energy Beam',
	icon: '⚡',
	tags: ['arcane', 'light'],
	tier: 2,
	rarity: 'uncommon',
	manaCost: 100,
	cooldown: 2.5,
	castTime: 0.4,
	sustained: false,
	manaDrain: 0,
	scaling: {power: 1.1, duration: 0, radius: 0.3},
	micro: {
		shape: 'beam',
		baseDamage: 25,
		baseHeal: 0,
		baseRadius: 8,
		chainCount: 0,
		chainDecay: 0,
		speed: 0,
		duration: 0,
		friendlyFire: true,
		statusEffect: '',
		statusDuration: 0,
	},
	macro: undefined,
	pros: ['Pierces all enemies in line', 'Instant hit', 'Great vs formations'],
	cons: ['Requires aim', 'Friendly fire', 'Medium-high mana'],
	description: 'A searing beam of pure energy cuts through everything in its path. Devastating against enemies foolish enough to line up.',
};
