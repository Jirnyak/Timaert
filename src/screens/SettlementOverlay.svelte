<script lang="ts">
	import type {PlayerState, Settlement} from '../game/state';
	import {calculateDerived} from '../game/attributes';

	type Props = {
		player: PlayerState;
		settlement: Settlement;
		onClose: () => void;
	};

	let {player, settlement, onClose}: Props = $props();

	let tab = $state<'trade' | 'rest' | 'info'>('info');
	let message = $state('');

	// Trade items
	const TRADE_ITEMS = [
		{name: 'Bread', buyPrice: 5, sellPrice: 2},
		{name: 'Potion', buyPrice: 25, sellPrice: 10},
		{name: 'Iron Sword', buyPrice: 80, sellPrice: 30},
		{name: 'Leather Armor', buyPrice: 60, sellPrice: 20},
		{name: 'Map Fragment', buyPrice: 40, sellPrice: 15},
	];

	let derived = $derived(calculateDerived(player.attributes));

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

<div class="absolute inset-0 flex items-center justify-center bg-black/70">
	<div class="w-[500px] rounded-lg border border-gray-700 bg-gray-900/95 p-5 font-sans shadow-2xl">
		<div class="mb-3 flex items-center justify-between">
			<h2 class="text-xl font-black text-yellow-400">{settlement.name}</h2>
			<button onclick={onClose} class="rounded bg-gray-700 px-3 py-1 text-sm text-gray-300 hover:bg-gray-600">Leave [Esc]</button>
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
		</div>

		<!-- Info tab -->
		{#if tab === 'info'}
			<div class="space-y-2 text-sm text-gray-300">
				<p>Welcome to <span class="text-yellow-400">{settlement.name}</span>.</p>
				<p>This is a settlement with a population of {settlement.population} and a {settlement.economy} economy.</p>
				<p class="text-gray-500">You can trade goods or rest here to restore your vitals.</p>
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
