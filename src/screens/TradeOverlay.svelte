<script lang="ts">
	import type {PlayerState} from '../game/state';
	import type {Item, Inventory} from '../game/items';
	import {addItem, removeItem} from '../game/items';
	import {calculateDerived} from '../game/attributes';

	type Props = {
		player: PlayerState;
		traderName: string;
		traderInventory: Inventory;
		onClose: () => void;
	};

	let {player = $bindable(), traderName, traderInventory, onClose}: Props = $props();

	let message = $state('');
	let derived = $derived(calculateDerived(player.attributes));

	function buyItem(item: Item) {
		const discount = derived.tradeDiscount;
		const price = Math.max(1, Math.floor(item.value * (1 - discount)));
		if (player.gold < price) {
			message = 'Not enough gold!';
			return;
		}

		if (!removeItem(traderInventory, item.id, 1)) {
			message = 'Item unavailable.';
			return;
		}

		player.gold -= price;
		addItem(player.inventory, {...item, quantity: 1});
		player.items = player.inventory.items.reduce((s, i) => s + i.quantity, 0);
		message = `Bought ${item.name} for ${price}g`;
	}

	function sellItem(item: Item) {
		const sellPrice = Math.max(1, Math.floor(item.value * 0.5));
		if (!removeItem(player.inventory, item.id, 1)) {
			message = 'Cannot sell.';
			return;
		}

		player.gold += sellPrice;
		addItem(traderInventory, {...item, quantity: 1});
		player.items = player.inventory.items.reduce((s, i) => s + i.quantity, 0);
		message = `Sold ${item.name} for ${sellPrice}g`;
	}
</script>

<svelte:window onkeydown={e => { if (e.key === 'Escape') onClose(); }} />

<div class="absolute inset-0 flex flex-col items-center justify-center bg-black/90">
	<div class="w-[720px] rounded-lg border border-gray-700 bg-gray-900/95 p-5 font-sans shadow-2xl">
		<h2 class="mb-4 text-center text-xl font-black text-white">Trade</h2>

		{#if message}
			<div class="mb-3 rounded bg-gray-800 px-3 py-2 text-center text-sm text-cyan-300">{message}</div>
		{/if}

		<div class="mb-2 text-center text-sm text-yellow-400">Your gold: {player.gold}</div>

		<div class="flex gap-4">
			<!-- Player inventory -->
			<div class="flex-1 rounded border border-cyan-900/60 bg-gray-800/60 p-3">
				<h3 class="mb-2 text-sm font-bold text-cyan-300">Your Inventory</h3>
				<div class="grid grid-cols-6 gap-1">
					{#each Array(player.inventory.maxSlots) as _, idx}
						{@const item = player.inventory.items[idx]}
						<button
							class="flex h-10 w-full items-center justify-center rounded border text-lg
								{item ? 'border-cyan-800 bg-gray-700 hover:bg-gray-600 cursor-pointer' : 'border-gray-700 bg-gray-800/40'}"
							title={item ? `${item.name} x${item.quantity} (${Math.max(1, Math.floor(item.value * 0.5))}g)` : 'Empty'}
							onclick={() => { if (item) sellItem(item); }}
							disabled={!item}
						>
							{#if item}
								<span class="relative">
									{item.icon}
									{#if item.quantity > 1}
										<span class="absolute -right-2 -top-1 text-[9px] text-yellow-300">{item.quantity}</span>
									{/if}
								</span>
							{/if}
						</button>
					{/each}
				</div>
			</div>

			<!-- Trader inventory -->
			<div class="flex-1 rounded border border-amber-900/60 bg-gray-800/60 p-3">
				<h3 class="mb-2 text-sm font-bold text-amber-400">{traderName}'s Inventory</h3>
				<div class="grid grid-cols-6 gap-1">
					{#each Array(traderInventory.maxSlots) as _, idx}
						{@const item = traderInventory.items[idx]}
						<button
							class="flex h-10 w-full items-center justify-center rounded border text-lg
								{item ? 'border-amber-800 bg-gray-700 hover:bg-gray-600 cursor-pointer' : 'border-gray-700 bg-gray-800/40'}"
							title={item ? `${item.name} x${item.quantity} (${Math.max(1, Math.floor(item.value * (1 - derived.tradeDiscount)))}g)` : 'Empty'}
							onclick={() => { if (item) buyItem(item); }}
							disabled={!item}
						>
							{#if item}
								<span class="relative">
									{item.icon}
									{#if item.quantity > 1}
										<span class="absolute -right-2 -top-1 text-[9px] text-yellow-300">{item.quantity}</span>
									{/if}
								</span>
							{/if}
						</button>
					{/each}
				</div>
			</div>
		</div>

		<div class="mt-3 text-center text-xs text-gray-500">Click your items to sell &middot; Click trader items to buy</div>

		<button
			onclick={onClose}
			class="mt-3 w-full rounded bg-gray-700 px-4 py-2 text-sm font-bold text-white hover:bg-gray-600"
		>Close [Esc]</button>
	</div>
</div>
