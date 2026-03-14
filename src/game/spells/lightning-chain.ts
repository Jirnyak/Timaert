// === Lightning Chain — adaptive multi-target ===
import type {Spell, SpellEffect, SpellRenderContext} from './spell-types';

// ── Rendering: jagged spark bolt ────────────────────────────────
const CORE = '#ffffff';
const GLOW = '#ffee44';
const TRAIL = '#ffcc00';
const GLOW_SIZE = 3;
const PULSE_RATE = 20;

function renderProjectile(rc: SpellRenderContext): void {
	const {ctx, sx, sy, sr, vx, vy, time} = rc;

	const angle = Math.atan2(vy, vx);
	const flicker = 0.6 + 0.4 * Math.sin(time * PULSE_RATE);
	const r = sr * flicker;

	ctx.save();
	ctx.translate(sx, sy);
	ctx.rotate(angle);

	// Outer glow
	ctx.globalAlpha = 0.2;
	ctx.fillStyle = GLOW;
	ctx.beginPath();
	ctx.arc(0, 0, r * GLOW_SIZE, 0, Math.PI * 2);
	ctx.fill();

	// Jagged spark shape — 4-segment zigzag
	const length = r * 4;
	const jag = r * 1.2;
	ctx.globalAlpha = 0.7;
	ctx.strokeStyle = TRAIL;
	ctx.lineWidth = r * 0.8;
	ctx.lineCap = 'round';
	ctx.lineJoin = 'round';
	ctx.beginPath();
	ctx.moveTo(-length, 0);
	ctx.lineTo(-length * 0.5, -jag);
	ctx.lineTo(0, jag * 0.6);
	ctx.lineTo(length * 0.5, -jag * 0.4);
	ctx.lineTo(length, 0);
	ctx.stroke();

	// Core line
	ctx.globalAlpha = 1;
	ctx.strokeStyle = CORE;
	ctx.lineWidth = r * 0.3;
	ctx.beginPath();
	ctx.moveTo(-length, 0);
	ctx.lineTo(-length * 0.5, -jag);
	ctx.lineTo(0, jag * 0.6);
	ctx.lineTo(length * 0.5, -jag * 0.4);
	ctx.lineTo(length, 0);
	ctx.stroke();

	ctx.restore();
}

const effect: SpellEffect = {renderProjectile};

// ── Spell definition ────────────────────────────────────────────
export const lightningChain: Spell = {
	id: 'lightning_chain',
	name: 'Lightning Chain',
	icon: '⛧',
	tags: ['lightning'],
	tier: 3,
	rarity: 'rare',
	manaCost: 60,
	cooldown: 4,
	castTime: 0.1,
	sustained: false,
	manaDrain: 0,
	scaling: {power: 1, duration: 0, radius: 0.4},
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
	effect,
	pros: ['Hits up to 5 targets', 'Shock interrupts', 'Fast cast'],
	cons: ['Unpredictable jumps', 'Damage decays per jump', 'High mana'],
	description: 'Lightning arcs from the first target to nearby enemies, losing force with each jump. Brilliant against scattered groups — unreliable when you need precision.',
};
