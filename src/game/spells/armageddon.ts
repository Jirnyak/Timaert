// === Armageddon — ultimate devastation ===
import type {
	Spell, SpellEffect, SpellRenderContext,
	SpellSpawnContext, SpellProjectileDescriptor,
} from './spell-types';

// ── Rendering: perspective-shrinking meteor ─────────────────────
const CORE = '#ffaa00';
const GLOW = '#ff2200';
const TRAIL = '#440000';
const GLOW_SIZE = 4;
const PULSE_RATE = 2;
const PER_METEOR_BLAST = 25;

function renderProjectile(rc: SpellRenderContext): void {
	const {ctx, sx, sy, sr, lifeTimer, maxLifeTimer, time} = rc;

	// Perspective: meteor starts small (far away) and grows as it falls
	const progress = maxLifeTimer > 0 ? 1 - (lifeTimer / maxLifeTimer) : 1;
	const perspScale = 0.2 + progress * 0.8;
	const r = sr * perspScale;
	const pulse = 0.8 + 0.2 * Math.sin(time * PULSE_RATE + lifeTimer * 10);

	ctx.save();
	ctx.translate(sx, sy);

	// Smoke trail (behind, fading)
	ctx.globalAlpha = 0.1 * (1 - progress);
	ctx.fillStyle = TRAIL;
	ctx.beginPath();
	ctx.arc(0, -r * 2, r * GLOW_SIZE, 0, Math.PI * 2);
	ctx.fill();

	// Fire glow
	ctx.globalAlpha = 0.3 * pulse;
	ctx.fillStyle = GLOW;
	ctx.beginPath();
	ctx.arc(0, 0, r * GLOW_SIZE, 0, Math.PI * 2);
	ctx.fill();

	// Mid flame
	ctx.globalAlpha = 0.6 * pulse;
	ctx.fillStyle = GLOW;
	ctx.beginPath();
	ctx.arc(0, 0, r * 2, 0, Math.PI * 2);
	ctx.fill();

	// Core
	ctx.globalAlpha = 1;
	ctx.fillStyle = CORE;
	ctx.beginPath();
	ctx.arc(0, 0, r, 0, Math.PI * 2);
	ctx.fill();

	// Hot center
	ctx.globalAlpha = pulse;
	ctx.fillStyle = '#fff';
	ctx.beginPath();
	ctx.arc(0, 0, r * 0.4, 0, Math.PI * 2);
	ctx.fill();

	ctx.restore();
}

// ── Spawning: many fast meteors scaled to blast area ─────────────
function spawn(sc: SpellSpawnContext): SpellProjectileDescriptor[] {
	const {px, py, radius, damage, blastRadius, friendlyFire, color, spellId, rng} = sc;
	const spread = blastRadius > 0 ? blastRadius : 160;
	const count = Math.max(16, Math.ceil(spread * 0.2));
	const meteors: SpellProjectileDescriptor[] = [];
	for (let i = 0; i < count; i++) {
		const angle = rng() * Math.PI * 2;
		const dist = rng() * spread;
		const delay = rng() * 0.5;
		const life = 0.3 + delay;
		meteors.push({
			x: px + Math.cos(angle) * dist,
			y: py + Math.sin(angle) * dist,
			vx: 0,
			vy: 0,
			radius,
			color,
			damage,
			lifeTimer: life,
			maxLifeTimer: life,
			blastRadius: PER_METEOR_BLAST,
			friendlyFire,
			spellId,
			visualOnly: true,
			explodeOnExpiry: true,
		});
	}

	return meteors;
}

const effect: SpellEffect = {renderProjectile, spawn};

// ── Spell definition ────────────────────────────────────────────
export const armageddon: Spell = {
	id: 'armageddon',
	name: 'Armageddon',
	icon: '☠',
	tags: ['fire', 'dark'],
	tier: 5,
	rarity: 'mythic',
	manaCost: 1000,
	cooldown: 120,
	castTime: 2,
	sustained: false,
	manaDrain: 0,
	scaling: {power: 2, duration: 0.5, radius: 1},
	micro: {
		shape: 'nova',
		baseDamage: 80,
		baseHeal: 0,
		baseRadius: 160,
		chainCount: 0,
		chainDecay: 0,
		speed: 0,
		duration: 0,
		friendlyFire: true,
		statusEffect: 'burning',
		statusDuration: 8,
	},
	macro: {
		type: 'damage_region',
		power: 50,
		duration: 0,
	},
	effect,
	pros: ['Massive AoE', 'Battle-ending power', 'Burns everything'],
	cons: ['Friendly fire', '2s cast time', 'Enormous mana cost', 'Faction reputation hit', '2 min cooldown'],
	description: 'Rain fire and ruin upon the world. Everything burns — enemies, allies, buildings, reputation. The ultimate expression of magical supremacy and moral bankruptcy.',
};
