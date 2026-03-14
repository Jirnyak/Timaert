// === Magic Bolt — cheap reliable single-target ===
import type {Spell, SpellEffect, SpellRenderContext} from './spell-types';

// ── Rendering: short elongated violet streak ────────────────────

const CORE = '#e0c0ff';
const GLOW = '#8844cc';
const TRAIL = '#6622aa';
const ELONGATION = 3;
const GLOW_SIZE = 2;
const PULSE_RATE = 8;

function renderProjectile(rc: SpellRenderContext): void {
	const {ctx, sx, sy, sr, vx, vy, time} = rc;
	const speed = Math.sqrt(vx * vx + vy * vy);
	if (speed < 0.01) {
		return;
	}

	const nx = vx / speed;
	const ny = vy / speed;
	const t = time * PULSE_RATE;
	const pulse = 0.8 + 0.2 * Math.sin(t);

	const halfLength = sr * ELONGATION * pulse;
	const halfW = sr * 0.6;

	ctx.save();
	ctx.translate(sx, sy);
	ctx.rotate(Math.atan2(ny, nx));

	// Trail glow
	ctx.globalAlpha = 0.2;
	ctx.fillStyle = TRAIL;
	ctx.beginPath();
	ctx.ellipse(-halfLength * 0.5, 0, halfLength * 1.5, halfW * GLOW_SIZE, 0, 0, Math.PI * 2);
	ctx.fill();

	// Outer glow
	ctx.globalAlpha = 0.35;
	ctx.fillStyle = GLOW;
	ctx.beginPath();
	ctx.ellipse(0, 0, halfLength * 1.2, halfW * 1.5, 0, 0, Math.PI * 2);
	ctx.fill();

	// Core — elongated bright
	ctx.globalAlpha = 1;
	ctx.fillStyle = CORE;
	ctx.beginPath();
	ctx.ellipse(0, 0, halfLength, halfW, 0, 0, Math.PI * 2);
	ctx.fill();

	// Hot tip
	ctx.fillStyle = '#fff';
	ctx.beginPath();
	ctx.ellipse(halfLength * 0.3, 0, halfW * 0.5, halfW * 0.4, 0, 0, Math.PI * 2);
	ctx.fill();

	ctx.restore();
}

const effect: SpellEffect = {renderProjectile};

// ── Spell definition ────────────────────────────────────────────

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
	effect,
	pros: ['No cooldown', 'Low mana cost', 'Fast projectile'],
	cons: ['Weak scaling at high tiers', 'No AoE', 'No utility'],
	description: 'A bolt of raw arcane energy. Cheap, fast, reliable — the bread and butter of every spell-caster. Won\'t win wars, but keeps you alive.',
};
