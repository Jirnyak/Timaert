<script lang="ts">
	import type {PlayerState, SettlementMood} from '../game/state';
	import {
		type Item, type Inventory, addItem, removeItem,
	} from '../game/items';
	import type {NPCTrait} from '../game/npc';
		import {calculateDerived} from '../game/attributes';
	import {
		color, panelStyle, accentHeadingStyle, messageStyle, mutedStyle, btnProps,
	} from '../ui/theme';

	type Props = {
		player: PlayerState;
		traderName: string;
		traderInventory: Inventory;
		traderTraits?: NPCTrait[];
		settlementMood?: SettlementMood;
		currentDay: number;
		onClose: () => void;
	};

	let {player = $bindable(), traderName, traderInventory, traderTraits = [], settlementMood, currentDay, onClose}: Props = $props();

	let message = $state('');
	const derived = $derived(calculateDerived(player.attributes));

	function getPriceModifiers(): {buyMult: number; sellMult: number} {
		let buyMult = 1; // Player buying from NPC (Base 100%)
		let sellMult = 0.5; // Player selling to NPC (Base 50%)

		// Mood modifiers
		if (settlementMood === 'Prosperous') {
			buyMult -= 0.1;
		}

		if (settlementMood === 'Unrest') {
			buyMult += 0.2;
		}

		if (settlementMood === 'Revolt') {
			buyMult += 0.4;
		}

		if (traderTraits.includes('Greedy')) {
			buyMult += 0.2; // NPC charges 20% more
			sellMult -= 0.1; // NPC pays 10% less
		}

		if (traderTraits.includes('Generous')) {
			buyMult -= 0.1; // NPC charges 10% less
			sellMult += 0.1; // NPC pays 10% more
		}

		// Player CHA discount applies to buying
		buyMult -= derived.tradeDiscount;

		return {buyMult: Math.max(0.1, buyMult), sellMult: Math.max(0.1, sellMult)};
	}

	function calcBuyPrice(item: Item): number {
		const {buyMult} = getPriceModifiers();
		return Math.max(1, Math.floor(item.value * buyMult));
	}

	function calcSellPrice(item: Item): number {
		const {sellMult} = getPriceModifiers();
		return Math.max(1, Math.floor(item.value * sellMult));
	}

	function buyItem(item: Item) {
		const price = calcBuyPrice(item);
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
		player.eventLog.push({type: 'economy', day: currentDay, message: `Bought ${item.name} from ${traderName} for ${price}g`});
	}

	function sellItem(item: Item) {
		const sellPrice = calcSellPrice(item);
		if (!removeItem(player.inventory, item.id, 1)) {
			message = 'Cannot sell.';
			return;
		}

		player.gold += sellPrice;
		addItem(traderInventory, {...item, quantity: 1});
		player.items = player.inventory.items.reduce((s, i) => s + i.quantity, 0);
		message = `Sold ${item.name} for ${sellPrice}g`;
		player.eventLog.push({type: 'economy', day: currentDay, message: `Sold ${item.name} to ${traderName} for ${sellPrice}g`});
	}
</script>

<svelte:window onkeydown={e => {
	if (e.key === 'Escape') {
		onClose();
	}
}} />

<div class="absolute inset-0 flex flex-col items-center justify-center" style="background: {color.backdropMedium};">
	<div class="w-[720px] rounded-lg border-4 p-5 font-sans" style={panelStyle()}>
		<h2 class="mb-4 text-center text-xl font-black" style={accentHeadingStyle}>Trade</h2>

		{#if message}
			<div class="mb-3 rounded border px-3 py-2 text-center text-sm" style={messageStyle}>{message}</div>
		{/if}

		<div class="mb-2 text-center text-sm cursor-help" style="color: {color.accent}; font-weight: bold;" title="The universal measure of value, demanded even by the most reclusive merchants.">Your gold: {player.gold}</div>

		<div class="flex gap-4">
			<!-- Player inventory -->
			<div class="flex-1 rounded border-2 p-3" style="border-color: {color.divider}; background: {color.innerPanelBg};">
				<h3 class="mb-2 text-sm font-bold" style="color: {color.heading};">Your Inventory (Sell)</h3>
				<div class="grid grid-cols-8 gap-1">
					{#each Array.from({length: player.inventory.maxSlots}) as _, idx}
						{@const item = player.inventory.items[idx]}
						<button
							class="flex h-10 w-full items-center justify-center rounded border-2 text-lg transition"
							style="{item ? `border-color: ${color.divider}; background: ${color.cardBg}; cursor: pointer;` : `border-color: ${color.emptySlotBorder}; background: ${color.emptySlotBg};`}"
							title={item ? `${item.name} x${item.quantity} (Sell: ${calcSellPrice(item)}g)` : 'Empty'}
							onclick={() => {
								if (item) {
									sellItem(item);
								}
							}}
							onmouseover={e => {
								if (item) {
									e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)';
								}
							}}
							onmouseout={e => {
								if (item) {
									e.currentTarget.style.background = color.cardBg;
								}
							}}
							onfocus={e => {
								if (item) {
									e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)';
								}
							}}
							onblur={e => {
								if (item) {
									e.currentTarget.style.background = color.cardBg;
								}
							}}
							disabled={!item}
						>
							{#if item}
								<span class="relative">
									{item.icon}
									{#if item.quantity > 1}
										<span class="absolute -right-2 -top-1 text-[9px]" style="color: {color.accent};">{item.quantity}</span>
									{/if}
								</span>
							{/if}
						</button>
					{/each}
				</div>
			</div>

			<!-- Trader inventory -->
			<div class="flex-1 rounded border-2 p-3" style="border-color: {color.divider}; background: {color.innerPanelBg};">
				<div class="mb-2 flex items-center justify-between">
					<h3 class="text-sm font-bold" style="color: {color.accent};">{traderName}'s Inventory</h3>
					<div class="flex gap-1">
						{#each traderTraits as trait}
							<span class="rounded bg-[{color.barTrack}]/10 border border-[{color.divider}]/40 px-1 py-0.5 text-[9px] font-bold uppercase tracking-wide cursor-help" style="color: {color.accent};" title="Affects prices">{trait}</span>
						{/each}
					</div>
				</div>
				<div class="grid grid-cols-8 gap-1">
					{#each Array.from({length: traderInventory.maxSlots}) as _, idx}
						{@const item = traderInventory.items[idx]}
						<button
							class="flex h-10 w-full items-center justify-center rounded border-2 text-lg transition"
							style="{item ? `border-color: ${color.divider}; background: ${color.cardBg}; cursor: pointer;` : `border-color: ${color.emptySlotBorder}; background: ${color.emptySlotBg};`}"
							title={item ? `${item.name} x${item.quantity} (Buy: ${calcBuyPrice(item)}g)` : 'Empty'}
							onclick={() => {
								if (item) {
									buyItem(item);
								}
							}}
							onmouseover={e => {
								if (item) {
									e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)';
								}
							}}
							onmouseout={e => {
								if (item) {
									e.currentTarget.style.background = color.cardBg;
								}
							}}
							onfocus={e => {
								if (item) {
									e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)';
								}
							}}
							onblur={e => {
								if (item) {
									e.currentTarget.style.background = color.cardBg;
								}
							}}
							disabled={!item}
						>
							{#if item}
								<span class="relative">
									{item.icon}
									{#if item.quantity > 1}
										<span class="absolute -right-2 -top-1 text-[9px]" style="color: {color.accent};">{item.quantity}</span>
									{/if}
								</span>
							{/if}
						</button>
					{/each}
				</div>
			</div>
		</div>

		<div class="mt-3 text-center text-xs" style={mutedStyle}>Click your items to sell &middot; Click trader items to buy</div>

		<button
			onclick={onClose}
			class="mt-3 w-full rounded border-2 px-4 py-2 text-sm font-bold transition"
			{...btnProps('close')}
		>Close [Esc]</button>
	</div>
</div>
