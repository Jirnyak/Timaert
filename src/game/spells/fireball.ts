// === Fireball — AoE blast with friendly fire ===
import type {Spell} from './spell-types';

export const fireball: Spell = {
	id: 'fireball',
	name: 'Fireball',
	icon: '🔥',
	tags: ['fire'],
	tier: 2,
	rarity: 'common',
	manaCost: 60,
	cooldown: 2,
	castTime: 0.3,	sustained: false,
	manaDrain: 0,	scaling: {power: 1.2, duration: 0, radius: 0.5},
	micro: {
		shape: 'projectile',
		baseDamage: 30,
		baseHeal: 0,
		baseRadius: 48,
		chainCount: 0,
		chainDecay: 0,
		speed: 280,
		duration: 0,
		friendlyFire: true,
		statusEffect: 'burning',
		statusDuration: 3,
	},
	macro: {
		type: 'damage_region',
		power: 10,
		duration: 0,
	},
	pros: ['Strong AoE damage', 'Burning DOT', 'Good at chokepoints'],
	cons: ['Friendly fire', 'Cast time', 'Higher mana cost'],
	description: 'Hurls a ball of fire that explodes on impact, burning everything in the blast radius — allies included. The classic.',
};
