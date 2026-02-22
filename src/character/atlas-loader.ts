/* eslint-disable @typescript-eslint/no-restricted-types */
import type {AtlasData, AtlasEntry} from './types';

const ATLAS_IMAGE_URL = '/assets/character/atlas.png';
const ATLAS_BIN_URL = '/assets/character/atlas.bin';

export const TILES_PER_SHEET = 160;
export const TILE_COLS = 8;
export const LOGICAL_TILE_SIZE = 48;

let atlas: AtlasData | undefined;
let loading: Promise<AtlasData> | undefined;

export function getAtlas(): AtlasData | undefined {
	return atlas;
}

export async function loadAtlas(): Promise<AtlasData> {
	if (atlas) {
		return atlas;
	}

	if (loading) {
		return loading;
	}

	loading = (async () => {
		const [image, response] = await Promise.all([
			loadAtlasImage(ATLAS_IMAGE_URL),
			fetch(ATLAS_BIN_URL),
		]);
		const buffer = await response.arrayBuffer();
		const parsed = parseAtlasBin(buffer);
		atlas = {image, ...parsed};
		return atlas;
	})();
	return loading;
}

// ---------------------------------------------------------------------------
// Image loader
// ---------------------------------------------------------------------------

async function loadAtlasImage(src: string): Promise<HTMLImageElement> {
	const img = new Image();
	await new Promise<void>((resolve, reject) => {
		img.addEventListener('load', () => {
			resolve();
		});
		img.addEventListener('error', () => {
			reject(new Error(`Failed to load atlas image: ${src}`));
		});
		img.src = src; // Set src AFTER listeners to avoid missing cached load
	});
	// Ensure the image is fully decoded before returning for texImage2D
	await img.decode();
	return img;
}

// ---------------------------------------------------------------------------
// Binary parser
// ---------------------------------------------------------------------------

function parseAtlasBin(buffer: ArrayBuffer): Omit<AtlasData, 'image'> {
	const view = new DataView(buffer);
	let offset = 0;

	// --- Header (16 bytes) ---
	const magic = String.fromCodePoint(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));
	if (magic !== 'ATLS') {
		throw new Error('Invalid atlas magic');
	}

	offset = 4;
	const version = view.getUint16(offset, true);
	offset += 2;
	if (version !== 1) {
		throw new Error(`Unsupported atlas version: ${version}`);
	}

	const sheetCount = view.getUint16(offset, true);
	offset += 2;
	const entryCount = view.getUint32(offset, true);
	offset += 4;
	const atlasWidth = view.getUint16(offset, true);
	offset += 2;
	const atlasHeight = view.getUint16(offset, true);
	offset += 2;
	// Offset is now 16

	// --- String table ---
	const tableSize = view.getUint32(offset, true);
	offset += 4;
	const tableBytes = new Uint8Array(buffer, offset, tableSize);
	offset += tableSize;
	// Pad to 4-byte boundary
	offset += (4 - (tableSize % 4)) % 4;

	// --- Sheet name offsets ---
	const decoder = new TextDecoder();
	const sheetNames: string[] = [];
	const nameToOrdinal = new Map<string, number>();
	for (let i = 0; i < sheetCount; i++) {
		const nameOffset = view.getUint16(offset, true);
		offset += 2;
		// Read null-terminated string from table
		let end = nameOffset;
		while (end < tableBytes.length && tableBytes[end] !== 0) {
			end++;
		}

		const name = decoder.decode(tableBytes.subarray(nameOffset, end));
		sheetNames.push(name);
		nameToOrdinal.set(name, i);
	}

	// Pad to 16-byte boundary (from file start)
	offset += (16 - (offset % 16)) % 16;

	// --- AoS entries ---
	const entries = new Uint16Array(buffer, offset, entryCount * 8);

	return {
		atlasWidth, atlasHeight, sheetCount, entryCount,
		entries, sheetNames, nameToOrdinal,
	};
}

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

/**
 * Compute the flat AoS entry index for a sheet ordinal + tile position.
 * Tile position is row-major: tileIndex = y * 8 + x.
 */
export function getEntryIndex(sheetOrdinal: number, tileIndex: number): number {
	return sheetOrdinal * TILES_PER_SHEET + tileIndex;
}

/**
 * Read a single AoS entry by flat index. Returns null for transparent tiles.
 */
export function getEntryByIndex(data: AtlasData, index: number): AtlasEntry | null {
	if (index < 0 || index >= data.entryCount) {
		return null;
	}

	const base = index * 8;
	const w = data.entries[base + 2];
	const h = data.entries[base + 3];
	if (w === 0 || h === 0) {
		return null;
	}

	return {
		u0: data.entries[base],
		v0: data.entries[base + 1],
		w,
		h,
		ox: data.entries[base + 4],
		oy: data.entries[base + 5],
	};
}

/**
 * Map a web sprite path (e.g. `/assets/character/Body/body_001.png`)
 * to the sheet ordinal in the atlas. Returns -1 if not found.
 */
export function getSheetOrdinal(data: AtlasData, spritePath: string): number {
	const relative = spritePath.replace(/^\/assets\/character\//, '');
	return data.nameToOrdinal.get(relative) ?? -1;
}
