// === Subworld Lighting — sun direction, ambient, point lights ===
//
// Pure graphics helper: computes light parameters from time-of-day.
// No game state dependencies. Used by renderer-3d.ts exclusively.
//
// Sun direction matches sky.ts formula: sunAng = (tod − 0.25) × 2π.
// Four-band quantised diffuse for pixel-retro aesthetic.
// Point lights (torches etc.) supported modularly via uniform arrays.

import type {Vec3} from './math3d';

// ── Constants ───────────────────────────────────────────────────

export const MAX_POINT_LIGHTS = 8;

// ── Types ───────────────────────────────────────────────────────

export type PointLight = {
	readonly x: number;
	readonly y: number;
	readonly z: number;
	readonly r: number;
	readonly g: number;
	readonly b: number;
	readonly radius: number;
};

export type LightParameters = {
	readonly sunDir: Vec3;
	readonly sunColor: Vec3;
	readonly sunIntensity: number;
	readonly ambientColor: Vec3;
};

// ── Implementation ──────────────────────────────────────────────

function smoothstep(edge0: number, edge1: number, x: number): number {
	const t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
	return t * t * (3 - 2 * t);
}

/**
 * Compute lighting parameters from time-of-day.
 * @param tod - 0..1 (0 = midnight, 0.5 = noon)
 */
export function computeLightParameters(tod: number): LightParameters {
	const sunAng = (tod - 0.25) * Math.PI * 2;
	const sunDir: Vec3 = [Math.cos(sunAng), Math.sin(sunAng), 0];

	const elevation = sunDir[1];

	// Intensity ramps up during dawn, full at midday, zero at night
	const sunIntensity = smoothstep(-0.05, 0.3, elevation);

	// Sun color: warm orange near horizon → neutral white overhead
	const warmth = 1 - smoothstep(0, 0.4, elevation);
	const sunColor: Vec3 = [
		1 - warmth * 0.1,
		1 - warmth * 0.3,
		1 - warmth * 0.55,
	];

	// Ambient: cool blue moonlight at night → neutral during day
	const dayRaw = smoothstep(0.22, 0.35, tod) - smoothstep(0.65, 0.78, tod);
	const dayF = Math.max(0, Math.min(1, dayRaw));
	const ambientColor: Vec3 = [
		0.12 + dayF * 0.28,
		0.12 + dayF * 0.28,
		0.18 + dayF * 0.22,
	];

	return {
		sunDir, sunColor, sunIntensity, ambientColor,
	};
}
