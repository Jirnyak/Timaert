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
	const derived = $derived(calculateDerived(player.attributes, player.skills));

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
				<div class="max-h-[50vh] overflow-y-auto rounded border" style="border-color: {color.divider};">
					{#if player.inventory.items.length === 0}
						<div class="p-2 text-xs" style={mutedStyle}>Empty</div>
					{:else}
						{#each player.inventory.items as item (item.id)}
							<button
								class="flex w-full items-center justify-between gap-2 border-b px-2 py-1 text-left text-xs transition"
								style="border-color: {color.divider};"
								title={`${item.name}\n${item.description}\nWeight: ${item.weight} kg`}
								onclick={() => sellItem(item)}
								onmouseover={e => {
									e.currentTarget.style.background = color.cardBg;
								}}
								onmouseout={e => {
									e.currentTarget.style.background = 'transparent';
								}}
								onfocus={e => {
									e.currentTarget.style.background = color.cardBg;
								}}
								onblur={e => {
									e.currentTarget.style.background = 'transparent';
								}}
							>
								<span class="flex items-center gap-1.5 truncate" style="color: {color.heading};">
									<span class="text-base">{item.icon}</span>
									<span class="truncate">{item.name}</span>
									{#if item.quantity > 1}<span style="color: {color.muted};">×{item.quantity}</span>{/if}
								</span>
								<span class="flex shrink-0 items-center gap-2" style="color: {color.muted};">
									<span>{(item.weight * item.quantity).toFixed(2)} kg</span>
									<span style="color: {color.accent};">{calcSellPrice(item)}g</span>
								</span>
							</button>
						{/each}
					{/if}
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
				<div class="max-h-[50vh] overflow-y-auto rounded border" style="border-color: {color.divider};">
					{#if traderInventory.items.length === 0}
						<div class="p-2 text-xs" style={mutedStyle}>Out of stock</div>
					{:else}
						{#each traderInventory.items as item (item.id)}
							<button
								class="flex w-full items-center justify-between gap-2 border-b px-2 py-1 text-left text-xs transition"
								style="border-color: {color.divider};"
								title={`${item.name}\n${item.description}\nWeight: ${item.weight} kg`}
								onclick={() => buyItem(item)}
								onmouseover={e => {
									e.currentTarget.style.background = color.cardBg;
								}}
								onmouseout={e => {
									e.currentTarget.style.background = 'transparent';
								}}
								onfocus={e => {
									e.currentTarget.style.background = color.cardBg;
								}}
								onblur={e => {
									e.currentTarget.style.background = 'transparent';
								}}
							>
								<span class="flex items-center gap-1.5 truncate" style="color: {color.heading};">
									<span class="text-base">{item.icon}</span>
									<span class="truncate">{item.name}</span>
									{#if item.quantity > 1}<span style="color: {color.muted};">×{item.quantity}</span>{/if}
								</span>
								<span class="flex shrink-0 items-center gap-2" style="color: {color.muted};">
									<span>{item.weight} kg</span>
									<span style="color: {color.accent};">{calcBuyPrice(item)}g</span>
								</span>
							</button>
						{/each}
					{/if}
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
