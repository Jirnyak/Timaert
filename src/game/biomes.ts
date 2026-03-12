// Whittaker-style biome matrix: temperature (rows) × moisture (columns)
// Generates a GPU lookup texture for data-driven biome determination.
// To add a biome: define it, place it in BIOME_MATRIX, assign a terrainIndex.

export type BiomeDef = {
	readonly id: number;
	readonly name: string;
	readonly color: readonly [number, number, number]; // RGB 0..1
	readonly terrainIndex: number; // 0-8 maps to TERRAIN_PATHS in renderer.ts
};

// ── Biome definitions ──────────────────────────────────────────────
// terrainIndex key: 0=water 1=sand 2=grass 3=dirt 4=mount 5=snow 6=jungle 7=swamp 8=tundra

const ICE_SHEET: BiomeDef = {
	id: 0, name: 'Ice Sheet', color: [0.92, 0.94, 0.97], terrainIndex: 5,
};
const COLD_DESERT: BiomeDef = {
	id: 1, name: 'Cold Desert', color: [0.6, 0.58, 0.5], terrainIndex: 3,
};
const TUNDRA: BiomeDef = {
	id: 2, name: 'Tundra', color: [0.5, 0.52, 0.45], terrainIndex: 8,
};
const SNOW_FOREST: BiomeDef = {
	id: 3, name: 'Snow Forest', color: [0.28, 0.38, 0.32], terrainIndex: 5,
};
const STEPPE: BiomeDef = {
	id: 4, name: 'Steppe', color: [0.58, 0.55, 0.35], terrainIndex: 3,
};
const DRY_GRASS: BiomeDef = {
	id: 5, name: 'Dry Grassland', color: [0.55, 0.5, 0.28], terrainIndex: 3,
};
const GRASSLAND: BiomeDef = {
	id: 6, name: 'Grassland', color: [0.4, 0.52, 0.28], terrainIndex: 2,
};
const BOREAL_FOREST: BiomeDef = {
	id: 7, name: 'Boreal Forest', color: [0.18, 0.35, 0.22], terrainIndex: 8,
};
const TAIGA: BiomeDef = {
	id: 8, name: 'Taiga', color: [0.22, 0.35, 0.28], terrainIndex: 8,
};
const DESERT: BiomeDef = {
	id: 9, name: 'Desert', color: [0.82, 0.72, 0.48], terrainIndex: 1,
};
const RED_DESERT: BiomeDef = {
	id: 10, name: 'Red Desert', color: [0.72, 0.45, 0.3], terrainIndex: 1,
};
const SHRUBLAND: BiomeDef = {
	id: 11, name: 'Shrubland', color: [0.52, 0.5, 0.3], terrainIndex: 3,
};
const WOODLAND: BiomeDef = {
	id: 12, name: 'Woodland', color: [0.45, 0.5, 0.25], terrainIndex: 2,
};
const TEMP_FOREST: BiomeDef = {
	id: 13, name: 'Temperate Forest', color: [0.22, 0.42, 0.18], terrainIndex: 2,
};
const TEMP_RAINFOREST: BiomeDef = {
	id: 14, name: 'Temperate Rainforest', color: [0.12, 0.38, 0.15], terrainIndex: 7,
};
const SAVANNA: BiomeDef = {
	id: 15, name: 'Savanna', color: [0.68, 0.6, 0.32], terrainIndex: 3,
};
const TROP_DRY_FOREST: BiomeDef = {
	id: 16, name: 'Tropical Dry Forest', color: [0.42, 0.48, 0.22], terrainIndex: 2,
};
const TROP_FOREST: BiomeDef = {
	id: 17, name: 'Tropical Forest', color: [0.12, 0.35, 0.12], terrainIndex: 6,
};
const TROP_RAINFOREST: BiomeDef = {
	id: 18, name: 'Tropical Rainforest', color: [0.05, 0.3, 0.05], terrainIndex: 6,
};

// ── Whittaker biome matrix ─────────────────────────────────────────
// Rows: temperature, cold (0) → hot (5)
// Columns: moisture, dry (0) → wet (5)
// Add new rows/columns to extend climate zones.

export const BIOME_MATRIX: ReadonlyArray<readonly BiomeDef[]> = [
	//            Very Dry       Dry            Semi-Dry       Semi-Wet        Wet             Very Wet
	/* Arctic  */ [ICE_SHEET, ICE_SHEET, TUNDRA, TUNDRA, SNOW_FOREST, ICE_SHEET],
	/* Subarc. */ [COLD_DESERT, TUNDRA, TUNDRA, GRASSLAND, TAIGA, SNOW_FOREST],
	/* Boreal  */ [STEPPE, DRY_GRASS, GRASSLAND, BOREAL_FOREST, TAIGA, TAIGA],
	/* Temper. */ [DESERT, SHRUBLAND, GRASSLAND, TEMP_FOREST, TEMP_RAINFOREST, TEMP_RAINFOREST],
	/* Subtrop */ [DESERT, SAVANNA, WOODLAND, TEMP_FOREST, TROP_FOREST, TROP_RAINFOREST],
	/* Tropic  */ [RED_DESERT, SAVANNA, TROP_DRY_FOREST, TROP_FOREST, TROP_RAINFOREST, TROP_RAINFOREST],
];

export const BIOME_ROWS = BIOME_MATRIX.length;
export const BIOME_COLS = BIOME_MATRIX[0].length;

// ── Texture generation ─────────────────────────────────────────────
// Produces an RGBA Uint8Array for GPU upload.
// R,G,B = biome color (bilinear-interpolated between matrix cells)
// A     = terrain overlay index (0-8, nearest-cell)

export function generateBiomeTexture(size: number): Uint8Array {
	const data = new Uint8Array(size * size * 4);
	const maxCol = BIOME_COLS - 1;
	const maxRow = BIOME_ROWS - 1;

	for (let y = 0; y < size; y++) {
		for (let x = 0; x < size; x++) {
			const mx = (x / (size - 1)) * maxCol;
			const my = (y / (size - 1)) * maxRow;

			const x0 = Math.min(Math.floor(mx), maxCol - 1);
			const y0 = Math.min(Math.floor(my), maxRow - 1);
			const x1 = x0 + 1;
			const y1 = y0 + 1;
			const fx = mx - x0;
			const fy = my - y0;

			// Bilinear color interpolation
			const c00 = BIOME_MATRIX[y0][x0].color;
			const c10 = BIOME_MATRIX[y0][x1].color;
			const c01 = BIOME_MATRIX[y1][x0].color;
			const c11 = BIOME_MATRIX[y1][x1].color;

			const r = lerp(lerp(c00[0], c10[0], fx), lerp(c01[0], c11[0], fx), fy);
			const g = lerp(lerp(c00[1], c10[1], fx), lerp(c01[1], c11[1], fx), fy);
			const b = lerp(lerp(c00[2], c10[2], fx), lerp(c01[2], c11[2], fx), fy);

			// Nearest cell for terrain index
			const nearRow = Math.min(Math.round(my), maxRow);
			const nearCol = Math.min(Math.round(mx), maxCol);
			const nearest = BIOME_MATRIX[nearRow][nearCol];

			const idx = (y * size + x) * 4;
			data[idx] = Math.round(r * 255);
			data[idx + 1] = Math.round(g * 255);
			data[idx + 2] = Math.round(b * 255);
			data[idx + 3] = nearest.terrainIndex;
		}
	}

	return data;
}

function lerp(a: number, b: number, t: number): number {
	return a + (b - a) * t;
}
