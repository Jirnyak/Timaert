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

<div class="absolute inset-0 flex flex-col items-center justify-center" style="background: rgba(20, 10, 5, 0.9);">
	<div class="w-[720px] rounded-lg border-4 p-5 font-sans" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
		<h2 class="mb-4 text-center text-xl font-black" style="color: #8b6f3a; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">Trade</h2>

		{#if message}
			<div class="mb-3 rounded border px-3 py-2 text-center text-sm" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;">{message}</div>
		{/if}

		<div class="mb-2 text-center text-sm" style="color: #8b6f3a; font-weight: bold;">Your gold: {player.gold}</div>

		<div class="flex gap-4">
			<!-- Player inventory -->
			<div class="flex-1 rounded border-2 p-3" style="border-color: #8b6f47; background: linear-gradient(to bottom, #c8b89f, #b8a88f);">
				<h3 class="mb-2 text-sm font-bold" style="color: #3d2817;">Your Inventory</h3>
				<div class="grid grid-cols-8 gap-1">
					{#each Array(player.inventory.maxSlots) as _, idx}
						{@const item = player.inventory.items[idx]}
						<button
							class="flex h-10 w-full items-center justify-center rounded border-2 text-lg transition"
							style="{item ? 'border-color: #8b6f47; background: linear-gradient(to bottom, #d4bf9f, #c4af8f); cursor: pointer;' : 'border-color: #9a8570; background: linear-gradient(to bottom, #a89880, #988870);'}"
							title={item ? `${item.name} x${item.quantity} (${Math.max(1, Math.floor(item.value * 0.5))}g)` : 'Empty'}
							onclick={() => { if (item) sellItem(item); }}
							onmouseover={e => { if (item) e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)'; }}
							onmouseout={e => { if (item) e.currentTarget.style.background = 'linear-gradient(to bottom, #d4bf9f, #c4af8f)'; }}
							disabled={!item}
						>
							{#if item}
								<span class="relative">
									{item.icon}
									{#if item.quantity > 1}
										<span class="absolute -right-2 -top-1 text-[9px]" style="color: #8b6f3a;">{item.quantity}</span>
									{/if}
								</span>
							{/if}
						</button>
					{/each}
				</div>
			</div>

			<!-- Trader inventory -->
			<div class="flex-1 rounded border-2 p-3" style="border-color: #8b6f47; background: linear-gradient(to bottom, #c8b89f, #b8a88f);">
				<h3 class="mb-2 text-sm font-bold" style="color: #8b6f3a;">{traderName}'s Inventory</h3>
				<div class="grid grid-cols-8 gap-1">
					{#each Array(traderInventory.maxSlots) as _, idx}
						{@const item = traderInventory.items[idx]}
						<button
							class="flex h-10 w-full items-center justify-center rounded border-2 text-lg transition"
							style="{item ? 'border-color: #8b6f47; background: linear-gradient(to bottom, #d4bf9f, #c4af8f); cursor: pointer;' : 'border-color: #9a8570; background: linear-gradient(to bottom, #a89880, #988870);'}"
							title={item ? `${item.name} x${item.quantity} (${Math.max(1, Math.floor(item.value * (1 - derived.tradeDiscount)))}g)` : 'Empty'}
							onclick={() => { if (item) buyItem(item); }}
							onmouseover={e => { if (item) e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)'; }}
							onmouseout={e => { if (item) e.currentTarget.style.background = 'linear-gradient(to bottom, #d4bf9f, #c4af8f)'; }}
							disabled={!item}
						>
							{#if item}
								<span class="relative">
									{item.icon}
									{#if item.quantity > 1}
										<span class="absolute -right-2 -top-1 text-[9px]" style="color: #8b6f3a;">{item.quantity}</span>
									{/if}
								</span>
							{/if}
						</button>
					{/each}
				</div>
			</div>
		</div>

		<div class="mt-3 text-center text-xs" style="color: #7a6a5a;">Click your items to sell &middot; Click trader items to buy</div>

		<button
			onclick={onClose}
			class="mt-3 w-full rounded border-2 px-4 py-2 text-sm font-bold transition"
			style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;"
			onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b8a0, #b8a890)'}
			onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'}
		>Close [Esc]</button>
	</div>
</div>
