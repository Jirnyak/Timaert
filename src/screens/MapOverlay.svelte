<script lang="ts">
	import {onMount} from 'svelte';
	import {MapGenerator} from '../webgl/map-generator';

	type Props = {
		mapGenerator: MapGenerator;
		mapWidth: number;
		mapHeight: number;
		onClose: () => void;
	};

	const {mapGenerator, mapWidth, mapHeight, onClose}: Props = $props();

	type MapMode = 'world' | 'politics' | 'iron' | 'clay' | 'fertility';

	let currentMode: MapMode = $state('world');
	let canvas: HTMLCanvasElement;
	let offsetX = $state(0);
	let offsetY = $state(0);
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
				return;
			}

			case 'iron':
			case 'clay':
			case 'fertility': {
				renderResourceMap(ctx, drawX, drawY, mapSize, currentMode);
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

			// Create ImageData and draw to canvas
			const imageData = ctx.createImageData(mapWidth, mapHeight);
			imageData.data.set(pixels);

			// Flip vertically (WebGL bottom-left vs canvas top-left)
			const temporaryCanvas = document.createElement('canvas');
			temporaryCanvas.width = mapWidth;
			temporaryCanvas.height = mapHeight;
			const temporaryCtx = temporaryCanvas.getContext('2d');
			if (temporaryCtx) {
				temporaryCtx.putImageData(imageData, 0, 0);
				ctx.save();
				ctx.translate(0, canvas.height);
				ctx.scale(1, -1);
				ctx.drawImage(temporaryCanvas, drawX, canvas.height - drawY - mapSize, mapSize, mapSize);
				ctx.restore();
			}

			gl.bindFramebuffer(gl.FRAMEBUFFER, null);
			gl.deleteFramebuffer(fb);
		}

		// Draw border
		ctx.strokeStyle = '#FFFFFF';
		ctx.lineWidth = 2;
		ctx.strokeRect(drawX, drawY, mapSize, mapSize);
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
		for (let py = 0; py < mapHeight; py++) {
			for (let px = 0; px < mapWidth; px++) {
				const idx = (py * mapWidth + px) * 4;
				const heightIdx = py * mapWidth + px;
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

		// Draw to main canvas
		ctx.drawImage(temporaryCanvas, x, y, size, size);
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

	.instructions {
		margin-top: auto;
		color: rgb(150 150 150);
		font-size: 0.875rem;
		text-shadow: 0 2px 4px rgba(0, 0, 0, 0.8);
	}
</style>
