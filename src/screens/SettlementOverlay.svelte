<script lang="ts">
	import type {PlayerState, Settlement} from '../game/state';
	import {calculateDerived} from '../game/attributes';
	import {CityGenerator} from '../game/city-generator';

	type Props = {
		player: PlayerState;
		settlement: Settlement;
		worldSeed: number;
		onClose: () => void;
		onEnter: () => void;
	};

	let {player, settlement, worldSeed, onClose, onEnter}: Props = $props();

	let tab = $state<'trade' | 'rest' | 'info' | 'map'>('info');
	let message = $state('');
	let mapUrl = $state('');

	// Trade items
	const TRADE_ITEMS = [
		{name: 'Bread', buyPrice: 5, sellPrice: 2},
		{name: 'Potion', buyPrice: 25, sellPrice: 10},
		{name: 'Iron Sword', buyPrice: 80, sellPrice: 30},
		{name: 'Leather Armor', buyPrice: 60, sellPrice: 20},
		{name: 'Map Fragment', buyPrice: 40, sellPrice: 15},
	];

	let derived = $derived(calculateDerived(player.attributes));

	$effect(() => {
		const seed = worldSeed + settlement.id * 123;
		const gen = new CityGenerator(seed, 128, 128, 'city');
		const data = gen.generate(settlement.population);
		mapUrl = data.visual.toDataURL();
	});

	function buyItem(item: typeof TRADE_ITEMS[number]) {
		const discounted = Math.max(1, Math.floor(item.buyPrice * (1 - derived.tradeDiscount)));
		if (player.gold < discounted) {
			message = 'Not enough gold!';
			return;
		}

		player.gold -= discounted;
		player.items += 1;
		message = `Bought ${item.name} for ${discounted}g`;
	}

	function sellItem(item: typeof TRADE_ITEMS[number]) {
		if (player.items <= 0) {
			message = 'No items to sell!';
			return;
		}

		const boosted = Math.floor(item.sellPrice * (1 + derived.tradeDiscount * 0.5));
		player.gold += boosted;
		player.items -= 1;
		message = `Sold item for ${boosted}g`;
	}

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

<div class="absolute inset-0 flex items-center justify-center bg-black/70 z-[100]">
	<div class="w-[500px] rounded-lg border-2 border-amber-900/40 bg-gray-900/95 p-5 font-sans shadow-2xl overflow-hidden">
		<!-- Heraldic Header -->
		<div class="mb-4 flex items-start gap-4 border-b border-gray-800 pb-4">
			<div class="h-20 w-20 shrink-0 overflow-hidden rounded border-2 border-gray-700 bg-black shadow-lg">
				<img src={settlement.banner} alt="City Banner" class="h-full w-full object-cover" />
			</div>
			<div class="flex-1">
				<div class="flex items-center justify-between">
					<h2 class="text-2xl font-black tracking-tight text-yellow-400 uppercase">{settlement.name}</h2>
					<button onclick={onClose} class="rounded bg-gray-800 px-2 py-1 text-[10px] font-bold uppercase tracking-tighter text-gray-400 hover:bg-gray-700 hover:text-white transition-colors">Leave [Esc]</button>
				</div>
				<div class="mt-1 flex gap-3 text-[10px] font-bold uppercase tracking-widest text-gray-500">
					<span>Pop: <span class="text-gray-300">{settlement.population}</span></span>
					<span>Econ: <span class="text-cyan-600">{settlement.economy}</span></span>
				</div>
			</div>
		</div>

		<div class="mb-1 text-xs text-gray-500">Pop: {settlement.population} | Econ: {settlement.economy}</div>

		<!-- Tabs -->
		<div class="mb-3 flex gap-1 border-b border-gray-700 pb-2">
			<button
				onclick={() => { tab = 'info'; }}
				class="rounded px-3 py-1 text-sm {tab === 'info' ? 'bg-cyan-700 text-white' : 'text-gray-400 hover:text-white'}"
			>Info</button>
			<button
				onclick={() => { tab = 'trade'; }}
				class="rounded px-3 py-1 text-sm {tab === 'trade' ? 'bg-cyan-700 text-white' : 'text-gray-400 hover:text-white'}"
			>Trade</button>
			<button
				onclick={() => { tab = 'rest'; }}
				class="rounded px-3 py-1 text-sm {tab === 'rest' ? 'bg-cyan-700 text-white' : 'text-gray-400 hover:text-white'}"
			>Rest</button>
			<button
				onclick={() => { tab = 'map'; }}
				class="rounded px-3 py-1 text-sm {tab === 'map' ? 'bg-cyan-700 text-white' : 'text-gray-400 hover:text-white'}"
			>Map</button>
		</div>

		<!-- Info tab -->
		{#if tab === 'info'}
			<div class="space-y-2 text-sm text-gray-300">
				<p>Welcome to <span class="text-yellow-400">{settlement.name}</span>.</p>
				<p>This is a settlement with a population of {settlement.population} and a {settlement.economy} economy.</p>
				<p class="text-gray-500">You can trade goods or rest here to restore your vitals.</p>
				<div class="pt-2">
					<button
						onclick={onEnter}
						class="rounded bg-yellow-900/90 px-4 py-2 text-sm font-bold text-yellow-200 transition hover:bg-yellow-800 hover:text-white"
					>Enter City</button>
				</div>
			</div>
		{/if}

		<!-- Map tab -->
		{#if tab === 'map'}
			<div class="space-y-3 text-sm text-gray-300">
				<div class="flex items-center justify-between">
					<span class="text-gray-400">City preview</span>
					<button
						onclick={() => {
							const seed = worldSeed + settlement.id * 123;
							const gen = new CityGenerator(seed, 128, 128, 'city');
							const data = gen.generate(settlement.population);
							mapUrl = data.visual.toDataURL();
						}}
						class="rounded bg-gray-800 px-2 py-1 text-[10px] font-bold uppercase tracking-widest text-gray-300 hover:bg-gray-700"
					>Refresh</button>
				</div>
				<div class="overflow-hidden rounded border border-gray-800 bg-black/40">
					{#if mapUrl}
						<img src={mapUrl} alt="City map preview" class="h-64 w-full object-cover" />
					{:else}
						<div class="flex h-64 items-center justify-center text-gray-500">Generating map…</div>
					{/if}
				</div>
				<div class="text-xs text-gray-500">Seed: {worldSeed + settlement.id * 123} · Population: {settlement.population}</div>
			</div>
		{/if}

		<!-- Trade tab -->
		{#if tab === 'trade'}
			<div class="mb-2 flex justify-between text-sm">
				<span class="text-yellow-400">Gold: {player.gold}</span>
				<span class="text-gray-400">Items: {player.items}</span>
				<span class="text-cyan-400">Discount: {(derived.tradeDiscount * 100).toFixed(0)}%</span>
			</div>
			<div class="space-y-1">
				{#each TRADE_ITEMS as item}
					<div class="flex items-center justify-between rounded bg-gray-800 px-3 py-1.5 text-sm">
						<span class="text-white">{item.name}</span>
						<div class="flex gap-2">
							<button
								onclick={() => buyItem(item)}
								class="rounded bg-green-800 px-2 py-0.5 text-xs text-green-300 hover:bg-green-700"
							>Buy {Math.max(1, Math.floor(item.buyPrice * (1 - derived.tradeDiscount)))}g</button>
							<button
								onclick={() => sellItem(item)}
								class="rounded bg-orange-800 px-2 py-0.5 text-xs text-orange-300 hover:bg-orange-700"
							>Sell {Math.floor(item.sellPrice * (1 + derived.tradeDiscount * 0.5))}g</button>
						</div>
					</div>
				{/each}
			</div>
		{/if}

		<!-- Rest tab -->
		{#if tab === 'rest'}
			<div class="space-y-3 text-sm">
				<div class="text-gray-300">
					<p>Rest at the inn to fully restore HP, MP, and SP.</p>
					<p class="mt-1 text-gray-500">Cost: 10 gold</p>
				</div>
				<div class="flex gap-3 text-xs">
					<span class="text-red-400">HP: {player.combatStats.currentHp}/{player.combatStats.maxHp}</span>
					<span class="text-blue-400">MP: {player.combatStats.currentMp}/{player.combatStats.maxMp}</span>
					<span class="text-amber-300">SP: {Math.floor(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
				</div>
				<button
					onclick={rest}
					class="rounded bg-cyan-800 px-4 py-2 text-sm font-bold text-white hover:bg-cyan-700"
				>Rest (10g)</button>
			</div>
		{/if}

		<!-- Message -->
		{#if message}
			<div class="mt-3 rounded bg-gray-800 px-3 py-2 text-center text-sm text-cyan-300">{message}</div>
		{/if}
	</div>
</div>
