// 3×3 biome matrix: temperature (rows) × moisture (columns).
// 9 land biomes, determined by climate. Water is height-based, not a biome.
// Generates a GPU lookup texture for data-driven macroworld rendering.
// To add a biome: define it, place it in BIOME_MATRIX.

export type BiomeDef = {
	readonly id: number;
	readonly name: string;
	readonly color: readonly [number, number, number]; // RGB 0..1
	readonly terrainIndex: number; // Same as biome id (0-8)
};

// ── Biome enum (shared with subworld via CellContext) ──────────────
// Row-major index into the 3×3 matrix: row * 3 + col.
// Cold→Hot (rows 0..2), Dry→Wet (cols 0..2).

export enum Biome {
	Tundra = 0,
	Taiga = 1,
	Snow = 2,
	Valley = 3,
	Meadow = 4,
	Swamp = 5,
	Desert = 6,
	Steppe = 7,
	Tropics = 8,
	Water = 9,
}

export const BIOME_COUNT = 9; // Land biomes (excluding Water)

// ── Biome definitions ──────────────────────────────────────────────

const TUNDRA: BiomeDef = {
	id: Biome.Tundra, name: 'Tundra', color: [0.5, 0.52, 0.45], terrainIndex: 0,
};
const TAIGA: BiomeDef = {
	id: Biome.Taiga, name: 'Taiga', color: [0.22, 0.38, 0.28], terrainIndex: 1,
};
const SNOW: BiomeDef = {
	id: Biome.Snow, name: 'Snow', color: [0.9, 0.92, 0.96], terrainIndex: 2,
};
const VALLEY: BiomeDef = {
	id: Biome.Valley, name: 'Valley', color: [0.55, 0.52, 0.32], terrainIndex: 3,
};
const MEADOW: BiomeDef = {
	id: Biome.Meadow, name: 'Meadow', color: [0.4, 0.52, 0.28], terrainIndex: 4,
};
const SWAMP: BiomeDef = {
	id: Biome.Swamp, name: 'Swamp', color: [0.28, 0.38, 0.22], terrainIndex: 5,
};
const DESERT: BiomeDef = {
	id: Biome.Desert, name: 'Desert', color: [0.82, 0.72, 0.48], terrainIndex: 6,
};
const STEPPE: BiomeDef = {
	id: Biome.Steppe, name: 'Steppe', color: [0.68, 0.6, 0.32], terrainIndex: 7,
};
const TROPICS: BiomeDef = {
	id: Biome.Tropics, name: 'Tropics', color: [0.1, 0.35, 0.1], terrainIndex: 8,
};

// ── 3×3 biome matrix ───────────────────────────────────────────────
// Rows: temperature — cold (0) → hot (2)
// Columns: moisture — dry (0) → wet (2)

export const BIOME_MATRIX: ReadonlyArray<readonly BiomeDef[]> = [
	//       Dry      Medium   Wet
	/* Cold */ [TUNDRA, TAIGA, SNOW],
	/* Temp */ [VALLEY, MEADOW, SWAMP],
	/* Hot  */ [DESERT, STEPPE, TROPICS],
];

export const BIOME_ROWS = BIOME_MATRIX.length;
export const BIOME_COLS = BIOME_MATRIX[0].length;

/** Flat array of all land biome definitions, indexed by Biome enum. */
export const BIOME_DEFS: readonly BiomeDef[] = [
	TUNDRA, TAIGA, SNOW, VALLEY, MEADOW, SWAMP, DESERT, STEPPE, TROPICS,
];

// ── CPU biome lookup ────────────────────────────────────────────────
// Determines the biome for a cell given normalised temperature and moisture.
// Mirrors the GPU biome texture lookup.

export function biomeFromClimate(temperature01: number, moisture01: number): Biome {
	const row = Math.min(Math.round(temperature01 * (BIOME_ROWS - 1)), BIOME_ROWS - 1);
	const col = Math.min(Math.round(moisture01 * (BIOME_COLS - 1)), BIOME_COLS - 1);
	return BIOME_MATRIX[row][col].id as Biome;
}

// ── Texture generation ─────────────────────────────────────────────
// Produces an RGBA Uint8Array for GPU upload.
// R,G,B = biome color (bilinear-interpolated between matrix cells)
// A     = terrain index (biome id, 0-8, nearest-cell)

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
