// === Ice Shard — high burst single-target with slow ===
import type {Spell, SpellEffect, SpellRenderContext} from './spell-types';

// ── Rendering: elongated crystalline diamond ────────────────────

const CORE = '#ffffff';
const GLOW = '#88ddff';
const TRAIL = '#4488cc';
const ELONGATION = 2.5;
const GLOW_SIZE = 2;
const PULSE_RATE = 6;

function renderProjectile(rc: SpellRenderContext): void {
	const {ctx, sx, sy, sr, vx, vy, time} = rc;
	const speed = Math.sqrt(vx * vx + vy * vy);
	if (speed < 0.01) {
		return;
	}

	const angle = Math.atan2(vy, vx);
	const t = time * PULSE_RATE;
	const shimmer = 0.85 + 0.15 * Math.sin(t);

	const halfLength = sr * ELONGATION;
	const halfW = sr * 0.5;

	ctx.save();
	ctx.translate(sx, sy);
	ctx.rotate(angle);

	// Frost trail
	ctx.globalAlpha = 0.15;
	ctx.fillStyle = TRAIL;
	ctx.beginPath();
	ctx.ellipse(-halfLength, 0, halfLength * 1.5, halfW * 2, 0, 0, Math.PI * 2);
	ctx.fill();

	// Glow
	ctx.globalAlpha = 0.3 * shimmer;
	ctx.fillStyle = GLOW;
	ctx.beginPath();
	ctx.ellipse(0, 0, halfLength * 1.3, halfW * GLOW_SIZE, 0, 0, Math.PI * 2);
	ctx.fill();

	// Diamond shard shape
	ctx.globalAlpha = 1;
	ctx.fillStyle = CORE;
	ctx.beginPath();
	ctx.moveTo(halfLength, 0);
	ctx.lineTo(0, halfW);
	ctx.lineTo(-halfLength * 0.5, 0);
	ctx.lineTo(0, -halfW);
	ctx.closePath();
	ctx.fill();

	// Inner glint
	ctx.fillStyle = '#fff';
	ctx.globalAlpha = shimmer;
	ctx.beginPath();
	ctx.ellipse(halfLength * 0.2, 0, halfW * 0.3, halfW * 0.2, 0, 0, Math.PI * 2);
	ctx.fill();

	ctx.restore();
}

const effect: SpellEffect = {renderProjectile};

// ── Spell definition ────────────────────────────────────────────

export const iceShard: Spell = {
	id: 'ice_shard',
	name: 'Ice Shard',
	icon: '❄',
	tags: ['ice'],
	tier: 2,
	rarity: 'uncommon',
	manaCost: 30,
	cooldown: 1.5,
	castTime: 0.2,
	sustained: false,
	manaDrain: 0,
	scaling: {power: 1.4, duration: 0.3, radius: 0},
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
	effect,
	pros: ['High single-target burst', 'Chill slows enemy', 'No friendly fire'],
	cons: ['Single target only', 'Short cooldown still matters', 'Weak vs crowds'],
	description: 'A razor-sharp shard of magical ice that pierces flesh and numbs the soul. Excellent against bosses and elites — useless against a horde.',
};
