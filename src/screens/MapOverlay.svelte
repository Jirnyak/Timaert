<script lang="ts">
	import {onMount} from 'svelte';
	import {MapGenerator} from '../webgl/map-generator';
	import {type Marker, MARKER_COLORS, MARKER_GLYPHS} from '../game/markers';
	import type {Settlement, Village} from '../game/state';

	type Props = {
		mapGenerator: MapGenerator;
		mapWidth: number;
		mapHeight: number;
		playerX: number;
		playerY: number;
		settlements: Settlement[];
		villages: Village[];
		markers: Marker[];
		onClose: () => void;
	};

	const {mapGenerator, mapWidth, mapHeight, playerX, playerY, settlements, villages, markers, onClose}: Props = $props();

	type MapMode = 'world' | 'politics' | 'iron' | 'clay' | 'fertility';

	type LayerKey = 'player' | 'cities' | 'cityNames' | 'villages' | 'villageNames' | 'questMarkers' | 'poiMarkers' | 'dangerMarkers' | 'factionNames';

	type LegendEntry = {
		key: LayerKey;
		label: string;
		color: string;
		glyph?: string;
	};

	const LEGEND: LegendEntry[] = [
		{key: 'player', label: 'Player', color: '#ffffff'},
		{key: 'cities', label: 'Cities', color: '#e8c44a'},
		{key: 'cityNames', label: 'City Names', color: '#e8c44a'},
		{key: 'villages', label: 'Villages', color: '#8b6914'},
		{key: 'villageNames', label: 'Village Names', color: '#8b6914'},
		{
			key: 'questMarkers', label: 'Quest Markers', color: MARKER_COLORS.quest, glyph: MARKER_GLYPHS.quest,
		},
		{
			key: 'poiMarkers', label: 'Points of Interest', color: MARKER_COLORS.poi, glyph: MARKER_GLYPHS.poi,
		},
		{
			key: 'dangerMarkers', label: 'Danger Zones', color: MARKER_COLORS.danger, glyph: MARKER_GLYPHS.danger,
		},
		{key: 'factionNames', label: 'Faction Names', color: '#f5f1e0'},
	];

	const defaultVisible: Record<LayerKey, boolean> = {
		player: true,
		cities: true,
		cityNames: true,
		villages: true,
		villageNames: false,
		questMarkers: true,
		poiMarkers: true,
		dangerMarkers: true,
		factionNames: false,
	};

	let currentMode: MapMode = $state('world');
	let canvas: HTMLCanvasElement;
	let offsetX = $state(0);
	let offsetY = $state(0);
	let legendOpen = $state(true);
	const layerVisible: Record<LayerKey, boolean> = $state({...defaultVisible});
	let isDragging = false;
	let dragStartX = 0;
	let dragStartY = 0;
	let dragOffsetStartX = 0;
	let dragOffsetStartY = 0;

	const modeLabels: Record<MapMode, string> = {
		world: 'World Map',
		politics: 'Politics',
		iron: 'Iron Resources',
		clay: 'Clay Resources',
		fertility: 'Fertility',
	};

	onMount(() => {
		renderMap();
	});

	function renderMap() {
		if (!canvas || !mapGenerator) {
			return;
		}

		const size = Math.min(canvas.clientWidth, canvas.clientHeight);
		canvas.width = size;
		canvas.height = size;

		const ctx = canvas.getContext('2d');
		if (!ctx) {
			return;
		}

		ctx.fillStyle = '#000000';
		ctx.fillRect(0, 0, canvas.width, canvas.height);

		// Calculate the region to draw
		const mapSize = Math.min(canvas.width, canvas.height);
		const drawX = (canvas.width - mapSize) / 2 + offsetX;
		const drawY = (canvas.height - mapSize) / 2 + offsetY;

		// Get the appropriate texture based on mode
		let texture: WebGLTexture | undefined;

		switch (currentMode) {
			case 'world': {
				texture = mapGenerator.getVisualTexture();
				break;
			}

			case 'politics': {
				// For politics, use a simplified color scheme
				renderPoliticsMap(ctx, drawX, drawY, mapSize);
				drawOverlays(ctx, drawX, drawY, mapSize);
				if (layerVisible.factionNames) {
					drawFactionNames(ctx, drawX, drawY, mapSize);
				}

				return;
			}

			case 'iron':
			case 'clay':
			case 'fertility': {
				renderResourceMap(ctx, drawX, drawY, mapSize, currentMode);
				drawOverlays(ctx, drawX, drawY, mapSize);
				return;
			}

			default: {
				break;
			}
		}

		if (texture) {
			// Read texture data from WebGL and render to canvas
			const gl = mapGenerator.getGL();
			const fb = gl.createFramebuffer();
			gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
			gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);

			const pixels = new Uint8Array(mapWidth * mapHeight * 4);
			gl.readPixels(0, 0, mapWidth, mapHeight, gl.RGBA, gl.UNSIGNED_BYTE, pixels);

			// Flip rows: GL readPixels returns bottom-up, canvas needs top-down
			// Row 0 in GL = south (low Y), flip so north (high Y) is at canvas top
			const imageData = ctx.createImageData(mapWidth, mapHeight);
			const rowBytes = mapWidth * 4;
			for (let y = 0; y < mapHeight; y++) {
				const src = (mapHeight - 1 - y) * rowBytes;
				const dst = y * rowBytes;
				imageData.data.set(pixels.subarray(src, src + rowBytes), dst);
			}

			const temporaryCanvas = document.createElement('canvas');
			temporaryCanvas.width = mapWidth;
			temporaryCanvas.height = mapHeight;
			const temporaryCtx = temporaryCanvas.getContext('2d');
			if (temporaryCtx) {
				temporaryCtx.putImageData(imageData, 0, 0);
				ctx.drawImage(temporaryCanvas, drawX, drawY, mapSize, mapSize);
			}

			gl.bindFramebuffer(gl.FRAMEBUFFER, null);
			gl.deleteFramebuffer(fb);
		}

		// Draw border
		ctx.strokeStyle = '#FFFFFF';
		ctx.lineWidth = 2;
		ctx.strokeRect(drawX, drawY, mapSize, mapSize);

		// Draw all overlays
		drawOverlays(ctx, drawX, drawY, mapSize);
	}

	function renderPoliticsMap(ctx: CanvasRenderingContext2D, x: number, y: number, size: number) {
		const politik = mapGenerator.getPolitik();
		if (!politik || politik.cellOwner.length === 0) {
			ctx.fillStyle = '#304050';
			ctx.fillRect(x, y, size, size);
			return;
		}

		const {cellOwner, width: pw, height: ph, kingdoms} = politik;

		// Build a flat lookup: defIdx (1-based, 0=wild) -> [r,g,b]
		const palette: Array<[number, number, number]> = [[24, 22, 30]]; // Wild = dark
		for (const k of Object.values(kingdoms)) {
			const slot = k.defIdx + 1;
			while (palette.length <= slot) {
				palette.push([24, 22, 30]);
			}

			palette[slot] = [
				Math.round(k.rgb[0] * 255),
				Math.round(k.rgb[1] * 255),
				Math.round(k.rgb[2] * 255),
			];
		}

		const temporaryCanvas = document.createElement('canvas');
		temporaryCanvas.width = pw;
		temporaryCanvas.height = ph;
		const temporaryCtx = temporaryCanvas.getContext('2d');
		if (!temporaryCtx) {
			return;
		}

		const imageData = temporaryCtx.createImageData(pw, ph);
		const pix = imageData.data;

		// Flip rows so north is up (cellOwner is stored y=0 south just like GL).
		for (let py = 0; py < ph; py++) {
			const srcY = ph - 1 - py;
			for (let px = 0; px < pw; px++) {
				const id = cellOwner[srcY * pw + px];
				const rgb = palette[id] ?? palette[0];
				const di = (py * pw + px) * 4;
				pix[di] = rgb[0];
				pix[di + 1] = rgb[1];
				pix[di + 2] = rgb[2];
				pix[di + 3] = 255;
			}
		}

		// Border outline: darken cells whose kingdom differs from a neighbour.
		for (let py = 0; py < ph; py++) {
			for (let px = 0; px < pw; px++) {
				const id = cellOwner[(ph - 1 - py) * pw + px];
				if (id === 0) {
					continue;
				}

				const e = cellOwner[(ph - 1 - py) * pw + ((px + 1) % pw)];
				const w = cellOwner[(ph - 1 - py) * pw + ((px - 1 + pw) % pw)];
				const n = cellOwner[(ph - 1 - ((py - 1 + ph) % ph)) * pw + px];
				const s = cellOwner[(ph - 1 - ((py + 1) % ph)) * pw + px];
				if (e !== id || w !== id || n !== id || s !== id) {
					const di = (py * pw + px) * 4;
					pix[di] = Math.round(pix[di] * 0.35);
					pix[di + 1] = Math.round(pix[di + 1] * 0.35);
					pix[di + 2] = Math.round(pix[di + 2] * 0.35);
				}
			}
		}

		temporaryCtx.putImageData(imageData, 0, 0);
		ctx.imageSmoothingEnabled = false;
		ctx.drawImage(temporaryCanvas, x, y, size, size);
	}

	function renderResourceMap(ctx: CanvasRenderingContext2D, x: number, y: number, size: number, mode: 'iron' | 'clay' | 'fertility') {
		// Get heightmap or other data from map generator
		const travData = mapGenerator.getTerrainData();
		if (!travData) {
			ctx.fillStyle = '#203050';
			ctx.fillRect(x, y, size, size);
			return;
		}

		const data = travData.heightData;

		// Create a canvas for the resource map
		const temporaryCanvas = document.createElement('canvas');
		temporaryCanvas.width = mapWidth;
		temporaryCanvas.height = mapHeight;
		const temporaryCtx = temporaryCanvas.getContext('2d');
		if (!temporaryCtx) {
			return;
		}

		const imageData = temporaryCtx.createImageData(mapWidth, mapHeight);

		// Map height data to resource colors
		// GL readPixels data is bottom-up: flip so north (high Y) is at canvas top
		for (let py = 0; py < mapHeight; py++) {
			for (let px = 0; px < mapWidth; px++) {
				const idx = (py * mapWidth + px) * 4;
				const srcY = mapHeight - 1 - py;
				const heightIdx = srcY * mapWidth + px;
				const height = data[heightIdx] / 255; // Normalize to 0-1 range

				let r = 0;
				let g = 0;
				let b = 0;

				switch (mode) {
					case 'iron': {
						// Iron in mountains (high elevation)
						if (height > 0.7) {
							const intensity = ((height - 0.7) / 0.3) * 255;
							r = Math.floor(intensity);
							g = Math.floor(intensity * 0.3);
							b = 0;
						} else {
							r = 30;
							g = 40;
							b = 50;
						}

						break;
					}

					case 'clay': {
						// Clay near water/rivers (medium-low elevation)
						if (height > 0.4 && height < 0.6) {
							const intensity = (1 - Math.abs(height - 0.5) * 4) * 255;
							r = Math.floor(intensity * 0.6);
							g = Math.floor(intensity * 0.4);
							b = Math.floor(intensity * 0.2);
						} else {
							r = 30;
							g = 40;
							b = 50;
						}

						break;
					}

					case 'fertility': {
						// Fertility in temperate zones (mid elevation, not too high or low)
						if (height > 0.45 && height < 0.65) {
							let intensity = (1 - Math.abs(height - 0.55) * 10) * 255;
							intensity = Math.max(0, Math.min(255, intensity));
							r = Math.floor(intensity * 0.2);
							g = Math.floor(intensity);
							b = Math.floor(intensity * 0.3);
						} else {
							r = 30;
							g = 40;
							b = 50;
						}

						break;
					}

					default: {
						break;
					}
				}

				imageData.data[idx] = r;
				imageData.data[idx + 1] = g;
				imageData.data[idx + 2] = b;
				imageData.data[idx + 3] = 255;
			}
		}

		temporaryCtx.putImageData(imageData, 0, 0);

		// Draw to main canvas (rows already flipped in the loop above)
		ctx.drawImage(temporaryCanvas, x, y, size, size);
	}

	// ── Coordinate helpers ──

	function worldToCanvas(wx: number, wy: number, drawX: number, drawY: number, mapSize: number): {cx: number; cy: number} {
		return {
			cx: (wx / mapWidth) * mapSize + drawX,
			cy: (1 - wy / mapHeight) * mapSize + drawY,
		};
	}

	// ── Overlay drawing ──

	function drawOverlays(ctx: CanvasRenderingContext2D, drawX: number, drawY: number, mapSize: number) {
		// Pass 1: icons/dots (drawn first, behind text)
		if (layerVisible.villages) {
			drawVillages(ctx, drawX, drawY, mapSize);
		}

		if (layerVisible.cities) {
			drawSettlements(ctx, drawX, drawY, mapSize);
		}

		if (layerVisible.questMarkers || layerVisible.poiMarkers || layerVisible.dangerMarkers) {
			drawMarkers(ctx, drawX, drawY, mapSize);
		}

		if (layerVisible.player) {
			drawPlayerMarker(ctx, drawX, drawY, mapSize);
		}

		// Pass 2: text labels (drawn last, on top of everything)
		if (layerVisible.villageNames) {
			drawVillageNames(ctx, drawX, drawY, mapSize);
		}

		if (layerVisible.cityNames) {
			drawSettlementNames(ctx, drawX, drawY, mapSize);
		}
	}

	function drawPlayerMarker(ctx: CanvasRenderingContext2D, drawX: number, drawY: number, mapSize: number) {
		const {cx, cy} = worldToCanvas(playerX, playerY, drawX, drawY, mapSize);
		ctx.beginPath();
		ctx.arc(cx, cy, 4, 0, Math.PI * 2);
		ctx.fillStyle = '#FFFFFF';
		ctx.fill();
		ctx.strokeStyle = '#000000';
		ctx.lineWidth = 1.5;
		ctx.stroke();
	}

	function drawSettlements(ctx: CanvasRenderingContext2D, drawX: number, drawY: number, mapSize: number) {
		for (const s of settlements) {
			const {cx, cy} = worldToCanvas(s.x, s.y, drawX, drawY, mapSize);
			ctx.beginPath();
			ctx.arc(cx, cy, 4, 0, Math.PI * 2);
			ctx.fillStyle = '#e8c44a';
			ctx.fill();
			ctx.strokeStyle = '#000000';
			ctx.lineWidth = 1;
			ctx.stroke();
		}
	}

	function drawSettlementNames(ctx: CanvasRenderingContext2D, drawX: number, drawY: number, mapSize: number) {
		ctx.font = '10px sans-serif';
		ctx.textAlign = 'center';
		for (const s of settlements) {
			const {cx, cy} = worldToCanvas(s.x, s.y, drawX, drawY, mapSize);
			ctx.fillStyle = '#000000';
			ctx.fillText(s.name, cx + 1, cy - 7);
			ctx.fillStyle = '#e8c44a';
			ctx.fillText(s.name, cx, cy - 8);
		}
	}

	function drawVillages(ctx: CanvasRenderingContext2D, drawX: number, drawY: number, mapSize: number) {
		for (const v of villages) {
			const {cx, cy} = worldToCanvas(v.x, v.y, drawX, drawY, mapSize);
			ctx.beginPath();
			ctx.arc(cx, cy, 2.5, 0, Math.PI * 2);
			ctx.fillStyle = '#8b6914';
			ctx.fill();
			ctx.strokeStyle = '#000000';
			ctx.lineWidth = 0.8;
			ctx.stroke();
		}
	}

	function drawVillageNames(ctx: CanvasRenderingContext2D, drawX: number, drawY: number, mapSize: number) {
		ctx.font = '8px sans-serif';
		ctx.textAlign = 'center';
		for (const v of villages) {
			const {cx, cy} = worldToCanvas(v.x, v.y, drawX, drawY, mapSize);
			ctx.fillStyle = '#000000';
			ctx.fillText(v.name, cx + 1, cy - 5);
			ctx.fillStyle = '#8b6914';
			ctx.fillText(v.name, cx, cy - 6);
		}
	}

	function drawMarkers(ctx: CanvasRenderingContext2D, drawX: number, drawY: number, mapSize: number) {
		const styleVisible: Record<string, boolean> = {
			quest: layerVisible.questMarkers,
			poi: layerVisible.poiMarkers,
			danger: layerVisible.dangerMarkers,
			waypoint: layerVisible.poiMarkers,
		};

		ctx.font = 'bold 12px sans-serif';
		ctx.textAlign = 'center';
		ctx.textBaseline = 'middle';

		for (const m of markers) {
			if (!styleVisible[m.style]) {
				continue;
			}

			const {cx, cy} = worldToCanvas(m.x, m.y, drawX, drawY, mapSize);
			const color = MARKER_COLORS[m.style];
			const glyph = MARKER_GLYPHS[m.style];

			// Shadow
			ctx.fillStyle = '#000000';
			ctx.fillText(glyph, cx + 1, cy + 1);
			// Glyph
			ctx.fillStyle = color;
			ctx.fillText(glyph, cx, cy);
		}

		ctx.textBaseline = 'alphabetic';
	}

	// EU/HOI/CK-style faction names: tracked all-caps drawn inside each
	// kingdom's territory. Torus-wrap aware: each kingdom's cells are
	// unwrapped relative to the first cell encountered before computing
	// centroid + PCA. Font size is fit to the actual owned chord through
	// the placement point along the long axis, and capped by the
	// perpendicular owned chord so labels never spill into neighbours.
	function drawFactionNames(ctx: CanvasRenderingContext2D, drawX: number, drawY: number, mapSize: number) {
		const politik = mapGenerator.getPolitik();
		if (!politik || politik.cellOwner.length === 0) {
			return;
		}

		const {cellOwner, width: pw, height: ph, kingdoms} = politik;

		const halfW = pw / 2;
		const halfH = ph / 2;

		// Per-kingdom stats accumulated in unwrapped coords (relative to
		// each kingdom's first-seen cell, the "anchor"). PCA values are
		// translation-invariant; the centroid is recovered as anchor + mean.
		type Stat = {
			count: number;
			ax: number; ay: number; // Anchor cell coords (any owned cell)
			sx: number; sy: number;
			sxx: number; syy: number; sxy: number;
		};
		const stats = new Map<number, Stat>();
		for (let py = 0; py < ph; py++) {
			for (let px = 0; px < pw; px++) {
				const id = cellOwner[py * pw + px];
				if (id === 0) {
					continue;
				}

				let s = stats.get(id);
				if (!s) {
					s = {
						count: 0,
						ax: px, ay: py,
						sx: 0, sy: 0,
						sxx: 0, syy: 0, sxy: 0,
					};
					stats.set(id, s);
				}

				// Unwrap delta into [-half, +half) so a kingdom that crosses
				// the seam still has a coherent point cloud.
				let dx = px - s.ax;
				if (dx > halfW) {
					dx -= pw;
				} else if (dx < -halfW) {
					dx += pw;
				}

				let dy = py - s.ay;
				if (dy > halfH) {
					dy -= ph;
				} else if (dy < -halfH) {
					dy += ph;
				}

				s.count++;
				s.sx += dx;
				s.sy += dy;
				s.sxx += dx * dx;
				s.syy += dy * dy;
				s.sxy += dx * dy;
			}
		}

		// Map defIdx (slot id) -> Kingdom for name + color.
		const slotToKingdom = new Map<number, {name: string; rgb: [number, number, number]}>();
		for (const k of Object.values(kingdoms)) {
			slotToKingdom.set(k.defIdx + 1, {name: k.name, rgb: k.rgb});
		}

		const cellPx = mapSize / pw;

		// Owner sample with toroidal wrap.
		const ownerAt = (px: number, py: number): number => {
			const wx = ((Math.round(px) % pw) + pw) % pw;
			const wy = ((Math.round(py) % ph) + ph) % ph;
			return cellOwner[(wy * pw) + wx];
		};

		// Walk a ray from (px,py) along (dx,dy) (unit step) until we leave
		// owned territory. Returns the run length in cells.
		const rayLen = (px: number, py: number, dx: number, dy: number, id: number): number => {
			const max = Math.max(pw, ph);
			for (let t = 0; t < max; t++) {
				if (ownerAt(px + (dx * t), py + (dy * t)) !== id) {
					return t;
				}
			}

			return max;
		};

		const chordThrough = (px: number, py: number, dx: number, dy: number, id: number): number => {
			if (ownerAt(px, py) !== id) {
				return 0;
			}

			return rayLen(px, py, dx, dy, id) + rayLen(px, py, -dx, -dy, id) - 1;
		};

		ctx.save();
		ctx.textAlign = 'center';
		ctx.textBaseline = 'middle';
		ctx.lineJoin = 'round';
		ctx.miterLimit = 2;

		for (const [id, s] of stats) {
			const k = slotToKingdom.get(id);
			if (!k) {
				continue;
			}

			// Centroid in unwrapped space, then re-wrapped to grid.
			const meanDx = s.sx / s.count;
			const meanDy = s.sy / s.count;
			let centroidX = s.ax + meanDx;
			let centroidY = s.ay + meanDy;
			centroidX = ((centroidX % pw) + pw) % pw;
			centroidY = ((centroidY % ph) + ph) % ph;

			// PCA on the (dx,dy) cloud — eigenvector of largest eigenvalue
			// gives the long axis; eigenvalues approximate variances.
			const cxx = (s.sxx / s.count) - (meanDx * meanDx);
			const cyy = (s.syy / s.count) - (meanDy * meanDy);
			const cxy = (s.sxy / s.count) - (meanDx * meanDy);
			const tr = cxx + cyy;
			const det = (cxx * cyy) - (cxy * cxy);
			const disc = Math.max(0, ((tr * tr) / 4) - det);
			const lambda1 = (tr / 2) + Math.sqrt(disc); // Largest
			let angle = 0;
			if (Math.abs(cxy) > 1e-3) {
				angle = Math.atan2(lambda1 - cxx, cxy);
			} else if (cyy > cxx) {
				angle = Math.PI / 2;
			}

			const cosA = Math.cos(angle);
			const sinA = Math.sin(angle);

			// Pick placement point: prefer centroid; if it's outside the
			// kingdom (concave / disjoint), spiral outward to the nearest
			// owned cell, then refine by walking along the long axis to the
			// midpoint of the longest local chord.
			let placeX = centroidX;
			let placeY = centroidY;
			if (ownerAt(placeX, placeY) !== id) {
				placeX = s.ax;
				placeY = s.ay;
				// Spiral search outward from the (wrapped) centroid for the
				// nearest owned cell — better than the anchor fallback
				// because anchor is just the first scanned cell.
				const cxR = Math.round(centroidX);
				const cyR = Math.round(centroidY);
				const maxR = Math.min(pw, ph) >> 1;
				let found = false;
				for (let r = 1; r <= maxR && !found; r++) {
					for (let dy = -r; dy <= r && !found; dy++) {
						for (let dx = -r; dx <= r && !found; dx++) {
							if (Math.abs(dx) !== r && Math.abs(dy) !== r) {
								continue;
							}

							if (ownerAt(cxR + dx, cyR + dy) === id) {
								placeX = cxR + dx;
								placeY = cyR + dy;
								found = true;
							}
						}
					}
				}
			}

			// Slide along the long axis to the midpoint of the chord we sit
			// on so the label is centred inside the territory.
			const fwd = rayLen(placeX, placeY, cosA, sinA, id);
			const bwd = rayLen(placeX, placeY, -cosA, -sinA, id);
			const slide = (fwd - bwd) / 2;
			placeX += cosA * slide;
			placeY += sinA * slide;

			// Owned chord through placement point along each axis.
			const longChord = chordThrough(placeX, placeY, cosA, sinA, id);
			const shortChord = chordThrough(placeX, placeY, -sinA, cosA, id);

			// Take the larger of (chord through placement point) and (PCA
			// extent estimate) so highly concave or fragmented kingdoms
			// still get a sensible label size.
			const pcaLong = 2 * Math.sqrt(Math.max(0, lambda1));
			const pcaShort = 2 * Math.sqrt(Math.max(0, tr - lambda1));
			const longCells = Math.max(longChord, pcaLong);
			const shortCells = Math.max(shortChord, pcaShort * 0.6);

			// Convert to pixels with a small inner margin so glyphs don't
			// kiss the border.
			const longPx = longCells * cellPx * 0.9;
			const shortPx = shortCells * cellPx * 0.85;

			const upper = k.name.toUpperCase();
			const tracking = 0.18; // Em-units of letter-spacing
			// Approx serif-cap advance ~0.58em per char.
			const widthFactor = (upper.length * 0.58) + (Math.max(0, upper.length - 1) * tracking);

			// Width-fit: fontSize so the tracked label fits longPx.
			const fontByWidth = longPx / Math.max(1, widthFactor);
			// Height-fit: serif cap-height ≈ 0.72em; halo adds ~0.36em total.
			const fontByHeight = shortPx / 0.95;

			const fontSize = Math.max(9, Math.min(80, Math.min(fontByWidth, fontByHeight)));

			const trackPx = fontSize * tracking;

			// Canvas Y points down → flip angle so text reads horizontally on
			// the map, and keep glyphs upright (no upside-down labels).
			let drawAngle = -angle;
			// Normalise to (-π/2, +π/2] so text isn't rendered upside-down.
			while (drawAngle > Math.PI / 2) {
				drawAngle -= Math.PI;
			}

			while (drawAngle <= -Math.PI / 2) {
				drawAngle += Math.PI;
			}

			const cx = drawX + (placeX * cellPx) + (cellPx / 2);
			const cy = drawY + ((ph - 1 - placeY) * cellPx) + (cellPx / 2);

			ctx.translate(cx, cy);
			ctx.rotate(drawAngle);

			ctx.font = `700 ${fontSize.toFixed(1)}px "Cinzel", "Trajan Pro", Georgia, "Times New Roman", serif`;

			let total = 0;
			const widths: number[] = [];
			for (const ch of upper) {
				const w = ctx.measureText(ch).width;
				widths.push(w);
				total += w;
			}

			total += trackPx * Math.max(0, upper.length - 1);

			const ink = `rgba(${Math.round((k.rgb[0] * 80) + 195)}, ${Math.round((k.rgb[1] * 80) + 190)}, ${Math.round((k.rgb[2] * 80) + 180)}, 1)`;

			let cursor = -total / 2;
			for (const [i, ch] of [...upper].entries()) {
				const w = widths[i];
				const gx = cursor + (w / 2);
				ctx.lineWidth = Math.max(2, fontSize * 0.16);
				ctx.strokeStyle = 'rgba(0, 0, 0, 0.92)';
				ctx.strokeText(ch, gx, 0);
				ctx.fillStyle = ink;
				ctx.fillText(ch, gx, 0);
				cursor += w + trackPx;
			}

			ctx.setTransform(1, 0, 0, 1, 0, 0);
		}

		ctx.restore();
	}

	function cycleMode() {
		const modes: MapMode[] = ['world', 'politics', 'iron', 'clay', 'fertility'];
		const currentIndex = modes.indexOf(currentMode);
		currentMode = modes[(currentIndex + 1) % modes.length];
		renderMap();
	}

	function handleMouseDown(event: MouseEvent) {
		isDragging = true;
		dragStartX = event.clientX;
		dragStartY = event.clientY;
		dragOffsetStartX = offsetX;
		dragOffsetStartY = offsetY;
	}

	function handleMouseMove(event: MouseEvent) {
		if (!isDragging) {
			return;
		}

		const dx = event.clientX - dragStartX;
		const dy = event.clientY - dragStartY;
		offsetX = dragOffsetStartX + dx;
		offsetY = dragOffsetStartY + dy;
		renderMap();
	}

	function handleMouseUp() {
		isDragging = false;
	}

	function handleKeyDown(event: KeyboardEvent) {
		if (event.key === 'Escape' || event.key === 'm' || event.key === 'M') {
			onClose();
		} else if (event.key === 'Tab') {
			event.preventDefault();
			cycleMode();
		}
	}
</script>

<svelte:window onkeydown={handleKeyDown} onmouseup={handleMouseUp} onmousemove={handleMouseMove} />

<div class='map-overlay'>
	<canvas
		bind:this={canvas}
		onmousedown={handleMouseDown}
	></canvas>

	<div class='ui-overlay'>
		<div class='mode-label'>{modeLabels[currentMode]}</div>

		<!-- svelte-ignore a11y_no_static_element_interactions -->
		<!-- svelte-ignore a11y_click_events_have_key_events -->
		<div class='legend'>
			<div class='legend-header' onclick={() => (legendOpen = !legendOpen)}>
				<span class='legend-toggle'>{legendOpen ? '▾' : '▸'}</span>
				Legend
			</div>
			{#if legendOpen}
				<div class='legend-items'>
					{#each LEGEND as entry (entry.key)}
						<label class='legend-item'>
							<input
								type='checkbox'
								checked={layerVisible[entry.key]}
								onchange={() => {
									layerVisible[entry.key] = !layerVisible[entry.key];
									renderMap();
								}}
							/>
							{#if entry.glyph}
								<span class='legend-glyph' style:color={entry.color}>{entry.glyph}</span>
							{:else}
								<span class='legend-dot' style:background={entry.color}></span>
							{/if}
							{entry.label}
						</label>
					{/each}
				</div>
			{/if}
		</div>

		<div class='instructions'>
			<div>[ Tab: Cycle Modes ]</div>
			<div>[ M / ESC: Close ]</div>
		</div>
	</div>
</div>

<style>
	.map-overlay {
		position: fixed;
		inset: 0;
		background: rgba(0, 0, 0, 0.95);
		display: flex;
		align-items: center;
		justify-content: center;
		z-index: 1000;
	}

	canvas {
		width: 90vmin;
		height: 90vmin;
		cursor: grab;
		image-rendering: pixelated;
	}

	canvas:active {
		cursor: grabbing;
	}

	.ui-overlay {
		position: absolute;
		inset: 0;
		pointer-events: none;
		padding: 1rem;
		display: flex;
		flex-direction: column;
		font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen-Sans, Ubuntu, Cantarell, 'Helvetica Neue', sans-serif;
	}

	.mode-label {
		color: white;
		font-size: 1.125rem;
		font-weight: 600;
		text-shadow: 0 2px 4px rgba(0, 0, 0, 0.8);
	}

	.legend {
		margin-top: 0.75rem;
		padding: 0.5rem 0.625rem;
		background: rgba(0, 0, 0, 0.6);
		border-radius: 0.375rem;
		width: fit-content;
		pointer-events: auto;
		user-select: none;
	}

	.legend-header {
		color: rgb(200 200 200);
		font-size: 0.8125rem;
		font-weight: 600;
		text-shadow: 0 1px 3px rgba(0, 0, 0, 0.9);
		cursor: pointer;
	}

	.legend-toggle {
		display: inline-block;
		width: 0.75rem;
		text-align: center;
	}

	.legend-items {
		margin-top: 0.375rem;
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
	}

	.legend-item {
		display: flex;
		align-items: center;
		gap: 0.375rem;
		color: rgb(180 180 180);
		font-size: 0.75rem;
		text-shadow: 0 1px 3px rgba(0, 0, 0, 0.9);
		cursor: pointer;
	}

	.legend-item input[type='checkbox'] {
		width: 12px;
		height: 12px;
		margin: 0;
		accent-color: #6b8;
		cursor: pointer;
	}

	.legend-dot {
		width: 8px;
		height: 8px;
		border-radius: 50%;
		flex-shrink: 0;
	}

	.legend-glyph {
		font-size: 0.875rem;
		font-weight: bold;
		width: 8px;
		text-align: center;
		flex-shrink: 0;
		text-shadow: 0 0 4px currentColor;
	}

	.instructions {
		margin-top: auto;
		color: rgb(150 150 150);
		font-size: 0.875rem;
		text-shadow: 0 2px 4px rgba(0, 0, 0, 0.8);
	}
</style>
