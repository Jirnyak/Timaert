// === Fireball — AoE blast with friendly fire ===
import type {Spell, SpellEffect, SpellRenderContext} from './spell-types';

// ── Rendering: pulsing fire ball ────────────────────────────────

const CORE = '#ffcc00';
const GLOW = '#ff4400';
const TRAIL = '#ff2200';
const GLOW_SIZE = 3;
const PULSE_RATE = 4;

function renderProjectile(rc: SpellRenderContext): void {
	const {ctx, sx, sy, sr, time} = rc;
	const t = time * PULSE_RATE;
	const pulse = 0.85 + 0.15 * Math.sin(t);
	const glowR = sr * GLOW_SIZE * pulse;

	// Outer heat haze
	ctx.save();
	ctx.globalAlpha = 0.15;
	ctx.fillStyle = TRAIL;
	ctx.beginPath();
	ctx.arc(sx, sy, glowR * 1.5, 0, Math.PI * 2);
	ctx.fill();
	ctx.restore();

	// Mid glow
	ctx.save();
	ctx.globalAlpha = 0.35;
	ctx.fillStyle = GLOW;
	ctx.beginPath();
	ctx.arc(sx, sy, glowR, 0, Math.PI * 2);
	ctx.fill();
	ctx.restore();

	// Core
	ctx.fillStyle = CORE;
	ctx.beginPath();
	ctx.arc(sx, sy, sr * pulse, 0, Math.PI * 2);
	ctx.fill();

	// Bright center
	ctx.fillStyle = '#fff';
	ctx.beginPath();
	ctx.arc(sx, sy, sr * 0.35, 0, Math.PI * 2);
	ctx.fill();
}

const effect: SpellEffect = {renderProjectile};

// ── Spell definition ────────────────────────────────────────────

export const fireball: Spell = {
	id: 'fireball',
	name: 'Fireball',
	icon: '🔥',
	tags: ['fire'],
	tier: 2,
	rarity: 'common',
	manaCost: 60,
	cooldown: 2,
	castTime: 0.3,
	sustained: false,
	manaDrain: 0,
	scaling: {power: 1.2, duration: 0, radius: 0.5},
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
	effect,
	pros: ['Strong AoE damage', 'Burning DOT', 'Good at chokepoints'],
	cons: ['Friendly fire', 'Cast time', 'Higher mana cost'],
	description: 'Hurls a ball of fire that explodes on impact, burning everything in the blast radius — allies included. The classic.',
};
