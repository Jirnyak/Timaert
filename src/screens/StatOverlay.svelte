<script lang="ts">
	import type {PlayerState} from '../game/state';
	import {
		calculateDerived, tryLevelUp, calculateCombatStats, PERK_LIST, type PerkID, addPerk,
	} from '../game/attributes';
	import {useItem} from '../game/items';
	import {
		totalUnits, UNIT_STATS, ALL_UNIT_TYPES, fireUnit, type UnitType, type ArmyComposition,
	} from '../game/army';
		import {
			color, panelStyle, headingStyle, sectionStyle, bodyStyle, mutedStyle, messageStyle, btnProps, btnStyle, btnHover, btnOut, slotStyle, slotHover, slotOut, backdropStyle, fmtStat,
		} from '../ui/theme';

	type Props = {
		player: PlayerState;
		deserterPool: ArmyComposition;
		onClose: () => void;
	};

	let {player = $bindable(), deserterPool, onClose}: Props = $props();

	let useMessage = $state('');
	let showPerkSelection = $state(false);

	const ATTR_NAMES = [
		{
			key: 'str', label: 'STR', color: 'text-red-400', desc: '+1 physical damage per point. Raw martial power.',
		},
		{
			key: 'vit', label: 'VIT', color: 'text-orange-400', desc: '+10 max HP per point. The vessel of your life force.',
		},
		{
			key: 'end', label: 'END', color: 'text-green-400', desc: '+10 max SP per point. Physical resilience and stamina.',
		},
		{
			key: 'wil', label: 'WIL', color: 'text-purple-400', desc: '+10 max MP per point. Mental fortitude against the Void.',
		},
		{
			key: 'int', label: 'INT', color: 'text-blue-400', desc: '+1 spell damage per point. Your grasp on Pure Magic.',
		},
		{
			key: 'wis', label: 'WIS', color: 'text-cyan-400', desc: 'EXP bonus +1%. Memory and understanding of the world.',
		},
		{
			key: 'lck', label: 'LCK', color: 'text-yellow-400', desc: 'Crit scaling, better loot. The unpredictable favor of dead gods.',
		},
		{
			key: 'cha', label: 'CHA', color: 'text-pink-400', desc: 'Trade discount, relations. Influence over mortal minds.',
		},
		{
			key: 'spd', label: 'SPD', color: 'text-emerald-400', desc: 'Movement speed (asymptotic). Swiftness on the global map.',
		},
	] as const;

	const SKILL_NAMES = [
		{key: 'bodybuilding', label: 'Bodybuilding', desc: '+5% max HP per rank. Physical excellence unaffected by magic.'},
		{key: 'meditation', label: 'Meditation', desc: '+5% max MP per rank. Deepens your mana reserves.'},
		{key: 'travel', label: 'Travel', desc: '+3% move speed per rank. Essential for navigating the harsh Torus world.'},
		{key: 'fighter', label: 'Fighter', desc: '+5% physical damage per rank. The discipline of the blade and fist.'},
		{key: 'endurance', label: 'Endurance', desc: '+5% max SP per rank. Prolonged exertion without fatigue.'},
		{key: 'spellcraft', label: 'Spellcraft', desc: '+5% spell damage per rank. Mastery of Pure Magic.'},
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
		const message = useItem(player.inventory, itemId, player.combatStats);
		if (message) {
			useMessage = message;
			player.items = player.inventory.items.reduce((s, i) => s + i.quantity, 0);
		}
	}

	const derived = $derived(calculateDerived(player.attributes, player.skills));
	const armyTotal = $derived(totalUnits(player.army));

	function doFire(ut: UnitType) {
		if (fireUnit(player.army, deserterPool, ut)) {
			useMessage = `Dismissed 1 ${UNIT_STATS[ut].label} — deserted into the wilds`;
		}
	}
</script>

<svelte:window onkeydown={event => {
	if (event.key === 'Escape' || event.key === 'c') {
		onClose();
	}
}} />

<div class="absolute inset-0 flex items-center justify-center" style="background: {color.backdrop};">
	<div class="max-h-[90vh] w-[820px] overflow-y-auto rounded-lg border-4 p-5 font-sans" style={panelStyle()}>
		<div class="mb-4 flex items-center justify-between">
			<h2 class="text-2xl font-black" style={headingStyle}>Character Status</h2>
			<button onclick={onClose} class="rounded border-2 px-3 py-1 text-sm font-bold transition" {...btnProps('close')}>Close [Esc]</button>
		</div>

		<div class="flex gap-4">
			<!-- Left side: Inventory Grid -->
			<div class="w-60 shrink-0">
				<h3 class="mb-2 border-b pb-1 text-sm font-bold" style={sectionStyle}>Inventory Grid - Click to use</h3>
				<div class="grid grid-cols-6 gap-1">
					{#each Array.from({length: player.inventory.maxSlots}) as _, idx}
						{@const item = player.inventory.items[idx]}
						<button
							class="flex h-9 w-9 items-center justify-center rounded border-2 text-base transition"
							style={slotStyle(Boolean(item))}
							title={item ? `${item.name} x${item.quantity}\n${item.description}` : 'Empty'}
							onclick={() => {
								if (item) {
									handleUseItem(item.id);
								}
							}}
							onmouseover={slotHover(Boolean(item))}
							onmouseout={slotOut(Boolean(item))} onfocus={slotHover(Boolean(item))}
						onblur={slotOut(Boolean(item))} disabled={!item}
						>
							{#if item}
								<span class="relative">
									{item.icon}
									{#if item.quantity > 1}
										<span class="absolute -right-2 -top-1 text-[9px]" style="color: {color.divider};">{item.quantity}</span>
									{/if}
								</span>
							{/if}
						</button>
					{/each}
				</div>
				{#if useMessage}
					<div class="mt-2 rounded border px-2 py-1 text-xs" style={messageStyle}>{useMessage}</div>
				{/if}
			</div>

			<!-- Right side: Stats -->
			<div class="flex-1">
				<div class="grid grid-cols-3 gap-4">
					<!-- Column 1: Vitals + Level -->
					<div>
						<h3 class="mb-2 border-b pb-1 text-sm font-bold" style={sectionStyle}>Vitals</h3>
						<div class="space-y-1 text-sm" style="color: {color.heading};">
							<div class="flex justify-between">
								<span style="color: {color.hp};">Health</span>
								<span style="color: {color.heading}; font-weight: bold;">{fmtStat(player.combatStats.currentHp)}/{player.combatStats.maxHp}</span>
							</div>
							<div class="flex justify-between">
								<span style="color: {color.mp};">MP</span>
								<span style="color: {color.heading}; font-weight: bold;">{fmtStat(player.combatStats.currentMp)}/{player.combatStats.maxMp}</span>
							</div>
							<div class="flex justify-between">
								<span style="color: {color.sp};">SP</span>
								<span style="color: {color.heading}; font-weight: bold;">{fmtStat(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
							</div>
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style={sectionStyle}>Level & Experience</h3>
						<div class="space-y-1 text-sm" style="color: {color.heading};">
							<div class="flex justify-between">
								<span style={bodyStyle}>Level</span>
								<span style="color: {color.positive}; font-weight: bold;">{player.levelData.level}</span>
							</div>
							<div class="flex justify-between">
								<span style={bodyStyle}>EXP</span>
								<span style="color: {color.heading}; font-weight: bold;">{player.levelData.exp}/{player.levelData.expToNext}</span>
							</div>
							{#if player.levelData.exp >= player.levelData.expToNext}
								<button onclick={doLevelUp} class="mt-1 w-full rounded border-2 px-2 py-1 text-xs font-bold transition" {...btnProps('primary')}>Level Up!</button>
							{/if}
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style={sectionStyle}>Resources</h3>
						<div class="space-y-1 text-sm" style="color: {color.heading};">
							<div class="flex justify-between">
								<span style="color: {color.accent};">Gold</span>
								<span style="color: {color.heading}; font-weight: bold;">{player.gold}</span>
							</div>
							<div class="flex justify-between">
								<span style="color: #6a4a8b;">Perk Points</span>
								<span style="color: {color.heading}; font-weight: bold;">{player.levelData.perkPoints}</span>
							</div>
							{#if player.levelData.perkPoints > 0}
								<button onclick={() => showPerkSelection = true} class="mt-1 w-full rounded border-2 px-2 py-1 text-xs font-bold transition" {...btnProps('perk')}>Choose Perk</button>
							{/if}
						</div>
					</div>

					<!-- Column 2: Attributes -->
					<div>
						<h3 class="mb-2 border-b pb-1 text-sm font-bold" style={sectionStyle}>
							Attributes
							{#if player.levelData.attributePoints > 0}
								<span class="ml-1" style="color: {color.accent};">({player.levelData.attributePoints} pts)</span>
							{/if}
						</h3>
						<div class="space-y-1">
							{#each ATTR_NAMES as attr}
								<div class="flex items-center justify-between text-sm">
									<span style="color: {color.label};" title={attr.desc}>{attr.label}: {player.attributes[attr.key]}</span>
									{#if player.levelData.attributePoints > 0}
										<button
											onclick={() => increaseAttr(attr.key)}
											class="rounded border px-1.5 text-xs transition"
											{...btnProps('action')}
										>+</button>
									{/if}
								</div>
							{/each}
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style={sectionStyle}>
							Skills
							{#if player.levelData.skillPoints > 0}
								<span class="ml-1" style="color: {color.accent};">({player.levelData.skillPoints} pts)</span>
							{/if}
						</h3>
						<div class="space-y-1">
							{#each SKILL_NAMES as skill}
								<div class="flex items-center justify-between text-sm">
									<span style="color: {color.label};" title={skill.desc}>{skill.label}: {player.skills[skill.key]}</span>
									{#if player.levelData.skillPoints > 0}
										<button
											onclick={() => increaseSkill(skill.key)}
											class="rounded border px-1.5 text-xs transition"
											{...btnProps('action')}
										>+</button>
									{/if}
								</div>
							{/each}
						</div>
					</div>

					<!-- Column 3: Derived + Reputation -->
					<div>
						<h3 class="mb-2 border-b pb-1 text-sm font-bold" style={sectionStyle}>Derived Bonuses</h3>
						<div class="space-y-0.5 text-xs" style="color: {color.heading};">
							<div class="flex justify-between cursor-help" title="STR + Fighter skill. Flat bonus to physical strikes."><span style={bodyStyle}>Phys Dmg</span><span style="font-weight: bold;">+{derived.rawPhysDamage.toFixed(0)}</span></div>
							<div class="flex justify-between cursor-help" title="INT + Spellcraft skill. Flat bonus to spell power."><span style={bodyStyle}>Spell Dmg</span><span style="font-weight: bold;">+{derived.rawSpellDamage.toFixed(0)}</span></div>
							<div class="flex justify-between cursor-help" title="Based on WIS. Determines your rate of learning."><span style={bodyStyle}>EXP Bonus</span><span style="font-weight: bold;">x{derived.expMult.toFixed(2)}</span></div>
							<div class="flex justify-between cursor-help" title="Based on SPD + Travel skill. Reduces travel time across the global map."><span style={bodyStyle}>Move Spd</span><span style="font-weight: bold;">x{derived.moveSpeedMult.toFixed(2)}</span></div>
							<div class="flex justify-between cursor-help" title="Based on CHA. Lowers prices when dealing with local merchants."><span style={bodyStyle}>Trade</span><span style="font-weight: bold;">{(derived.tradeDiscount * 100).toFixed(0)}%</span></div>
							<div class="flex justify-between cursor-help" title="Based on LCK. Chance to strike a devastating blow."><span style={bodyStyle}>Crit</span><span style="font-weight: bold;">{(derived.critBase * 100).toFixed(0)}%</span></div>
						</div>
						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style={sectionStyle}>Reputation</h3>
						<div class="space-y-0.5 text-xs">
							{#each Object.entries(player.reputation) as [faction, value]}
								<div class="flex justify-between">
									<span style="color: {color.label};">{faction}</span>
									<span style="color: {value >= 0 ? color.positive : color.hp}; font-weight: bold;">{value}</span>
								</div>
							{/each}
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style={sectionStyle}>Army ({armyTotal})</h3>
						<div class="space-y-0.5 text-xs">
							{#if armyTotal === 0}
								<div style={mutedStyle}>No troops recruited</div>
							{:else}
								{#each ALL_UNIT_TYPES as ut (ut)}
									{@const count = player.army[ut as UnitType] ?? 0}
									{#if count > 0}
										<div class="flex items-center justify-between">
											<span style="color: {color.label};">{UNIT_STATS[ut as UnitType].label}</span>
											<span class="flex items-center gap-1">
												<span style="font-weight: bold; color: {color.heading};">{count}</span>
												<button onclick={() => doFire(ut as UnitType)} class="rounded border px-1 text-[9px] transition" {...btnProps('close')} title="Dismiss unit (becomes deserter)">×</button>
											</span>
										</div>
									{/if}
								{/each}
							{/if}
						</div>

						<h3 class="mb-2 mt-3 border-b pb-1 text-sm font-bold" style={sectionStyle}>Active Perks</h3>
						<div class="space-y-0.5 text-xs">
							{#if player.perks.size === 0}
								<div style={mutedStyle}>No perks selected</div>
							{:else}
								{#each PERK_LIST as perk}
									{#if player.perks.has(perk.id)}
										<div class="rounded border p-1" style="background: linear-gradient(to bottom, #9a8a9a, #7a6a7a); border-color: #5a4a5a; color: {color.light};" title={perk.description}>
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

		<div class="mt-3 text-center text-xs" style={mutedStyle}>[ Press ESC/C to close ]</div>
	</div>
</div>

{#if showPerkSelection}
	<div class="fixed inset-0 z-50 flex items-center justify-center" style={backdropStyle('medium')}>
		<div class="max-h-[80vh] w-[600px] overflow-y-auto rounded-lg border-4 p-5" style={panelStyle()}>
			<div class="mb-4 flex items-center justify-between">
				<h2 class="text-xl font-black" style="color: #5a3a5a; text-shadow: {color.headingShadow};">Choose a Perk</h2>
				<button onclick={() => showPerkSelection = false} class="rounded border-2 px-3 py-1 text-sm font-bold transition" {...btnProps('close')}>Cancel</button>
			</div>

			<div class="space-y-2">
				{#each PERK_LIST as perk}
					{@const owned = player.perks.has(perk.id)}
					<button
						onclick={() => selectPerk(perk.id)}
						disabled={owned}
						class="w-full rounded border-2 p-3 text-left transition"
						style="{owned ? `background: linear-gradient(to bottom, #988870, #887860); border-color: #6b5847; color: ${color.muted}; cursor: not-allowed; opacity: 0.6;` : btnStyle('secondary')}"
						onmouseover={e => {
							if (!owned) {
								btnHover('secondary')(e);
							}
						}}
						onmouseout={e => {
							if (!owned) {
								btnOut('secondary')(e);
							}
						}}
						onfocus={e => {
							if (!owned) {
								btnHover('secondary')(e);
							}
						}}
						onblur={e => {
							if (!owned) {
								btnOut('secondary')(e);
							}
						}}
					>
						<div class="font-bold" style="color: {owned ? color.muted : '#5a3a5a'};">{perk.name}</div>
						<div class="mt-1 text-xs" style="color: {owned ? color.muted : color.body};">{perk.description}</div>
						<div class="mt-2 flex gap-4 text-xs">
							<div><span style="color: {color.positive};">+</span> {perk.advantage}</div>
							<div><span style="color: {color.hp};">−</span> {perk.disadvantage}</div>
						</div>
					</button>
				{/each}
			</div>
		</div>
	</div>
{/if}
