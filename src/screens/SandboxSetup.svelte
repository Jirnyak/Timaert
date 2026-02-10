<script lang="ts">
	import {onMount} from 'svelte';
	import {type GameState, type LayerParams, defaultParams, createGameState} from '../game/state';
	import {MapGenerator} from '../webgl/map-generator';

	type Props = {
		onStart: (state: GameState) => void;
		onBack: () => void;
	};

	let {onStart, onBack}: Props = $props();

	let parameters: LayerParams = $state({...defaultParams});
	let canvas: HTMLCanvasElement;
	let generator: MapGenerator | undefined;

	onMount(() => {
		generator = new MapGenerator(canvas, parameters);
		generator.generateAll();
		renderPreview();

		return () => {
			generator?.destroy();
		};
	});

	function renderPreview() {
		if (!generator || !canvas) {
			return;
		}

		const w = canvas.clientWidth;
		const h = canvas.clientHeight;
		canvas.width = w;
		canvas.height = h;
		generator.render('visual', w, h);
	}

	function regenerate() {
		if (!generator) {
			return;
		}

		generator.updateParams(parameters);
		generator.generateAll();
		renderPreview();
	}

	function randomSeed() {
		parameters.seed = Math.floor(Math.random() * 100_000);
		regenerate();
	}

	function handleStart() {
		if (!generator) {
			return;
		}

		generator.updateParams(parameters);
		generator.generateAll();
		const cities = generator.getCities();
		const dims = generator.getMapDimensions();
		const state = createGameState(parameters, cities, dims.width, dims.height);
		onStart(state);
	}

</script>

<div class="flex h-full w-full" style="background: linear-gradient(to bottom, #2a1810, #1a0f08);">
	<!-- Control panel -->
	<div class="flex w-80 flex-col overflow-y-auto p-4" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-right: 4px solid #6b4f3a;">
		<h2 class="mb-4 text-center font-sans text-xl font-bold" style="color: #8b6f3a; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">Sandbox Setup</h2>

		<!-- Seed -->
		<div class="mb-4">
			<!-- svelte-ignore a11y_label_has_associated_control -->
			<label class="mb-1 block font-sans text-sm" style="color: #6a5a4a;">Seed</label>
			<div class="flex gap-2">
				<input
					type="number"
					bind:value={parameters.seed}
					onchange={regenerate}
					class="w-full rounded border-2 px-2 py-1 font-sans text-sm"
					style="background: linear-gradient(to bottom, #f0e8d8, #e0d8c8); border-color: #8b6f47; color: #3d2817;"
				/>
				<button
					onclick={randomSeed}
					class="rounded border-2 px-3 py-1 font-sans text-sm transition"
					style="background: linear-gradient(to bottom, #c8b89f, #b8a88f); border-color: #8b6f47;"
					onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c8b89f)'}
					onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b8a88f)'}
				>
					&#127922;
				</button>
			</div>
		</div>

		<!-- Settlement params -->
		<h3 class="mb-2 font-sans text-xs font-semibold uppercase tracking-wide" style="color: #7a6a5a;">Settlements</h3>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Cities</span><span style="color: #7a6a5a;">{parameters.numCities}</span></div>
			<input type="range" min="10" max="300" step="10" bind:value={parameters.numCities} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Min Distance</span><span style="color: #7a6a5a;">{parameters.minCityDistance.toFixed(2)}</span></div>
			<input type="range" min="0.02" max="0.15" step="0.01" bind:value={parameters.minCityDistance} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-4">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Max Connections</span><span style="color: #7a6a5a;">{parameters.maxConnections}</span></div>
			<input type="range" min="1" max="6" step="1" bind:value={parameters.maxConnections} onchange={regenerate} class="w-full" />
		</div>

		<!-- Terrain params -->
		<h3 class="mb-2 font-sans text-xs font-semibold uppercase tracking-wide" style="color: #7a6a5a;">Terrain</h3>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Height Scale</span><span style="color: #7a6a5a;">{parameters.heightScale.toFixed(1)}</span></div>
			<input type="range" min="0.5" max="2" step="0.1" bind:value={parameters.heightScale} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Moisture Scale</span><span style="color: #7a6a5a;">{parameters.moistureScale.toFixed(1)}</span></div>
			<input type="range" min="0.5" max="2" step="0.1" bind:value={parameters.moistureScale} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Sea Level</span><span style="color: #7a6a5a;">{parameters.seaLevel.toFixed(2)}</span></div>
			<input type="range" min="0.1" max="0.6" step="0.01" bind:value={parameters.seaLevel} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Snow Level</span><span style="color: #7a6a5a;">{parameters.snowLevel.toFixed(2)}</span></div>
			<input type="range" min="0.6" max="0.95" step="0.01" bind:value={parameters.snowLevel} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Domain Warp</span><span style="color: #7a6a5a;">{parameters.domainWarp.toFixed(2)}</span></div>
			<input type="range" min="0" max="1" step="0.05" bind:value={parameters.domainWarp} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-4">
			<div class="flex justify-between font-sans text-sm"><span style="color: #5a4a3a;">Road Curviness</span><span style="color: #7a6a5a;">{parameters.roadWarpIntensity.toFixed(1)}</span></div>
			<input type="range" min="0" max="2" step="0.1" bind:value={parameters.roadWarpIntensity} onchange={regenerate} class="w-full" />
		</div>

		<!-- Actions -->
		<div class="mt-auto flex flex-col gap-2 pt-4">
			<button
				onclick={regenerate}
				class="w-full rounded border-2 py-2 font-sans font-semibold transition"
				style="background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); border-color: #5a6a7a; color: #f0e8d8;"
				onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #9aaaba, #7a8a9a)'}
				onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #8a9aaa, #6a7a8a)'}
			>
				Regenerate
			</button>
			<button
				onclick={handleStart}
				class="w-full rounded border-2 py-2 font-sans font-semibold transition"
				style="background: linear-gradient(to bottom, #8aaa8a, #6a8a6a); border-color: #5a7a5a; color: #f0e8d8;"
				onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #9aba9a, #7a9a7a)'}
				onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #8aaa8a, #6a8a6a)'}
			>
				Start Game
			</button>
			<button
				onclick={onBack}
				class="w-full rounded border-2 py-2 font-sans transition"
				style="background: linear-gradient(to bottom, #a89880, #988870); border-color: #7a6a5a; color: #3d2817;"
				onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'}
				onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #a89880, #988870)'}
			>
				Back
			</button>
		</div>
	</div>

	<!-- Map preview -->
	<div class="relative flex-1">
		<canvas bind:this={canvas} class="h-full w-full"></canvas>
	</div>
</div>
