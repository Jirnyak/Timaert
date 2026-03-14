// === Flight — terrain bypass ===
import type {Spell, SpellEffect, SpellAuraContext} from './spell-types';

// ── Rendering: blue aura ring with drifting motes ───────────────
const CORE = '#aaddff';
const GLOW = '#6699cc';
const TRAIL = '#334466';
const RING_PULSE = 3;
const MOTE_COUNT = 4;

function renderAura(ac: SpellAuraContext): void {
	const {ctx, sx, sy, radius, time} = ac;
	const pulse = 0.85 + 0.15 * Math.sin(time * RING_PULSE);
	const r = radius * 1.8 * pulse;

	ctx.save();

	// Wide faint ring
	ctx.globalAlpha = 0.08;
	ctx.strokeStyle = TRAIL;
	ctx.lineWidth = 6;
	ctx.beginPath();
	ctx.arc(sx, sy, r * 1.2, 0, Math.PI * 2);
	ctx.stroke();

	// Main ring
	ctx.globalAlpha = 0.2;
	ctx.strokeStyle = GLOW;
	ctx.lineWidth = 2;
	ctx.beginPath();
	ctx.arc(sx, sy, r, 0, Math.PI * 2);
	ctx.stroke();

	// Drifting upward motes
	for (let i = 0; i < MOTE_COUNT; i++) {
		const a = time * 1.5 + (i * Math.PI * 2) / MOTE_COUNT;
		const drift = (time * 20 + i * 7) % (r * 0.6);
		const px = sx + Math.cos(a) * r * 0.7;
		const py = sy - drift;
		ctx.globalAlpha = 0.4 * (1 - drift / (r * 0.6));
		ctx.fillStyle = CORE;
		ctx.beginPath();
		ctx.arc(px, py, 1.5, 0, Math.PI * 2);
		ctx.fill();
	}

	ctx.restore();
}

const effect: SpellEffect = {renderAura};

// ── Spell definition ────────────────────────────────────────────
export const flight: Spell = {
	id: 'flight',
	name: 'Flight',
	icon: '🕊',
	tags: ['air', 'arcane'],
	tier: 3,
	rarity: 'rare',
	manaCost: 0,
	cooldown: 0,
	castTime: 0,
	sustained: true,
	manaDrain: 20,
	scaling: {power: 0, duration: 1, radius: 0},
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
		statusEffect: 'flying',
		statusDuration: 0,
	},
	macro: {
		type: 'ignore_terrain',
		power: 1,
		duration: 12,
	},
	effect,
	pros: ['Ignore all terrain penalties', 'Fly over obstacles', 'Strategic repositioning'],
	cons: ['High mana drain', 'No combat benefit', 'Blocked indoors'],
	description: 'Rise above the ground and soar. Walls, rivers, mountains — none of it matters while you fly. But the magic fades fast, and the fall is unforgiving.',
};
