// === Spell Visual Renderer — thin generic dispatcher ===
// No spell-specific code. Each spell packages its own render functions.
// This module just looks up the spell and calls its effect methods.

import type {SpellAuraContext, SpellRenderContext} from './spell-types';
import {SPELL_CATALOG} from './index';

// ── Elapsed time for animation (module-level, shared) ──────────

let globalTime = 0;
export function tickVisualTime(dt: number): void {
	globalTime += dt;
}

// ── Generic fallback ────────────────────────────────────────────

function drawGenericProjectile(
	ctx: CanvasRenderingContext2D,
	sx: number, sy: number, sr: number,
	color: string,
): void {
	ctx.save();
	ctx.globalAlpha = 0.3;
	ctx.fillStyle = color;
	ctx.beginPath();
	ctx.arc(sx, sy, sr * 2.5, 0, Math.PI * 2);
	ctx.fill();
	ctx.restore();
	ctx.fillStyle = color;
	ctx.beginPath();
	ctx.arc(sx, sy, sr, 0, Math.PI * 2);
	ctx.fill();
	ctx.fillStyle = '#fff';
	ctx.beginPath();
	ctx.arc(sx, sy, sr * 0.4, 0, Math.PI * 2);
	ctx.fill();
}

// ── Public API ──────────────────────────────────────────────────

/**
 * Draw a spell projectile entity.
 * Delegates to the spell's own renderProjectile; falls back to generic glow.
 */
export function drawSpellProjectile(
	ctx: CanvasRenderingContext2D,
	sx: number, sy: number, sr: number,
	vx: number, vy: number,
	color: string,
	spellId: string | undefined,
	lifeTimer: number | undefined,
	maxLifeTimer: number | undefined,
	originX: number | undefined,
	originY: number | undefined,
	ox: number, oy: number, scale: number,
): void {
	const spell = spellId ? SPELL_CATALOG.get(spellId) : undefined;
	const render = spell?.effect?.renderProjectile;
	if (!render) {
		drawGenericProjectile(ctx, sx, sy, sr, color);
		return;
	}

	const rc: SpellRenderContext = {
		ctx, sx, sy, sr, vx, vy,
		lifeTimer: lifeTimer ?? 0,
		maxLifeTimer: maxLifeTimer ?? 0,
		originX: originX ?? 0,
		originY: originY ?? 0,
		ox, oy, scale,
		time: globalTime,
	};
	render(rc);
}

/**
 * Draw sustained aura around the caster for a given spell.
 * Delegates to the spell's own renderAura; no-op if the spell has none.
 */
export function drawCasterAura(
	ctx: CanvasRenderingContext2D,
	sx: number, sy: number,
	radius: number,
	spellId: string,
): void {
	const spell = SPELL_CATALOG.get(spellId);
	const render = spell?.effect?.renderAura;
	if (!render) {
		return;
	}

	const ac: SpellAuraContext = {
		ctx, sx, sy, radius, time: globalTime,
	};
	render(ac);
}
