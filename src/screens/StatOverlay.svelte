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
		{key: 'str', label: 'STR', color: 'text-red-400', desc: 'Physical damage +1%. Raw martial power.'},
		{key: 'end', label: 'END', color: 'text-orange-400', desc: 'HP, HP regen +1%. The vessel of your life force.'},
		{key: 'agi', label: 'AGI', color: 'text-green-400', desc: 'Dodge, SP regen +1%. Grace and reaction in the physical realm.'},
		{key: 'wil', label: 'WIL', color: 'text-purple-400', desc: 'MP, MP regen +1%. Mental fortitude against the Void.'},
		{key: 'int', label: 'INT', color: 'text-blue-400', desc: 'Spell damage +1%. Your grasp on Pure Magic.'},
		{key: 'wis', label: 'WIS', color: 'text-cyan-400', desc: 'EXP bonus +1%. Memory and understanding of the world.'},
		{key: 'lck', label: 'LCK', color: 'text-yellow-400', desc: 'Crit, better loot. The unpredictable favor of dead gods.'},
		{key: 'cha', label: 'CHA', color: 'text-pink-400', desc: 'Trade discount. Influence over mortal minds.'},
		{key: 'spd', label: 'SPD', color: 'text-emerald-400', desc: 'Movement speed. Swiftness on the global map.'},
	] as const;

	const SKILL_NAMES = [
		{key: 'bodybuilding', label: 'Bodybuilding', desc: '+1 base HP per rank. Physical excellence unaffected by magic.'},
		{key: 'travel', label: 'Travel', desc: 'Reduced SP cost. Essential for navigating the harsh Torus world.'},
		{key: 'fighter', label: 'Fighter', desc: '+1% physical damage. The discipline of the blade and fist.'},
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

<div class="absolute inset-0 flex items-center justify-center" style="background: rgba(20, 10, 5, 0.85);">
	<div class="max-h-[90vh] w-[820px] overflow-y-auto rounded-lg border-4 p-5 font-sans" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
		<div class="mb-4 flex items-center justify-between">
			<h2 class="text-2xl font-black" style="color: #3d2817; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">Character Status</h2>
			<button onclick={onClose} class="rounded border-2 px-3 py-1 text-sm font-bold transition" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b8a0, #b8a890)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'}>Close [Esc]</button>
		</div>

		<div class="flex gap-4">
			<!-- Left side: Inventory Grid -->
			<div class="w-60 shrink-0">
				<h3 class="mb-2 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">Inventory Grid - Click to use</h3>
				<div class="grid grid-cols-6 gap-1">
					{#each Array(player.inventory.maxSlots) as _, idx}
						{@const item = player.inventory.items[idx]}
						<button
							class="flex h-9 w-9 items-center justify-center rounded border-2 text-base transition"
							style="{item ? 'border-color: #8b6f47; background: linear-gradient(to bottom, #c8b89f, #b8a88f); cursor: pointer;' : 'border-color: #9a8570; background: linear-gradient(to bottom, #a89880, #988870);'}"
							title={item ? `${item.name} x${item.quantity}\n${item.description}` : 'Empty'}
							onclick={() => { if (item) handleUseItem(item.id); }}
							onmouseover={e => { if (item) e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c8b89f)'; }}
							onmouseout={e => { if (item) e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b8a88f)'; }}
							disabled={!item}
						>
							{#if item}
								<span class="relative">
									{item.icon}
									{#if item.quantity > 1}
										<span class="absolute -right-2 -top-1 text-[9px]" style="color: #8b6f47;">{item.quantity}</span>
									{/if}
								</span>
							{/if}
						</button>
					{/each}
				</div>
				{#if useMessage}
					<div class="mt-2 rounded border px-2 py-1 text-xs" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;">{useMessage}</div>
				{/if}
			</div>

			<!-- Right side: Stats -->
			<div class="flex-1">
				<div class="grid grid-cols-3 gap-4">
					<!-- Column 1: Vitals + Level -->
					<div>
						<h3 class="mb-2 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">Vitals</h3>
						<div class="space-y-1 text-sm" style="color: #3d2817;">
							<div class="flex justify-between">
								<span style="color: #8b3a3a;">Health</span>
								<span style="color: #3d2817; font-weight: bold;">{player.combatStats.currentHp}/{player.combatStats.maxHp}</span>
							</div>
							<div class="flex justify-between">
								<span style="color: #3a5a8b;">MP</span>
								<span style="color: #3d2817; font-weight: bold;">{player.combatStats.currentMp}/{player.combatStats.maxMp}</span>
							</div>
							<div class="flex justify-between">
								<span style="color: #8b6f3a;">SP</span>
								<span style="color: #3d2817; font-weight: bold;">{Math.floor(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
							</div>
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">Level & Experience</h3>
						<div class="space-y-1 text-sm" style="color: #3d2817;">
							<div class="flex justify-between">
								<span style="color: #5a4a3a;">Level</span>
								<span style="color: #4a7c4a; font-weight: bold;">{player.levelData.level}</span>
							</div>
							<div class="flex justify-between">
								<span style="color: #5a4a3a;">EXP</span>
								<span style="color: #3d2817; font-weight: bold;">{player.levelData.exp}/{player.levelData.expToNext}</span>
							</div>
							{#if player.levelData.exp >= player.levelData.expToNext}
								<button onclick={doLevelUp} class="mt-1 w-full rounded border-2 px-2 py-1 text-xs font-bold transition" style="background: linear-gradient(to bottom, #d4a574, #b8935a); border-color: #8b6f47; color: #3d2817;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e4b584, #c8a36a)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d4a574, #b8935a)'}>Level Up!</button>
							{/if}
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">Resources</h3>
						<div class="space-y-1 text-sm" style="color: #3d2817;">
							<div class="flex justify-between">
								<span style="color: #8b6f3a;">Gold</span>
								<span style="color: #3d2817; font-weight: bold;">{player.gold}</span>
							</div>
							<div class="flex justify-between">
								<span style="color: #6a4a8b;">Perk Points</span>
								<span style="color: #3d2817; font-weight: bold;">{player.levelData.perkPoints}</span>
							</div>
							{#if player.levelData.perkPoints > 0}
								<button onclick={() => showPerkSelection = true} class="mt-1 w-full rounded border-2 px-2 py-1 text-xs font-bold transition" style="background: linear-gradient(to bottom, #9a7a9a, #7a5a7a); border-color: #5a3a5a; color: #f0e8d8;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #aa8aaa, #8a6a8a)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #9a7a9a, #7a5a7a)'}>Choose Perk</button>
							{/if}
						</div>
					</div>

					<!-- Column 2: Attributes -->
					<div>
						<h3 class="mb-2 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">
							Attributes
							{#if player.levelData.attributePoints > 0}
								<span class="ml-1" style="color: #8b6f3a;">({player.levelData.attributePoints} pts)</span>
							{/if}
						</h3>
						<div class="space-y-1">
							{#each ATTR_NAMES as attr}
								<div class="flex items-center justify-between text-sm">
									<span style="color: #5a3a2a;" title={attr.desc}>{attr.label}: {player.attributes[attr.key]}</span>
									{#if player.levelData.attributePoints > 0}
										<button
											onclick={() => increaseAttr(attr.key)}
											class="rounded border px-1.5 text-xs transition"
											style="background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); border-color: #4a5a6a; color: #f0e8d8;"
											onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #9aaaba, #7a8a9a)'}
											onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #8a9aaa, #6a7a8a)'}
										>+</button>
									{/if}
								</div>
							{/each}
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">
							Skills
							{#if player.levelData.skillPoints > 0}
								<span class="ml-1" style="color: #8b6f3a;">({player.levelData.skillPoints} pts)</span>
							{/if}
						</h3>
						<div class="space-y-1">
							{#each SKILL_NAMES as skill}
								<div class="flex items-center justify-between text-sm">
									<span style="color: #5a3a2a;" title={skill.desc}>{skill.label}: {player.skills[skill.key]}</span>
									{#if player.levelData.skillPoints > 0}
										<button
											onclick={() => increaseSkill(skill.key)}
											class="rounded border px-1.5 text-xs transition"
											style="background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); border-color: #4a5a6a; color: #f0e8d8;"
											onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #9aaaba, #7a8a9a)'}
											onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #8a9aaa, #6a7a8a)'}
										>+</button>
									{/if}
								</div>
							{/each}
						</div>
					</div>

					<!-- Column 3: Derived + Reputation -->
					<div>
						<h3 class="mb-2 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">Derived Bonuses</h3>
						<div class="space-y-0.5 text-xs" style="color: #3d2817;">
							<div class="flex justify-between cursor-help" title="Based on STR. Modifies all physical strikes."><span style="color: #5a4a3a;">Phys Dmg</span><span style="font-weight: bold;">x{derived.physDamageMult.toFixed(2)}</span></div>
							<div class="flex justify-between cursor-help" title="Based on INT. Amplifies the power of Pure Magic."><span style="color: #5a4a3a;">Spell Dmg</span><span style="font-weight: bold;">x{derived.spellDamageMult.toFixed(2)}</span></div>
							<div class="flex justify-between cursor-help" title="Based on END. How quickly your mortal vessel recovers."><span style="color: #5a4a3a;">HP Regen</span><span style="font-weight: bold;">x{derived.hpRegenMult.toFixed(2)}</span></div>
							<div class="flex justify-between cursor-help" title="Based on WIS. Determines your rate of learning."><span style="color: #5a4a3a;">EXP Bonus</span><span style="font-weight: bold;">x{derived.expMult.toFixed(2)}</span></div>
							<div class="flex justify-between cursor-help" title="Based on SPD. Reduces travel time across the global map."><span style="color: #5a4a3a;">Move Spd</span><span style="font-weight: bold;">x{derived.moveSpeedMult.toFixed(2)}</span></div>
							<div class="flex justify-between cursor-help" title="Based on CHA. Lowers prices when dealing with local merchants."><span style="color: #5a4a3a;">Trade</span><span style="font-weight: bold;">{(derived.tradeDiscount * 100).toFixed(0)}%</span></div>
							<div class="flex justify-between cursor-help" title="Based on AGI. Chance to evade enemy attacks in combat."><span style="color: #5a4a3a;">Dodge</span><span style="font-weight: bold;">{(derived.dodgeBase * 100).toFixed(0)}%</span></div>
							<div class="flex justify-between cursor-help" title="Based on LCK. Chance to strike a devastating blow."><span style="color: #5a4a3a;">Crit</span><span style="font-weight: bold;">{(derived.critBase * 100).toFixed(0)}%</span></div>
						</div>
						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">Reputation</h3>
						<div class="space-y-0.5 text-xs">
							{#each Object.entries(player.reputation) as [faction, value]}
								<div class="flex justify-between">
									<span style="color: #5a3a2a;">{faction}</span>
									<span style="color: {value >= 0 ? '#4a7c4a' : '#8b3a3a'}; font-weight: bold;">{value}</span>
								</div>
							{/each}
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style="border-color: #8b6f47; color: #5a4a3a;">Active Perks</h3>
						<div class="space-y-0.5 text-xs">
							{#if player.perks.size === 0}
								<div style="color: #7a6a5a;">No perks selected</div>
							{:else}
								{#each PERK_LIST as perk}
									{#if player.perks.has(perk.id)}
										<div class="rounded border p-1" style="background: linear-gradient(to bottom, #9a8a9a, #7a6a7a); border-color: #5a4a5a; color: #f0e8d8;" title={perk.description}>
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

		<div class="mt-3 text-center text-xs" style="color: #7a6a5a;">[ Press ESC/C to close ]</div>
	</div>
</div>

{#if showPerkSelection}
	<div class="fixed inset-0 z-50 flex items-center justify-center" style="background: rgba(20, 10, 5, 0.9);">
		<div class="max-h-[80vh] w-[600px] overflow-y-auto rounded-lg border-4 p-5" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
			<div class="mb-4 flex items-center justify-between">
				<h2 class="text-xl font-black" style="color: #5a3a5a; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">Choose a Perk</h2>
				<button onclick={() => showPerkSelection = false} class="rounded border-2 px-3 py-1 text-sm font-bold transition" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b8a0, #b8a890)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'}>Cancel</button>
			</div>

			<div class="space-y-2">
				{#each PERK_LIST as perk}
					<button
						onclick={() => selectPerk(perk.id)}
						disabled={player.perks.has(perk.id)}
						class="w-full rounded border-2 p-3 text-left transition"
						style="{player.perks.has(perk.id) ? 'background: linear-gradient(to bottom, #988870, #887860); border-color: #6b5847; color: #6a5a4a; cursor: not-allowed; opacity: 0.6;' : 'background: linear-gradient(to bottom, #c8b89f, #b8a88f); border-color: #8b6f47; color: #3d2817;'}"
						onmouseover={e => { if (!player.perks.has(perk.id)) e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c8b89f)'; }}
						onmouseout={e => { if (!player.perks.has(perk.id)) e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b8a88f)'; }}
					>
						<div class="font-bold" style="color: {player.perks.has(perk.id) ? '#6a5a4a' : '#5a3a5a'};">{perk.name}</div>
						<div class="mt-1 text-xs" style="color: {player.perks.has(perk.id) ? '#7a6a5a' : '#5a4a3a'};">{perk.description}</div>
						<div class="mt-2 flex gap-4 text-xs">
							<div><span style="color: #4a7c4a;">+</span> {perk.advantage}</div>
							<div><span style="color: #8b3a3a;">−</span> {perk.disadvantage}</div>
						</div>
					</button>
				{/each}
			</div>
		</div>
	</div>
{/if}
