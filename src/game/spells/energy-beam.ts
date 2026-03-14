// === Energy Beam — piercing line damage ===
import type {
	Spell, SpellEffect, SpellRenderContext,
	SpellSpawnContext, SpellProjectileDescriptor, SpellBeamDescriptor,
} from './spell-types';

// ── Rendering constants ─────────────────────────────────────────
const CORE = '#ffffff';
const GLOW = '#aaddff';
const TRAIL = '#4488ff';
const GLOW_SIZE = 3;
const PULSE_RATE = 12;
const BEAM_LEN = 300;

function renderProjectile(rc: SpellRenderContext): void {
	const {ctx, sr, vx, vy, lifeTimer, maxLifeTimer, originX, originY, ox, oy, scale, time} = rc;

	const progress = maxLifeTimer > 0 ? lifeTimer / maxLifeTimer : 1;
	const startSx = ox + originX * scale;
	const startSy = oy + originY * scale;
	const angle = Math.atan2(vy, vx);
	const beamLength = BEAM_LEN * scale;
	const endSx = startSx + Math.cos(angle) * beamLength;
	const endSy = startSy + Math.sin(angle) * beamLength;

	const flicker = 0.7 + 0.3 * Math.sin(time * PULSE_RATE);
	const beamWidth = sr * 2 * progress * flicker;

	// Outer glow
	ctx.save();
	ctx.globalAlpha = 0.15 * progress;
	ctx.strokeStyle = GLOW;
	ctx.lineWidth = beamWidth * GLOW_SIZE;
	ctx.lineCap = 'round';
	ctx.beginPath();
	ctx.moveTo(startSx, startSy);
	ctx.lineTo(endSx, endSy);
	ctx.stroke();

	// Mid beam
	ctx.globalAlpha = 0.5 * progress;
	ctx.strokeStyle = TRAIL;
	ctx.lineWidth = beamWidth * 1.5;
	ctx.beginPath();
	ctx.moveTo(startSx, startSy);
	ctx.lineTo(endSx, endSy);
	ctx.stroke();

	// Core
	ctx.globalAlpha = progress;
	ctx.strokeStyle = CORE;
	ctx.lineWidth = beamWidth;
	ctx.beginPath();
	ctx.moveTo(startSx, startSy);
	ctx.lineTo(endSx, endSy);
	ctx.stroke();

	// Hot center
	ctx.globalAlpha = progress * flicker;
	ctx.strokeStyle = '#fff';
	ctx.lineWidth = beamWidth * 0.3;
	ctx.beginPath();
	ctx.moveTo(startSx, startSy);
	ctx.lineTo(endSx, endSy);
	ctx.stroke();
	ctx.restore();
}

// ── Spawning: visual-only line entity, damage deferred to expiry ─
function spawn(sc: SpellSpawnContext): SpellProjectileDescriptor[] {
	const {px, py, playerRadius, nx, ny, radius, damage, friendlyFire, color, spellId} = sc;
	const midX = px + nx * (BEAM_LEN / 2);
	const midY = py + ny * (BEAM_LEN / 2);
	return [{
		x: midX,
		y: midY,
		vx: nx,
		vy: ny,
		radius,
		color,
		damage,
		lifeTimer: 0.35,
		maxLifeTimer: 0.35,
		blastRadius: 0,
		friendlyFire,
		spellId,
		originX: px + nx * (playerRadius + 2),
		originY: py + ny * (playerRadius + 2),
		visualOnly: true,
		explodeOnExpiry: true,
	}];
}

function beamDamage(sc: SpellSpawnContext): SpellBeamDescriptor {
	return {
		ox: sc.px,
		oy: sc.py,
		nx: sc.nx,
		ny: sc.ny,
		length: BEAM_LEN,
		width: sc.radius * 2,
		damage: sc.damage,
		friendlyFire: sc.friendlyFire,
	};
}

const effect: SpellEffect = {renderProjectile, spawn, beamDamage};

// ── Spell definition ────────────────────────────────────────────
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
	effect,
	pros: ['Pierces all enemies in line', 'Instant hit', 'Great vs formations'],
	cons: ['Requires aim', 'Friendly fire', 'Medium-high mana'],
	description: 'A searing beam of pure energy cuts through everything in its path. Devastating against enemies foolish enough to line up.',
};
