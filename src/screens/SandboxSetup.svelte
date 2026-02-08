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

<div class="flex h-full w-full bg-gray-950">
	<!-- Control panel -->
	<div class="flex w-80 flex-col overflow-y-auto bg-gray-900 p-4 text-white">
		<h2 class="mb-4 text-center font-sans text-xl font-bold text-cyan-400">Sandbox Setup</h2>

		<!-- Seed -->
		<div class="mb-4">
			<!-- svelte-ignore a11y_label_has_associated_control -->
			<label class="mb-1 block font-sans text-sm text-gray-400">Seed</label>
			<div class="flex gap-2">
				<input
					type="number"
					bind:value={parameters.seed}
					onchange={regenerate}
					class="w-full rounded border border-gray-700 bg-gray-800 px-2 py-1 font-sans text-sm text-white"
				/>
				<button
					onclick={randomSeed}
					class="rounded bg-gray-700 px-3 py-1 font-sans text-sm hover:bg-gray-600"
				>
					&#127922;
				</button>
			</div>
		</div>

		<!-- Settlement params -->
		<h3 class="mb-2 font-sans text-xs font-semibold uppercase tracking-wide text-gray-500">Settlements</h3>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Cities</span><span class="text-gray-400">{parameters.numCities}</span></div>
			<input type="range" min="10" max="300" step="10" bind:value={parameters.numCities} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Min Distance</span><span class="text-gray-400">{parameters.minCityDistance.toFixed(2)}</span></div>
			<input type="range" min="0.02" max="0.15" step="0.01" bind:value={parameters.minCityDistance} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-4">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Max Connections</span><span class="text-gray-400">{parameters.maxConnections}</span></div>
			<input type="range" min="1" max="6" step="1" bind:value={parameters.maxConnections} onchange={regenerate} class="w-full" />
		</div>

		<!-- Terrain params -->
		<h3 class="mb-2 font-sans text-xs font-semibold uppercase tracking-wide text-gray-500">Terrain</h3>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Height Scale</span><span class="text-gray-400">{parameters.heightScale.toFixed(1)}</span></div>
			<input type="range" min="0.5" max="2" step="0.1" bind:value={parameters.heightScale} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Moisture Scale</span><span class="text-gray-400">{parameters.moistureScale.toFixed(1)}</span></div>
			<input type="range" min="0.5" max="2" step="0.1" bind:value={parameters.moistureScale} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Sea Level</span><span class="text-gray-400">{parameters.seaLevel.toFixed(2)}</span></div>
			<input type="range" min="0.1" max="0.6" step="0.01" bind:value={parameters.seaLevel} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Snow Level</span><span class="text-gray-400">{parameters.snowLevel.toFixed(2)}</span></div>
			<input type="range" min="0.6" max="0.95" step="0.01" bind:value={parameters.snowLevel} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Domain Warp</span><span class="text-gray-400">{parameters.domainWarp.toFixed(2)}</span></div>
			<input type="range" min="0" max="1" step="0.05" bind:value={parameters.domainWarp} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-4">
			<div class="flex justify-between font-sans text-sm"><span class="text-gray-300">Road Curviness</span><span class="text-gray-400">{parameters.roadWarpIntensity.toFixed(1)}</span></div>
			<input type="range" min="0" max="2" step="0.1" bind:value={parameters.roadWarpIntensity} onchange={regenerate} class="w-full" />
		</div>

		<!-- Actions -->
		<div class="mt-auto flex flex-col gap-2 pt-4">
			<button
				onclick={regenerate}
				class="w-full rounded bg-blue-700 py-2 font-sans font-semibold text-white hover:bg-blue-600"
			>
				Regenerate
			</button>
			<button
				onclick={handleStart}
				class="w-full rounded bg-green-700 py-2 font-sans font-semibold text-white hover:bg-green-600"
			>
				Start Game
			</button>
			<button
				onclick={onBack}
				class="w-full rounded border border-gray-600 bg-gray-800 py-2 font-sans text-white hover:bg-gray-700"
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
