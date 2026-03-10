<script lang="ts">
	import {onMount} from 'svelte';
	import {
		type GameState, type LayerParams, defaultParams, createGameState,
	} from '../game/state';
	import {MapGenerator} from '../webgl/map-generator';
	import {
		color, accentHeadingStyle, btnProps, bodyStyle, mutedStyle,
	} from '../ui/theme';

	type Props = {
		onStart: (state: GameState) => void;
		onBack: () => void;
	};

	const {onStart, onBack}: Props = $props();

	const parameters: LayerParams = $state({...defaultParams});
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

<div class="flex h-full w-full" style="background: {color.pageGradient};">
	<!-- Control panel -->
	<div class="flex w-80 flex-col overflow-y-auto p-4" style="background: {color.panelBg}; border-right: 4px solid {color.panelBorder};">
		<h2 class="mb-4 text-center font-sans text-xl font-bold" style={accentHeadingStyle}>Sandbox Setup</h2>

		<!-- Seed -->
		<div class="mb-4">
			<!-- svelte-ignore a11y_label_has_associated_control -->
			<label class="mb-1 block font-sans text-sm" style="color: {color.subtitle};">Seed</label>
			<div class="flex gap-2">
				<input
					type="number"
					bind:value={parameters.seed}
					onchange={regenerate}
					class="w-full rounded border-2 px-2 py-1 font-sans text-sm"
					style="background: {color.inputBg}; border-color: {color.divider}; color: {color.heading};"
				/>
				<button
					onclick={randomSeed}
					class="rounded border-2 px-3 py-1 font-sans text-sm transition"
					{...btnProps('secondary')}
				>
					&#127922;
				</button>
			</div>
		</div>

		<!-- Settlement params -->
		<h3 class="mb-2 font-sans text-xs font-semibold uppercase tracking-wide" style={mutedStyle}>Settlements</h3>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Cities</span><span style={mutedStyle}>{parameters.numCities}</span></div>
			<input type="range" min="10" max="300" step="10" bind:value={parameters.numCities} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Min Distance</span><span style={mutedStyle}>{parameters.minCityDistance.toFixed(2)}</span></div>
			<input type="range" min="0.02" max="0.15" step="0.01" bind:value={parameters.minCityDistance} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-4">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Max Connections</span><span style={mutedStyle}>{parameters.maxConnections}</span></div>
			<input type="range" min="1" max="6" step="1" bind:value={parameters.maxConnections} onchange={regenerate} class="w-full" />
		</div>

		<!-- Terrain params -->
		<h3 class="mb-2 font-sans text-xs font-semibold uppercase tracking-wide" style={mutedStyle}>Terrain</h3>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Height Scale</span><span style={mutedStyle}>{parameters.heightScale.toFixed(1)}</span></div>
			<input type="range" min="0.5" max="2" step="0.1" bind:value={parameters.heightScale} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Moisture Scale</span><span style={mutedStyle}>{parameters.moistureScale.toFixed(1)}</span></div>
			<input type="range" min="0.5" max="2" step="0.1" bind:value={parameters.moistureScale} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Sea Level</span><span style={mutedStyle}>{parameters.seaLevel.toFixed(2)}</span></div>
			<input type="range" min="0.1" max="0.6" step="0.01" bind:value={parameters.seaLevel} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Snow Level</span><span style={mutedStyle}>{parameters.snowLevel.toFixed(2)}</span></div>
			<input type="range" min="0.6" max="0.95" step="0.01" bind:value={parameters.snowLevel} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-1">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Domain Warp</span><span style={mutedStyle}>{parameters.domainWarp.toFixed(2)}</span></div>
			<input type="range" min="0" max="1" step="0.05" bind:value={parameters.domainWarp} onchange={regenerate} class="w-full" />
		</div>
		<div class="mb-4">
			<div class="flex justify-between font-sans text-sm"><span style={bodyStyle}>Road Curviness</span><span style={mutedStyle}>{parameters.roadWarpIntensity.toFixed(1)}</span></div>
			<input type="range" min="0" max="2" step="0.1" bind:value={parameters.roadWarpIntensity} onchange={regenerate} class="w-full" />
		</div>

		<!-- Actions -->
		<div class="mt-auto flex flex-col gap-2 pt-4">
			<button
				onclick={regenerate}
				class="w-full rounded border-2 py-2 font-sans font-semibold transition"
				{...btnProps('action')}
			>
				Regenerate
			</button>
			<button
				onclick={handleStart}
				class="w-full rounded border-2 py-2 font-sans font-semibold transition"
				{...btnProps('success')}
			>
				Start Game
			</button>
			<button
				onclick={onBack}
				class="w-full rounded border-2 py-2 font-sans transition"
				{...btnProps('muted')}
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
