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

	type LayerKey = 'player' | 'cities' | 'cityNames' | 'villages' | 'villageNames' | 'questMarkers' | 'poiMarkers' | 'dangerMarkers';

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
		// Simplified politics view - show territories with different colors
		ctx.fillStyle = '#304050';
		ctx.fillRect(x, y, size, size);

		// TODO: In a full implementation, this would read settlement/territory data
		// and color regions accordingly
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
