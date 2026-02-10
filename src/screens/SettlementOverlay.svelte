<script lang="ts">
	import type {PlayerState, Settlement} from '../game/state';
	import {CityGenerator} from '../game/city-generator';

	type Props = {
		player: PlayerState;
		settlement: Settlement;
		worldSeed: number;
		onClose: () => void;
		onEnter: () => void;
		onTrade: () => void;
	};

	let {player, settlement, worldSeed, onClose, onEnter, onTrade}: Props = $props();

	let tab = $state<'rest' | 'info' | 'map'>('info');
	let message = $state('');
	let mapUrl = $state('');
	let mapGenerated = $state(false);

	// Lazy generate map only when map tab is accessed
	$effect(() => {
		if (tab === 'map' && !mapGenerated) {
			const seed = worldSeed + settlement.id * 123;
			const gen = new CityGenerator(seed, 128, 128, 'city');
			const data = gen.generate(settlement.population);
			mapUrl = data.visual.toDataURL();
			mapGenerated = true;
		}
	});

	function rest() {
		const cost = 10;
		if (player.gold < cost) {
			message = 'Not enough gold to rest! (10g)';
			return;
		}

		player.gold -= cost;
		player.combatStats.currentHp = player.combatStats.maxHp;
		player.combatStats.currentMp = player.combatStats.maxMp;
		player.combatStats.currentSp = player.combatStats.maxSp;
		message = 'Fully rested! HP/MP/SP restored.';
	}
</script>

<svelte:window onkeydown={event => { if (event.key === 'Escape') onClose(); }} />

<div class="absolute inset-0 flex items-center justify-center z-[100]" style="background: rgba(20, 10, 5, 0.85);">
	<div class="w-[500px] rounded-lg border-4 p-5 font-sans overflow-hidden" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
		<!-- Heraldic Header -->
		<div class="mb-4 flex items-start gap-4 border-b pb-4" style="border-color: #8b6f47;">
			<div class="h-20 w-20 shrink-0 overflow-hidden rounded border-2 shadow-lg" style="border-color: #8b6f47; background: #1a1410;">
				<img src={settlement.banner} alt="City Banner" class="h-full w-full object-cover" />
			</div>
			<div class="flex-1">
				<div class="flex items-center justify-between">
					<h2 class="text-2xl font-black tracking-tight uppercase" style="color: #8b6f3a; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">{settlement.name}</h2>
					<button onclick={onClose} class="rounded border-2 px-2 py-1 text-[10px] font-bold uppercase tracking-tighter transition" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b8a0, #b8a890)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'}>Leave [Esc]</button>
				</div>
				<div class="mt-1 flex gap-3 text-[10px] font-bold uppercase tracking-widest" style="color: #7a6a5a;">
					<span>Pop: <span style="color: #5a4a3a;">{settlement.population}</span></span>
					<span>Econ: <span style="color: #6a7a8a;">{settlement.economy}</span></span>
				</div>
			</div>
		</div>

		<div class="mb-1 text-xs" style="color: #7a6a5a;">Pop: {settlement.population} | Econ: {settlement.economy}</div>

		<!-- Tabs -->
		<div class="mb-3 flex gap-1 border-b pb-2" style="border-color: #8b6f47;">
			<button
				onclick={() => { tab = 'info'; }}
				class="rounded px-3 py-1 text-sm transition"
				style="{tab === 'info' ? 'background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); color: #f0e8d8; border: 1px solid #5a6a7a;' : 'color: #7a6a5a; border: 1px solid transparent;'}"
				onmouseover={e => { if (tab !== 'info') e.currentTarget.style.color = '#5a4a3a'; }}
				onmouseout={e => { if (tab !== 'info') e.currentTarget.style.color = '#7a6a5a'; }}
			>Info</button>
			<button
				onclick={() => { tab = 'rest'; }}
				class="rounded px-3 py-1 text-sm transition"
				style="{tab === 'rest' ? 'background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); color: #f0e8d8; border: 1px solid #5a6a7a;' : 'color: #7a6a5a; border: 1px solid transparent;'}"
				onmouseover={e => { if (tab !== 'rest') e.currentTarget.style.color = '#5a4a3a'; }}
				onmouseout={e => { if (tab !== 'rest') e.currentTarget.style.color = '#7a6a5a'; }}
			>Rest</button>
			<button
				onclick={() => { tab = 'map'; }}
				class="rounded px-3 py-1 text-sm transition"
				style="{tab === 'map' ? 'background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); color: #f0e8d8; border: 1px solid #5a6a7a;' : 'color: #7a6a5a; border: 1px solid transparent;'}"
				onmouseover={e => { if (tab !== 'map') e.currentTarget.style.color = '#5a4a3a'; }}
				onmouseout={e => { if (tab !== 'map') e.currentTarget.style.color = '#7a6a5a'; }}
			>Map</button>
		</div>

		<!-- Info tab -->
		{#if tab === 'info'}
			<div class="space-y-2 text-sm" style="color: #5a4a3a;">
				<p>Welcome to <span style="color: #8b6f3a; font-weight: bold;">{settlement.name}</span>.</p>
				<p>This is a settlement with a population of {settlement.population} and a {settlement.economy} economy.</p>
				<p style="color: #7a6a5a;">You can trade goods or rest here to restore your vitals.</p>
				<div class="flex gap-2 pt-2">
					<button
						onclick={onEnter}
						class="rounded border-2 px-4 py-2 text-sm font-bold transition"
						style="background: linear-gradient(to bottom, #d4a574, #b8935a); border-color: #8b6f47; color: #3d2817;"
						onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e4b584, #c8a36a)'}
						onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d4a574, #b8935a)'}
					>Enter City</button>
					<button
						onclick={onTrade}
						class="rounded border-2 px-4 py-2 text-sm font-bold transition"
						style="background: linear-gradient(to bottom, #d4a574, #b8935a); border-color: #8b6f47; color: #3d2817;"
						onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e4b584, #c8a36a)'}
						onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d4a574, #b8935a)'}
					>Trade</button>
				</div>
			</div>
		{/if}

		<!-- Map tab -->
		{#if tab === 'map'}
			<div class="space-y-3 text-sm" style="color: #5a4a3a;">
				<div class="flex items-center justify-between">
					<span style="color: #7a6a5a;">City preview</span>
					<button
						onclick={() => {
							const seed = worldSeed + settlement.id * 123;
							const gen = new CityGenerator(seed, 128, 128, 'city');
							const data = gen.generate(settlement.population);
							mapUrl = data.visual.toDataURL();
						}}
						class="rounded border px-2 py-1 text-[10px] font-bold uppercase tracking-widest transition"
						style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;"
						onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b8a0, #b8a890)'}
						onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'}
					>Refresh</button>
				</div>
				<div class="overflow-hidden rounded border-2" style="border-color: #8b6f47; background: #1a1410;">
					{#if mapUrl}
						<img src={mapUrl} alt="City map preview" class="h-64 w-full object-cover" />
					{:else}
						<div class="flex h-64 items-center justify-center" style="color: #7a6a5a;">Generating map…</div>
					{/if}
				</div>
				<div class="text-xs" style="color: #7a6a5a;">Seed: {worldSeed + settlement.id * 123} · Population: {settlement.population}</div>
			</div>
		{/if}

		<!-- Rest tab -->
		{#if tab === 'rest'}
			<div class="space-y-3 text-sm">
				<div style="color: #5a4a3a;">
					<p>Rest at the inn to fully restore HP, MP, and SP.</p>
					<p class="mt-1" style="color: #7a6a5a;">Cost: 10 gold</p>
				</div>
				<div class="flex gap-3 text-xs">
					<span style="color: #8b3a3a;">HP: {player.combatStats.currentHp}/{player.combatStats.maxHp}</span>
					<span style="color: #3a5a8b;">MP: {player.combatStats.currentMp}/{player.combatStats.maxMp}</span>
					<span style="color: #8b6f3a;">SP: {Math.floor(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
				</div>
				<button
					onclick={rest}
					class="rounded border-2 px-4 py-2 text-sm font-bold transition"
					style="background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); border-color: #5a6a7a; color: #f0e8d8;"
					onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #9aaaba, #7a8a9a)'}
					onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #8a9aaa, #6a7a8a)'}
				>Rest (10g)</button>
			</div>
		{/if}

		<!-- Message -->
		{#if message}
			<div class="mt-3 rounded border px-3 py-2 text-center text-sm" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;">{message}</div>
		{/if}
	</div>
</div>
