// === Magic Bolt — cheap reliable single-target ===
import type {Spell} from './spell-types';

export const magicBolt: Spell = {
	id: 'magic_bolt',
	name: 'Magic Bolt',
	icon: '✦',
	tags: ['arcane'],
	tier: 1,
	rarity: 'common',
	manaCost: 10,
	cooldown: 0,
	castTime: 0,
	sustained: false,
	manaDrain: 0,
	scaling: {power: 1, duration: 0, radius: 0},
	micro: {
		shape: 'projectile',
		baseDamage: 12,
		baseHeal: 0,
		baseRadius: 0,
		chainCount: 0,
		chainDecay: 0,
		speed: 400,
		duration: 0,
		friendlyFire: false,
		statusEffect: '',
		statusDuration: 0,
	},
	macro: undefined,
	pros: ['No cooldown', 'Low mana cost', 'Fast projectile'],
	cons: ['Weak scaling at high tiers', 'No AoE', 'No utility'],
	description: 'A bolt of raw arcane energy. Cheap, fast, reliable — the bread and butter of every spell-caster. Won\'t win wars, but keeps you alive.',
};
