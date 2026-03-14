// === Haste — speed buff for both layers ===
import type {Spell, SpellEffect, SpellAuraContext} from './spell-types';

// ── Rendering: green shimmer ring + rotating particles ──────────
const CORE = '#88ffaa';
const GLOW = '#44cc66';
const TRAIL = '#228844';
const PARTICLE_COUNT = 6;
const RING_PULSE = 6;

function renderAura(ac: SpellAuraContext): void {
	const {ctx, sx, sy, radius, time} = ac;
	const pulse = 0.8 + 0.2 * Math.sin(time * RING_PULSE);
	const r = radius * 1.6 * pulse;

	ctx.save();

	// Outer shimmer ring
	ctx.globalAlpha = 0.12;
	ctx.strokeStyle = TRAIL;
	ctx.lineWidth = 4;
	ctx.beginPath();
	ctx.arc(sx, sy, r * 1.3, 0, Math.PI * 2);
	ctx.stroke();

	// Mid ring
	ctx.globalAlpha = 0.25;
	ctx.strokeStyle = GLOW;
	ctx.lineWidth = 2;
	ctx.beginPath();
	ctx.arc(sx, sy, r, 0, Math.PI * 2);
	ctx.stroke();

	// Rotating particles
	for (let i = 0; i < PARTICLE_COUNT; i++) {
		const a = time * 3 + (i * Math.PI * 2) / PARTICLE_COUNT;
		const px = sx + Math.cos(a) * r;
		const py = sy + Math.sin(a) * r;
		ctx.globalAlpha = 0.5 + 0.3 * Math.sin(time * 8 + i);
		ctx.fillStyle = CORE;
		ctx.beginPath();
		ctx.arc(px, py, 2, 0, Math.PI * 2);
		ctx.fill();
	}

	ctx.restore();
}

const effect: SpellEffect = {renderAura};

// ── Spell definition ────────────────────────────────────────────
export const haste: Spell = {
	id: 'haste',
	name: 'Haste',
	icon: '💨',
	tags: ['body', 'air'],
	tier: 2,
	rarity: 'uncommon',
	manaCost: 0,
	cooldown: 0,
	castTime: 0,
	sustained: true,
	manaDrain: 10,
	scaling: {power: 0.5, duration: 1.2, radius: 0},
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
		statusEffect: 'hasted',
		statusDuration: 0,
	},
	macro: {
		type: 'travel_speed',
		power: 1.5,
		duration: 8,
	},
	effect,
	pros: ['Move + attack speed up', 'Great for kiting', 'Works on world map'],
	cons: ['No direct damage', 'Continuous mana drain', 'Buff upkeep tax'],
	description: 'Accelerates body and mind. In combat, you move and strike faster. On the world map, your party covers ground at supernatural speed.',
};
