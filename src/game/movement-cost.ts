// === Movement Cost — SP drain weights per biome & feature ===
//
// Layer 1 (Macroworld). Data-driven SP costs for movement.
// Each macroworld cell costs BASE_SP × weight, where weight is
// determined by the cell's feature (if present) or biome (fallback).
//
// Balance anchor: 100 SP default → 10 road cells → 1 water cell.
// To add a biome/feature: add a weight entry. No other changes needed.

import {Biome} from './biomes';
import {FeatureType} from './features';

// ── Constants ───────────────────────────────────────────────────

/** Base SP cost per macroworld cell (before weight multiplier). */
export const MACRO_BASE_SP = 10;

/** Recovery rate: +10% of maxSP per game hour (base, before modifiers). */
export const REST_RECOVERY_PCT = 0.1;

/** Subworld: SP cost per 1000 distance units (flat, all terrain). */
export const SUBWORLD_SP_PER_1000 = 10;

/** Subworld: SP cost per 1000 distance units on water tiles. */
export const SUBWORLD_WATER_SP_PER_1000 = 100;

// ── Biome weights (terrain cost when no feature overrides) ──────

const BIOME_SP_WEIGHT: Record<number, number> = {
	[Biome.Tundra]: 2.5,
	[Biome.Taiga]: 2.5,
	[Biome.Snow]: 3,
	[Biome.Valley]: 2,
	[Biome.Meadow]: 2,
	[Biome.Swamp]: 3.5,
	[Biome.Desert]: 3,
	[Biome.Steppe]: 2,
	[Biome.Tropics]: 2.5,
	[Biome.Water]: 10,
};

// ── Feature weights (override biome cost when > 0) ──────────────

const FEATURE_SP_WEIGHT: Record<number, number> = {
	[FeatureType.None]: 0, // 0 = fall through to biome weight
	[FeatureType.Road]: 1,
	[FeatureType.Tree]: 3,
	[FeatureType.Mountain]: 5,
	[FeatureType.DirtRoad]: 1.5,
};

// ── Public API ──────────────────────────────────────────────────

/** Cost weight for a macroworld cell (multiplied by MACRO_BASE_SP). */
export function getCellSpWeight(biome: number, feature: number): number {
	const fw = FEATURE_SP_WEIGHT[feature] ?? 0;
	if (fw > 0) {
		return fw;
	}

	return BIOME_SP_WEIGHT[biome] ?? 2;
}

/** Absolute SP cost to enter a macroworld cell. */
export function getCellSpCost(biome: number, feature: number): number {
	return MACRO_BASE_SP * getCellSpWeight(biome, feature);
}

/**
 * Build a pre-computed cost-weight grid for A* pathfinding.
 * Each cell stores the SP weight (1.0–10.0).
 * Callers combine with MACRO_BASE_SP to get absolute SP cost.
 */
export function buildCostGrid(
	width: number,
	height: number,
	heightData: Uint8Array,
	moistureData: Uint8Array,
	temperatureData: Uint8Array,
	featureData: Uint8Array | undefined,
	seaLevel: number,
): Float32Array {
	const n = width * height;
	const grid = new Float32Array(n);
	for (let i = 0; i < n; i++) {
		const h = heightData[i] / 255;
		const biome = h < seaLevel
			? Biome.Water
			: biomeFromClimateRaw(temperatureData[i] / 255, moistureData[i] / 255);
		const feature = featureData ? featureData[i] : FeatureType.None;
		grid[i] = getCellSpWeight(biome, feature);
	}

	return grid;
}

// ── Internal ────────────────────────────────────────────────────

// Inline biome resolution to avoid circular import with biomes.ts.
// Mirrors biomeFromClimate() exactly.
function biomeFromClimateRaw(temperature01: number, moisture01: number): number {
	const row = Math.min(Math.round(temperature01 * 2), 2);
	const col = Math.min(Math.round(moisture01 * 2), 2);
	return row * 3 + col; // Biome enum is row-major: row*3+col
}
