<script lang="ts">
	import type {PlayerState} from '../game/state';
	import {calculateDerived, tryLevelUp, calculateCombatStats, PERK_LIST, type PerkID, addPerk} from '../game/attributes';
	import {useItem} from '../game/items';

	type Props = {
		player: PlayerState;
		onClose: () => void;
	};

	let {player = $bindable(), onClose}: Props = $props();

	let useMessage = $state('');
	let showPerkSelection = $state(false);

	const ATTR_NAMES = [
		{key: 'str', label: 'STR', color: 'text-red-400', desc: 'Physical damage +1%'},
		{key: 'end', label: 'END', color: 'text-orange-400', desc: 'HP, HP regen +1%'},
		{key: 'agi', label: 'AGI', color: 'text-green-400', desc: 'Dodge, SP regen +1%'},
		{key: 'wil', label: 'WIL', color: 'text-purple-400', desc: 'MP, MP regen +1%'},
		{key: 'int', label: 'INT', color: 'text-blue-400', desc: 'Spell damage +1%'},
		{key: 'wis', label: 'WIS', color: 'text-cyan-400', desc: 'EXP bonus +1%'},
		{key: 'lck', label: 'LCK', color: 'text-yellow-400', desc: 'Crit, better loot'},
		{key: 'cha', label: 'CHA', color: 'text-pink-400', desc: 'Trade discount'},
		{key: 'spd', label: 'SPD', color: 'text-emerald-400', desc: 'Movement speed'},
	] as const;

	const SKILL_NAMES = [
		{key: 'bodybuilding', label: 'Bodybuilding', desc: '+1 base HP per rank'},
		{key: 'travel', label: 'Travel', desc: 'Reduced SP cost'},
		{key: 'fighter', label: 'Fighter', desc: '+1% physical damage'},
	] as const;

	function increaseAttr(key: string) {
		if (player.levelData.attributePoints <= 0) {
			return;
		}

		const attrs = player.attributes as Record<string, number>;
		attrs[key] += 1;
		player.levelData.attributePoints -= 1;
		player.combatStats = calculateCombatStats(player.attributes, player.skills);
	}

	function increaseSkill(key: string) {
		if (player.levelData.skillPoints <= 0) {
			return;
		}

		const skills = player.skills as Record<string, number>;
		skills[key] += 1;
		player.levelData.skillPoints -= 1;
		player.combatStats = calculateCombatStats(player.attributes, player.skills);
	}

	function selectPerk(perkId: PerkID) {
		if (player.levelData.perkPoints <= 0) {
			return;
		}

		addPerk(player.perks, perkId);
		player.levelData.perkPoints -= 1;
		showPerkSelection = false;

		// Apply immediate perk effects
		if (perkId === 'talented') {
			tryLevelUp(player.levelData);
			player.combatStats = calculateCombatStats(player.attributes, player.skills);
		}
	}

	function doLevelUp() {
		if (tryLevelUp(player.levelData)) {
			player.combatStats = calculateCombatStats(player.attributes, player.skills);
		}
	}

	function handleUseItem(itemId: string) {
		const msg = useItem(player.inventory, itemId, player.combatStats);
		if (msg) {
			useMessage = msg;
			player.items = player.inventory.items.reduce((s, i) => s + i.quantity, 0);
		}
	}

	let derived = $derived(calculateDerived(player.attributes));
</script>

<svelte:window onkeydown={event => { if (event.key === 'Escape' || event.key === 'c') onClose(); }} />

<div class="absolute inset-0 flex items-center justify-center bg-black/70">
	<div class="max-h-[90vh] w-[820px] overflow-y-auto rounded-lg border border-gray-700 bg-gray-900/95 p-5 font-sans shadow-2xl">
		<div class="mb-4 flex items-center justify-between">
			<h2 class="text-2xl font-black text-white">Character Status</h2>
			<button onclick={onClose} class="rounded bg-gray-700 px-3 py-1 text-sm text-gray-300 hover:bg-gray-600">Close [Esc]</button>
		</div>

		<div class="flex gap-4">
			<!-- Left side: Inventory Grid -->
			<div class="w-60 shrink-0">
				<h3 class="mb-2 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">Inventory Grid - Click to use</h3>
				<div class="grid grid-cols-6 gap-1">
					{#each Array(player.inventory.maxSlots) as _, idx}
						{@const item = player.inventory.items[idx]}
						<button
							class="flex h-9 w-9 items-center justify-center rounded border text-base
								{item ? 'border-cyan-800 bg-gray-700 hover:bg-gray-600 cursor-pointer' : 'border-gray-700 bg-gray-800/40'}"
							title={item ? `${item.name} x${item.quantity}\n${item.description}` : 'Empty'}
							onclick={() => { if (item) handleUseItem(item.id); }}
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
				{#if useMessage}
					<div class="mt-2 rounded bg-gray-800 px-2 py-1 text-xs text-cyan-300">{useMessage}</div>
				{/if}
			</div>

			<!-- Right side: Stats -->
			<div class="flex-1">
				<div class="grid grid-cols-3 gap-4">
					<!-- Column 1: Vitals + Level -->
					<div>
						<h3 class="mb-2 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">Vitals</h3>
						<div class="space-y-1 text-sm">
							<div class="flex justify-between">
								<span class="text-red-400">Health</span>
								<span class="text-white">{player.combatStats.currentHp}/{player.combatStats.maxHp}</span>
							</div>
							<div class="flex justify-between">
								<span class="text-blue-400">MP</span>
								<span class="text-white">{player.combatStats.currentMp}/{player.combatStats.maxMp}</span>
							</div>
							<div class="flex justify-between">
								<span class="text-amber-300">SP</span>
								<span class="text-white">{Math.floor(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
							</div>
						</div>

						<h3 class="mb-2 mt-3 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">Level & Experience</h3>
						<div class="space-y-1 text-sm">
							<div class="flex justify-between">
								<span class="text-gray-300">Level</span>
								<span class="text-green-400">{player.levelData.level}</span>
							</div>
							<div class="flex justify-between">
								<span class="text-gray-300">EXP</span>
								<span class="text-white">{player.levelData.exp}/{player.levelData.expToNext}</span>
							</div>
							{#if player.levelData.exp >= player.levelData.expToNext}
								<button onclick={doLevelUp} class="mt-1 w-full rounded bg-yellow-700 px-2 py-1 text-xs font-bold text-white hover:bg-yellow-600">Level Up!</button>
							{/if}
						</div>

						<h3 class="mb-2 mt-3 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">Resources</h3>
						<div class="space-y-1 text-sm">
							<div class="flex justify-between">
								<span class="text-yellow-400">Gold</span>
								<span class="text-white">{player.gold}</span>
							</div>
							<div class="flex justify-between">
								<span class="text-purple-400">Perk Points</span>
								<span class="text-white">{player.levelData.perkPoints}</span>
							</div>
							{#if player.levelData.perkPoints > 0}
								<button onclick={() => showPerkSelection = true} class="mt-1 w-full rounded bg-purple-700 px-2 py-1 text-xs font-bold text-white hover:bg-purple-600">Choose Perk</button>
							{/if}
						</div>
					</div>

					<!-- Column 2: Attributes -->
					<div>
						<h3 class="mb-2 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">
							Attributes
							{#if player.levelData.attributePoints > 0}
								<span class="ml-1 text-yellow-400">({player.levelData.attributePoints} pts)</span>
							{/if}
						</h3>
						<div class="space-y-1">
							{#each ATTR_NAMES as attr}
								<div class="flex items-center justify-between text-sm">
									<span class={attr.color} title={attr.desc}>{attr.label}: {player.attributes[attr.key]}</span>
									{#if player.levelData.attributePoints > 0}
										<button
											onclick={() => increaseAttr(attr.key)}
											class="rounded bg-blue-800 px-1.5 text-xs text-blue-200 hover:bg-blue-700"
										>+</button>
									{/if}
								</div>
							{/each}
						</div>

						<h3 class="mb-2 mt-3 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">
							Skills
							{#if player.levelData.skillPoints > 0}
								<span class="ml-1 text-yellow-400">({player.levelData.skillPoints} pts)</span>
							{/if}
						</h3>
						<div class="space-y-1">
							{#each SKILL_NAMES as skill}
								<div class="flex items-center justify-between text-sm">
									<span class="text-gray-300" title={skill.desc}>{skill.label}: {player.skills[skill.key]}</span>
									{#if player.levelData.skillPoints > 0}
										<button
											onclick={() => increaseSkill(skill.key)}
											class="rounded bg-blue-800 px-1.5 text-xs text-blue-200 hover:bg-blue-700"
										>+</button>
									{/if}
								</div>
							{/each}
						</div>
					</div>

					<!-- Column 3: Derived + Reputation -->
					<div>
						<h3 class="mb-2 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">Derived Bonuses</h3>
						<div class="space-y-0.5 text-xs">
							<div class="flex justify-between"><span class="text-gray-400">Phys Dmg</span><span class="text-white">x{derived.physDamageMult.toFixed(2)}</span></div>
							<div class="flex justify-between"><span class="text-gray-400">Spell Dmg</span><span class="text-white">x{derived.spellDamageMult.toFixed(2)}</span></div>
							<div class="flex justify-between"><span class="text-gray-400">HP Regen</span><span class="text-white">x{derived.hpRegenMult.toFixed(2)}</span></div>
							<div class="flex justify-between"><span class="text-gray-400">EXP Bonus</span><span class="text-white">x{derived.expMult.toFixed(2)}</span></div>
							<div class="flex justify-between"><span class="text-gray-400">Move Spd</span><span class="text-white">x{derived.moveSpeedMult.toFixed(2)}</span></div>
							<div class="flex justify-between"><span class="text-gray-400">Trade</span><span class="text-white">{(derived.tradeDiscount * 100).toFixed(0)}%</span></div>
							<div class="flex justify-between"><span class="text-gray-400">Dodge</span><span class="text-white">{(derived.dodgeBase * 100).toFixed(0)}%</span></div>
							<div class="flex justify-between"><span class="text-gray-400">Crit</span><span class="text-white">{(derived.critBase * 100).toFixed(0)}%</span></div>
						</div>

						<h3 class="mb-2 mt-3 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">Reputation</h3>
						<div class="space-y-0.5 text-xs">
							{#each Object.entries(player.reputation) as [faction, value]}
								<div class="flex justify-between">
									<span class="text-gray-300">{faction}</span>
									<span class={value >= 0 ? 'text-green-400' : 'text-red-400'}>{value}</span>
								</div>
							{/each}
						</div>

						<h3 class="mb-2 mt-3 border-b border-gray-700 pb-1 text-sm font-bold text-gray-400">Active Perks</h3>
						<div class="space-y-0.5 text-xs">
							{#if player.perks.size === 0}
								<div class="text-gray-500">No perks selected</div>
							{:else}
								{#each PERK_LIST as perk}
									{#if player.perks.has(perk.id)}
										<div class="rounded bg-purple-900/30 p-1 text-purple-300" title={perk.description}>
											{perk.name}
										</div>
									{/if}
								{/each}
							{/if}
						</div>
					</div>
				</div>
			</div>
		</div>

		<div class="mt-3 text-center text-xs text-gray-500">[ Press ESC/C to close ]</div>
	</div>
</div>

{#if showPerkSelection}
	<div class="fixed inset-0 z-50 flex items-center justify-center bg-black/80">
		<div class="max-h-[80vh] w-[600px] overflow-y-auto rounded-lg border border-purple-700 bg-gray-900/95 p-5 shadow-2xl">
			<div class="mb-4 flex items-center justify-between">
				<h2 class="text-xl font-black text-purple-300">Choose a Perk</h2>
				<button onclick={() => showPerkSelection = false} class="rounded bg-gray-700 px-3 py-1 text-sm text-gray-300 hover:bg-gray-600">Cancel</button>
			</div>

			<div class="space-y-2">
				{#each PERK_LIST as perk}
					<button
						onclick={() => selectPerk(perk.id)}
						disabled={player.perks.has(perk.id)}
						class="w-full rounded border border-purple-800 bg-gray-800 p-3 text-left hover:bg-gray-700 disabled:cursor-not-allowed disabled:opacity-50"
					>
						<div class="font-bold text-purple-300">{perk.name}</div>
						<div class="mt-1 text-xs text-gray-400">{perk.description}</div>
						<div class="mt-2 flex gap-4 text-xs">
							<div><span class="text-green-400">+</span> {perk.advantage}</div>
							<div><span class="text-red-400">−</span> {perk.disadvantage}</div>
						</div>
					</button>
				{/each}
			</div>
		</div>
	</div>
{/if}
