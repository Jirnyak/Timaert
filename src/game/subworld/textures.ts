// === Procedural texture atlas for subworld 3D rendering ===
//
// Generates small pixel-art textures used by the raycaster for
// structure walls, roofs, and terrain. Each texture is a 64×64
// RGBA ImageData stored in a registry keyed by texture id string.
//
// Textures are generated once and cached. The raycaster samples
// them per-pixel during column rendering.

const TEX_SIZE = 64;
const registry = new Map<string, ImageData>();

// ── Seeded noise for deterministic textures ─────────────────────

function texNoise(x: number, y: number, seed: number): number {
	let v = (x * 374_761_393) ^ (y * 668_265_263) ^ (seed * 1_274_126_177);
	v = (v ^ (v >>> 13)) * 2_246_822_519;
	v ^= v >>> 16;
	return (v >>> 0) / 4_294_967_295;
}

// ── Helpers ─────────────────────────────────────────────────────

function clamp255(v: number): number {
	return Math.max(0, Math.min(255, Math.round(v)));
}

function setPixel(data: Uint8ClampedArray, x: number, y: number, r: number, g: number, b: number, a = 255): void {
	const i = (y * TEX_SIZE + x) * 4;
	data[i] = clamp255(r);
	data[i + 1] = clamp255(g);
	data[i + 2] = clamp255(b);
	data[i + 3] = a;
}

function createTexData(): ImageData {
	return new ImageData(TEX_SIZE, TEX_SIZE);
}

// ── Texture generators ──────────────────────────────────────────

function generateStoneWall(): ImageData {
	const img = createTexData();
	const d = img.data;
	// Solid gray stone with subtle noise — medieval cobblestone look
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 42) * 30 - 15;
			const n2 = texNoise(x * 3, y * 3, 43) * 10 - 5;
			setPixel(d, x, y, 130 + n + n2, 128 + n + n2, 122 + n + n2);
		}
	}

	return img;
}

function generateRuinWall(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 77) * 40 - 20;
			const crack = texNoise(x * 3, y * 3, 88) > 0.85;
			if (crack) {
				setPixel(d, x, y, 50 + n, 45 + n, 40 + n);
			} else {
				setPixel(d, x, y, 110 + n, 100 + n, 85 + n);
			}
		}
	}

	return img;
}

function generateRoofTile(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const tileH = 6;
			const tileW = 10;
			const row = Math.floor(y / tileH);
			const off = (row % 2) * (tileW / 2);
			const tx = (x + off) % tileW;
			const isEdge = tx === 0 || (y % tileH) === 0;
			const n = texNoise(x, y, 33) * 20 - 10;
			if (isEdge) {
				setPixel(d, x, y, 100 + n, 50 + n, 30 + n);
			} else {
				setPixel(d, x, y, 155 + n, 75 + n, 45 + n);
			}
		}
	}

	return img;
}

function generateRuinRoof(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 55) * 30 - 15;
			const hole = texNoise(x * 2, y * 2, 66) > 0.9;
			if (hole) {
				setPixel(d, x, y, 30 + n, 50 + n, 25 + n, 180);
			} else {
				setPixel(d, x, y, 95 + n, 80 + n, 60 + n);
			}
		}
	}

	return img;
}

function generateWallTop(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 22) * 20 - 10;
			setPixel(d, x, y, 120 + n, 115 + n, 105 + n);
		}
	}

	return img;
}

function generateTowerTop(): ImageData {
	const img = createTexData();
	const d = img.data;
	const cx = TEX_SIZE / 2;
	const cy = TEX_SIZE / 2;
	const r = TEX_SIZE / 2 - 2;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const dx = x - cx;
			const dy = y - cy;
			const dist = Math.sqrt(dx * dx + dy * dy);
			if (dist > r) {
				setPixel(d, x, y, 0, 0, 0, 0);
			} else {
				const n = texNoise(x, y, 11) * 20 - 10;
				// Conical shading
				const shade = 1 - (dist / r) * 0.3;
				setPixel(d, x, y, (100 + n) * shade, (95 + n) * shade, (85 + n) * shade);
			}
		}
	}

	return img;
}

function generateGrass(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 99) * 30 - 15;
			const blade = texNoise(x * 5, y * 5, 100) > 0.7;
			if (blade) {
				setPixel(d, x, y, 60 + n, 110 + n, 40 + n);
			} else {
				setPixel(d, x, y, 75 + n, 125 + n, 50 + n);
			}
		}
	}

	return img;
}

function generateDirt(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 44) * 25 - 12;
			setPixel(d, x, y, 120 + n, 95 + n, 65 + n);
		}
	}

	return img;
}

function generateSky(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		const t = y / TEX_SIZE;
		const r = 100 + t * 55;
		const g = 140 + t * 50;
		const b = 200 + t * 30;
		for (let x = 0; x < TEX_SIZE; x++) {
			setPixel(d, x, y, r, g, b);
		}
	}

	return img;
}

function generateRoad(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 61) * 20 - 10;
			const cobble = ((x + (Math.floor(y / 8) % 2) * 4) % 8 < 7) && (y % 8 < 7);
			if (cobble) {
				setPixel(d, x, y, 122 + n, 112 + n, 86 + n);
			} else {
				setPixel(d, x, y, 80 + n, 75 + n, 60 + n);
			}
		}
	}

	return img;
}

function generateField(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 73) * 20 - 10;
			const furrow = x % 4 === 0;
			if (furrow) {
				setPixel(d, x, y, 100 + n, 80 + n, 50 + n);
			} else {
				setPixel(d, x, y, 183 + n, 143 + n, 85 + n);
			}
		}
	}

	return img;
}

function generateSquare(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 52) * 15 - 7;
			const gridLine = (x % 12 === 0) || (y % 12 === 0);
			if (gridLine) {
				setPixel(d, x, y, 160 + n, 155 + n, 145 + n);
			} else {
				setPixel(d, x, y, 190 + n, 185 + n, 175 + n);
			}
		}
	}

	return img;
}

function generateWild(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 81) * 25 - 12;
			const patch = texNoise(x * 2, y * 2, 82) > 0.48;
			if (patch) {
				// Brown earth patches (#756248)
				setPixel(d, x, y, 117 + n, 98 + n, 72 + n);
			} else {
				// Olive-green scrub (#5f6e4b)
				setPixel(d, x, y, 95 + n, 110 + n, 75 + n);
			}
		}
	}

	return img;
}

function generateHouseWall(): ImageData {
	const img = createTexData();
	const d = img.data;
	// Brick wall with wooden timber frame at corners/edges (fachwerk)
	const timberW = 4; // Timber column width at edges
	const timberColor = [110, 78, 48] as const;
	const timberDark = [88, 62, 38] as const;

	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 55) * 18 - 9;
			// Timber columns at left/right edges
			const isTimber = x < timberW || x >= TEX_SIZE - timberW;
			if (isTimber) {
				const grain = texNoise(x * 2, y, 66) * 12 - 6;
				const edge = x === 0 || x === timberW - 1
					|| x === TEX_SIZE - timberW || x === TEX_SIZE - 1;
				const [tr, tg, tb] = edge ? timberDark : timberColor;
				setPixel(d, x, y, tr + grain, tg + grain, tb + grain);
			} else {
				// Brick pattern
				const brickH = 5;
				const brickW = 10;
				const row = Math.floor(y / brickH);
				const offset = (row % 2) * (brickW / 2);
				const bx = (x + offset) % brickW;
				const isMortar = bx === 0 || (y % brickH) === 0;
				if (isMortar) {
					setPixel(d, x, y, 82 + n, 78 + n, 72 + n);
				} else {
					setPixel(d, x, y, 165 + n, 85 + n, 55 + n);
				}
			}
		}
	}

	// Two windows between timber columns
	const wins = [[14, 18, 10, 14], [40, 18, 10, 14]];
	for (const [wx, wy, ww, wh] of wins) {
		for (let dy = 0; dy < wh; dy++) {
			for (let dx = 0; dx < ww; dx++) {
				const px = wx + dx;
				const py = wy + dy;
				const isFrame = dx === 0 || dx === ww - 1 || dy === 0 || dy === wh - 1;
				if (isFrame) {
					setPixel(d, px, py, 70, 55, 40);
				} else {
					const glow = texNoise(px, py, 99) * 20;
					setPixel(d, px, py, 25 + glow, 20 + glow * 0.8, 12);
				}
			}
		}

		for (let dx = -1; dx <= ww; dx++) {
			const px = wx + dx;
			if (px >= 0 && px < TEX_SIZE) {
				setPixel(d, px, wy + wh, 85, 78, 70);
			}
		}
	}

	return img;
}

function generateWoodWall(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 91) * 18 - 9;
			const logW = 8;
			const logX = x % logW;
			const grain = texNoise(x, y * 3, 92) * 10 - 5;
			if (logX === 0 || logX === logW - 1) {
				setPixel(d, x, y, 55 + n, 38 + n, 22 + n);
			} else {
				setPixel(d, x, y, 135 + n + grain, 92 + n + grain, 52 + n + grain);
			}
		}
	}

	return img;
}

function generatePalisadeTop(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 93) * 14 - 7;
			const logW = 8;
			if (x % logW === 0) {
				setPixel(d, x, y, 45 + n, 32 + n, 18 + n);
			} else {
				setPixel(d, x, y, 120 + n, 85 + n, 48 + n);
			}
		}
	}

	return img;
}

function generateWoodHouse(): ImageData {
	const img = createTexData();
	const d = img.data;
	const plankH = 8;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 94) * 14 - 7;
			const grain = texNoise(x * 4, y, 95) * 8 - 4;
			if (y % plankH === 0) {
				setPixel(d, x, y, 60 + n, 42 + n, 28 + n);
			} else {
				setPixel(d, x, y, 148 + n + grain, 105 + n + grain, 62 + n + grain);
			}
		}
	}

	// Window
	for (let dy = 0; dy < 12; dy++) {
		for (let dx = 0; dx < 10; dx++) {
			const px = 22 + dx;
			const py = 20 + dy;
			const isFrame = dx === 0 || dx === 9 || dy === 0 || dy === 11;
			if (isFrame) {
				setPixel(d, px, py, 65, 48, 32);
			} else {
				const glow = texNoise(px, py, 96) * 20;
				setPixel(d, px, py, 22 + glow, 18 + glow * 0.8, 10);
			}
		}
	}

	return img;
}

function generateThatchRoof(): ImageData {
	const img = createTexData();
	const d = img.data;
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const n = texNoise(x, y, 97) * 20 - 10;
			const straw = texNoise(x * 3, y * 2, 98) * 15 - 7;
			if (y % 6 === 0) {
				setPixel(d, x, y, 140 + n, 118 + n, 70 + n);
			} else {
				setPixel(d, x, y, 175 + n + straw, 148 + n + straw, 85 + n + straw);
			}
		}
	}

	return img;
}

// ── Built-in generators ─────────────────────────────────────────

const generators: Record<string, () => ImageData> = {
	wall_stone: generateStoneWall,
	wall_ruin: generateRuinWall,
	roof_tile: generateRoofTile,
	ruin_roof: generateRuinRoof,
	wall_top: generateWallTop,
	tower_top: generateTowerTop,
	grass: generateGrass,
	dirt: generateDirt,
	sky: generateSky,
	tree_top: generateGrass,
	tree_trunk: generateDirt,
	road: generateRoad,
	field: generateField,
	square: generateSquare,
	wild: generateWild,
	house_wall: generateHouseWall,
	wall_wood: generateWoodWall,
	palisade_top: generatePalisadeTop,
	house_wood: generateWoodHouse,
	roof_thatch: generateThatchRoof,
};

// ── Tree sprite atlas (6 types × 64×64) ────────────────────────

type TreePalette = {
	bark: Array<[number, number, number]>;
	leaf: Array<[number, number, number]>;
};

const TREE_PALETTES: TreePalette[] = [
	{bark: [[79, 56, 41], [101, 67, 33]], leaf: [[30, 120, 30], [50, 160, 50], [75, 105, 42]]}, // Oak
	{bark: [[60, 40, 30], [85, 55, 40]], leaf: [[255, 160, 180], [255, 120, 165], [225, 105, 145]]}, // Cherry
	{bark: [[195, 195, 190], [240, 240, 235]], leaf: [[105, 195, 85], [135, 215, 105], [85, 165, 65]]}, // Birch
	{bark: [[70, 50, 40], [95, 68, 48]], leaf: [[235, 125, 10], [225, 65, 10], [245, 200, 15]]}, // Autumn
	{bark: [[88, 58, 38], [105, 72, 52]], leaf: [[12, 82, 12], [32, 115, 32], [18, 68, 18]]}, // Pine
	{bark: [[88, 62, 48], [105, 72, 38]], leaf: [[125, 190, 45], [105, 170, 35], [145, 205, 55]]}, // Willow
];

function treeHash(n: number): number {
	const v = Math.sin(n) * 43_758.545_312_3;
	return v - Math.floor(v);
}

function setTreePixel(
	data: Uint8ClampedArray, atlasW: number,
	ox: number, px: number, py: number,
	r: number, g: number, b: number, a = 255,
): void {
	const i = (py * atlasW + ox + px) * 4;
	data[i] = clamp255(r);
	data[i + 1] = clamp255(g);
	data[i + 2] = clamp255(b);
	data[i + 3] = clamp255(a);
}

function renderTreeType(
	data: Uint8ClampedArray, atlasW: number,
	ox: number, tp: number, seed: number,
): void {
	const pal = TREE_PALETTES[tp];
	const v1 = treeHash(seed + 1);
	const v2 = treeHash(seed + 2);
	const cx = 7 + Math.floor((v1 - 0.5) * 2);
	const gridSize = 16;
	const scale = TEX_SIZE / gridSize; // 4 pixels per grid cell

	for (let gy = 0; gy < gridSize; gy++) {
		for (let gx = 0; gx < gridSize; gx++) {
			const ph = treeHash(seed + gx * 17.1 + gy * 31.7);
			const bk = pal.bark[ph < 0.5 ? 0 : 1];
			const lIdx = ph < 0.33 ? 0 : (ph < 0.66 ? 1 : 2);
			const lf = pal.leaf[lIdx];

			let r = 0;
			let g = 0;
			let b = 0;
			let a = 0;

			const result = getTreePixel(tp, gx, gy, cx, v1, v2, seed, ph, bk, lf);
			if (result) {
				[r, g, b, a] = result;
			}

			if (a > 0) {
				for (let sy = 0; sy < scale; sy++) {
					for (let sx = 0; sx < scale; sx++) {
						setTreePixel(data, atlasW, ox, gx * scale + sx, gy * scale + sy, r, g, b, a);
					}
				}
			}
		}
	}
}

function getTreePixel(
	tp: number, gx: number, gy: number, cx: number,
	v1: number, v2: number, seed: number, ph: number,
	bk: [number, number, number], lf: [number, number, number],
): [number, number, number, number] | undefined {
	if (tp === 4) {
		return getPinePixel(gx, gy, cx, v1, v2, seed, bk, lf);
	}

	if (tp === 2) {
		return getBirchPixel(gx, gy, cx, v1, v2, seed, ph, bk, lf);
	}

	if (tp === 5) {
		return getWillowPixel(gx, gy, cx, v1, seed, bk, lf);
	}

	// Oak (0), Cherry (1), Autumn (3) — round canopy
	return getRoundTreePixel(tp, gx, gy, cx, v1, v2, seed, ph, bk, lf);
}

function getPinePixel(
	gx: number, gy: number, cx: number,
	v1: number, v2: number, seed: number,
	bk: [number, number, number], lf: [number, number, number],
): [number, number, number, number] | undefined {
	const trT = 10 - Math.floor(v2);
	if (gy >= trT && gy <= 14 && Math.abs(gx - cx) < 1) {
		return [...bk, 255];
	}

	if (gy === 15 && Math.abs(gx - cx) <= 1) {
		return [20, 31, 10, 115];
	}

	const baseY = 1 + Math.floor(v1 * 2);
	for (let i = 0; i < 3; i++) {
		const tT = baseY + i * 3;
		const tB = tT + 3;
		if (gy >= tT && gy <= tB) {
			const fr = (gy - tT) / 3;
			const halfW = 0.5 + fr * (2.2 + i * 0.7);
			const edgeNoise = (treeHash(seed + gy * 7.1 + i * 97) - 0.5) * 0.7;
			if (Math.abs(gx - cx) <= halfW + edgeNoise) {
				let m = 1;
				if (gy < tT + 1) {
					m = 1.18;
				} else if (gy >= tB) {
					m = 0.72;
				}

				return [lf[0] * m, lf[1] * m, lf[2] * m, 255];
			}
		}
	}

	return undefined;
}

function getBirchPixel(
	gx: number, gy: number, cx: number,
	v1: number, v2: number, seed: number, ph: number,
	bk: [number, number, number], lf: [number, number, number],
): [number, number, number, number] | undefined {
	const trT = 5 - Math.floor(v2);
	if (gy >= trT && gy <= 14 && Math.abs(gx - cx) < 1) {
		const isMark = (gy + Math.floor(v1 * 3)) % 3 < 1 && ph > 0.4;
		const c: [number, number, number] = isMark ? [56, 56, 51] : bk;
		return [...c, 255];
	}

	if (gy === 15 && Math.abs(gx - cx) <= 1) {
		return [20, 31, 10, 115];
	}

	const cY = trT - 2.5;
	const rX = 2.5 + v1 * 1.2;
	const rY = 3.5 + v2 * 1.5;
	const ddx = (gx - cx) / rX;
	const ddy = (gy - cY) / rY;
	const dd = ddx * ddx + ddy * ddy;
	const edgeNoise = (treeHash(seed + gx * 11.3 + gy * 19.7) - 0.5) * 0.25;
	if (dd <= 1 + edgeNoise) {
		let m = 1;
		if (ddy < -0.35) {
			m = 1.18;
		} else if (ddy > 0.35) {
			m = 0.78;
		}

		return [lf[0] * m, lf[1] * m, lf[2] * m, 255];
	}

	return undefined;
}

function getWillowPixel(
	gx: number, gy: number, cx: number,
	v1: number, seed: number,
	bk: [number, number, number], lf: [number, number, number],
): [number, number, number, number] | undefined {
	if (gy >= 7 && gy <= 14 && Math.abs(gx - cx) < 1) {
		return [...bk, 255];
	}

	if (gy === 15 && Math.abs(gx - cx) <= 2) {
		return [20, 31, 10, 115];
	}

	const cY = 4.5;
	const cR = 4.5 + v1;
	const dx = gx - cx;
	const dy = gy - cY;
	const dist = Math.sqrt(dx * dx + dy * dy);
	const edgeNoise = (treeHash(seed + gx * 13.3 + gy * 23.7) - 0.5) * 1;
	if (dist <= cR + edgeNoise) {
		let m = 1;
		if (gy < cY - cR * 0.3) {
			m = 1.15;
		} else if (gy > cY + cR * 0.15) {
			m = 0.82;
		}

		return [lf[0] * m, lf[1] * m, lf[2] * m, 255];
	}

	// Vines
	for (let i = 0; i < 6; i++) {
		const vs = seed + i * 7.3;
		if (treeHash(vs) > 0.55) {
			continue;
		}

		const vx = cx - 3 + i * 1.2 + treeHash(vs + 1) * 0.5;
		const vineStart = cY + cR * 0.5;
		const vineLength = 2 + treeHash(vs + 2) * 2.5;
		if (Math.abs(gx - Math.floor(vx)) < 1 && gy >= vineStart && gy < vineStart + vineLength) {
			return [lf[0] * 0.82, lf[1] * 0.82, lf[2] * 0.82, 255];
		}
	}

	return undefined;
}

function getRoundTreePixel(
	tp: number, gx: number, gy: number, cx: number,
	v1: number, v2: number, seed: number, ph: number,
	bk: [number, number, number], lf: [number, number, number],
): [number, number, number, number] | undefined {
	const trT = 9 - Math.floor(v2 * 2);
	if (gy >= trT && gy <= 14 && Math.abs(gx - cx) < 1) {
		return [...bk, 255];
	}

	if (gy === 15 && Math.abs(gx - cx) <= 2) {
		return [20, 31, 10, 115];
	}

	const cR = 4.5 + v1 * 1.5;
	const cY = trT - cR + 1.5;
	const dx = gx - cx;
	const dy = gy - cY;
	const dist = Math.sqrt(dx * dx + dy * dy);
	const edgeNoise = (treeHash(seed + gx * 11.3 + gy * 19.7) - 0.5) * 1;
	if (dist <= cR + edgeNoise) {
		let m = 1;
		if (gy < cY - cR * 0.3) {
			m = 1.22;
		} else if (gy > cY + cR * 0.3) {
			m = 0.72;
		}

		if (dist > cR + edgeNoise - 1.2) {
			m *= 0.88;
		}

		let [r, g, b] = [lf[0] * m, lf[1] * m, lf[2] * m];
		if (tp === 1 && ph > 0.82) {
			r = 255;
			g = 245;
			b = 250;
		}

		return [r, g, b, 255];
	}

	return undefined;
}

const TREE_TYPE_COUNT = 6;
const TREE_ATLAS_WIDTH = TEX_SIZE * TREE_TYPE_COUNT; // 384

/** Generate a 384×64 RGBA atlas with 6 tree type sprites. */
export function generateTreeAtlas(): ImageData {
	const atlas = new ImageData(TREE_ATLAS_WIDTH, TEX_SIZE);
	for (let tp = 0; tp < TREE_TYPE_COUNT; tp++) {
		renderTreeType(atlas.data, TREE_ATLAS_WIDTH, tp * TEX_SIZE, tp, tp * 1000 + 42);
	}

	return atlas;
}

/** Number of tree types in the atlas. */
export const TREE_TYPES = TREE_TYPE_COUNT;

// ── Public API ──────────────────────────────────────────────────

/** Get a texture by id — generates on first access and caches. */
export function getTexture(id: string): ImageData {
	let tex = registry.get(id);
	if (tex) {
		return tex;
	}

	const gen = generators[id];
	tex = gen ? gen() : generateFallback(id);
	registry.set(id, tex);
	return tex;
}

/** Texture size in pixels (always square). */
export const TEXTURE_SIZE = TEX_SIZE;

/** Clear cached textures (e.g. on mode change). */
export function clearTextureCache(): void {
	registry.clear();
}

// ── Fallback ────────────────────────────────────────────────────

function generateFallback(id: string): ImageData {
	const img = createTexData();
	const d = img.data;
	// Hash the id for a deterministic color
	let hash = 0;
	for (let i = 0; i < id.length; i++) {
		hash = Math.trunc((hash << 5) - hash + id.codePointAt(i)!);
	}

	const r = 80 + ((hash >>> 0) % 100);
	const g = 80 + ((hash >>> 8) % 100);
	const b = 80 + ((hash >>> 16) % 100);
	for (let y = 0; y < TEX_SIZE; y++) {
		for (let x = 0; x < TEX_SIZE; x++) {
			const checker = ((x >> 3) + (y >> 3)) % 2 === 0;
			const m = checker ? 1 : 0.7;
			setPixel(d, x, y, r * m, g * m, b * m);
		}
	}

	return img;
}
