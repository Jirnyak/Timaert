<script lang="ts">
	import type {PlayerState} from '../game/state';
	import {
		type Spell, type SpellBook,
		SPELL_LIST, getSpell,
		canCast, setActiveSpell,
		spellDamage, spellHeal, spellRadius, spellDuration,
	} from '../game/spells';
		import {
			color, panelStyle, dividerStyle, headingStyle, btnProps,
		} from '../ui/theme';

	type Props = {
		player: PlayerState;
		spellBook: SpellBook;
		inMicro: boolean;
		onClose: () => void;
	};

	const {player, spellBook, inMicro, onClose}: Props = $props();

	// Only show learned spells
	const learnedSpells = $derived(SPELL_LIST.filter(s => spellBook.learned.includes(s.id)));

	let selectedSpell = $state<Spell | undefined>(getSpell(spellBook.activeSpellId) ?? learnedSpells[0]);

	// Rarity color mapping
	const rarityColor: Record<string, string> = {
		common: '#8b8b8b',
		uncommon: '#4a7c4a',
		rare: '#3a5a8b',
		epic: '#7a4a8b',
		mythic: '#b8935a',
	};

	const tierLabel = ['', 'I', 'II', 'III', 'IV', 'V'];

	function selectSpell(spell: Spell) {
		selectedSpell = spell;
	}

	function equipSpell(spell: Spell) {
		setActiveSpell(spellBook, spell.id);
	}

	function castCheck(spell: Spell): string {
		const result = canCast(spell, player.combatStats, spellBook, inMicro);
		return result.ok ? '' : result.reason;
	}
</script>

<svelte:window onkeydown={e => {
	if (e.key === 'Escape') {
		onClose();
	}
}} />

<div class="absolute inset-0 z-200 flex items-center justify-center" style="background: {color.backdropMedium};">
	<div class="flex h-[85vh] w-[960px] overflow-hidden rounded-lg border-4 font-sans" style={panelStyle('large')}>

		<!-- Left: Spell list (book index) -->
		<div class="flex w-56 flex-col border-r-2 p-4" style="{dividerStyle} background: {color.sidebarBg};">
			<h2 class="mb-4 text-center text-2xl font-black uppercase tracking-widest" style={headingStyle}>Spells</h2>

			<div class="flex flex-1 flex-col gap-1 overflow-y-auto" style="scrollbar-width: none;">
				{#each learnedSpells as spell}
					{@const isActive = spellBook.activeSpellId === spell.id}
					{@const isSelected = selectedSpell?.id === spell.id}
					<button
						onclick={() => selectSpell(spell)}
						class="flex items-center gap-2 rounded px-2 py-1.5 text-left text-sm transition"
						style="
							background: {isSelected ? color.innerPanelBg : 'transparent'};
							color: {isSelected ? color.heading : color.body};
							font-weight: {isSelected ? 'bold' : 'normal'};
						"
					>
						<span class="text-lg">{spell.icon}</span>
						<span class="flex-1 truncate">{spell.name}</span>
						{#if isActive}
							<span class="text-xs" style="color: {color.positive};" title="Active">●</span>
						{/if}
					</button>
				{/each}

				{#if learnedSpells.length === 0}
					<div class="px-2 py-4 text-center text-sm italic" style="color: {color.muted};">
						No spells learned yet.
					</div>
				{/if}
			</div>

			<!-- Mana display -->
			<div class="mt-4 border-t-2 pt-3" style={dividerStyle}>
				<div class="flex items-center justify-between text-sm">
					<span style="color: {color.mp};" class="font-bold">MP</span>
					<span style="color: {color.body};">{Math.floor(player.combatStats.currentMp)} / {player.combatStats.maxMp}</span>
				</div>
			</div>

			<button
				onclick={onClose}
				class="mt-3 w-full rounded border-2 px-4 py-2 text-sm font-bold transition"
				{...btnProps('muted')}
			>
				Close [Esc]
			</button>
		</div>

		<!-- Right: Spell detail (book page) -->
		<div class="flex flex-1 flex-col overflow-y-auto p-8" style="background: {color.contentBg};">
			{#if selectedSpell}
				{@const spell = selectedSpell}
				{@const dmg = spellDamage(spell, player.attributes, player.skills)}
				{@const heal = spellHeal(spell, player.attributes, player.skills)}
				{@const rad = spellRadius(spell, player.attributes, player.skills)}
				{@const dur = spellDuration(spell, player.attributes, player.skills)}
				{@const castErr = castCheck(spell)}
				{@const isEquipped = spellBook.activeSpellId === spell.id}

				<!-- Header -->
				<div class="mb-6 flex items-start gap-4">
					<div class="flex h-16 w-16 items-center justify-center rounded-lg border-2 text-4xl"
						style="border-color: {rarityColor[spell.rarity]}; background: {color.innerPanelBg};"
					>
						{spell.icon}
					</div>
					<div class="flex-1">
						<h1 class="text-3xl font-black" style="color: {color.heading}; text-shadow: {color.headingShadow};">
							{spell.name}
						</h1>
						<div class="mt-1 flex gap-3 text-sm">
							<span style="color: {rarityColor[spell.rarity]};" class="font-bold capitalize">{spell.rarity}</span>
							<span style="color: {color.muted};">Tier {tierLabel[spell.tier]}</span>
							<span style="color: {color.muted};">
								{spell.tags.map(t => t.charAt(0).toUpperCase() + t.slice(1)).join(', ')}
							</span>
						</div>
					</div>
				</div>

				<!-- Description -->
				<p class="mb-6 text-base leading-relaxed" style="color: {color.heading}; font-family: 'Times New Roman', serif;">
					{spell.description}
				</p>

				<!-- Stats grid -->
				<div class="mb-6 grid grid-cols-2 gap-x-8 gap-y-2 text-sm">
					{#if spell.sustained}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Type</span>
							<span style="color: {color.mp};" class="font-bold">Sustained</span>
						</div>
						<div class="flex justify-between">
							<span style="color: {color.muted};">Mana Drain</span>
							<span style="color: {color.mp};" class="font-bold">{spell.manaDrain}/s</span>
						</div>
					{:else}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Mana Cost</span>
							<span style="color: {color.mp};" class="font-bold">{spell.manaCost}</span>
						</div>
						<div class="flex justify-between">
							<span style="color: {color.muted};">Cooldown</span>
							<span style="color: {color.body};" class="font-bold">{spell.cooldown > 0 ? spell.cooldown + 's' : 'None'}</span>
						</div>
					{/if}
					<div class="flex justify-between">
						<span style="color: {color.muted};">Cast Time</span>
						<span style="color: {color.body};" class="font-bold">{spell.castTime > 0 ? spell.castTime + 's' : 'Instant'}</span>
					</div>
					{#if spell.micro?.shape}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Delivery</span>
							<span style="color: {color.body};" class="font-bold capitalize">{spell.micro.shape}</span>
						</div>
					{/if}

					{#if dmg > 0}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Damage</span>
							<span style="color: {color.hp};" class="font-bold">{dmg}</span>
						</div>
					{/if}
					{#if heal > 0}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Heal</span>
							<span style="color: {color.positive};" class="font-bold">{heal}</span>
						</div>
					{/if}
					{#if rad > 0}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Radius</span>
							<span style="color: {color.body};" class="font-bold">{rad}px</span>
						</div>
					{/if}
					{#if dur > 0}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Duration</span>
							<span style="color: {color.body};" class="font-bold">{dur.toFixed(1)}s</span>
						</div>
					{/if}
					{#if spell.micro && spell.micro.chainCount > 0}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Chain Targets</span>
							<span style="color: {color.body};" class="font-bold">{spell.micro.chainCount + 1}</span>
						</div>
					{/if}
					{#if spell.micro?.friendlyFire}
						<div class="flex justify-between">
							<span style="color: {color.muted};">Friendly Fire</span>
							<span style="color: {color.negative};" class="font-bold">Yes</span>
						</div>
					{/if}
				</div>

				<!-- Pros / Cons -->
				<div class="mb-6 grid grid-cols-2 gap-6">
					<div>
						<h3 class="mb-2 text-sm font-black uppercase" style="color: {color.positive};">Pros</h3>
						<ul class="list-none space-y-1 text-sm" style="color: {color.body};">
							{#each spell.pros as pro}
								<li>+ {pro}</li>
							{/each}
						</ul>
					</div>
					<div>
						<h3 class="mb-2 text-sm font-black uppercase" style="color: {color.negative};">Cons</h3>
						<ul class="list-none space-y-1 text-sm" style="color: {color.body};">
							{#each spell.cons as con}
								<li>− {con}</li>
							{/each}
						</ul>
					</div>
				</div>

				<!-- Layer availability -->
				<div class="mb-6 flex gap-4 text-sm">
					<span class="rounded px-2 py-1" style="background: {spell.micro ? color.positive : color.barTrack}; color: {color.light};">
						{spell.micro ? '✓ Combat' : '✗ Combat'}
					</span>
					<span class="rounded px-2 py-1" style="background: {spell.macro ? color.positive : color.barTrack}; color: {color.light};">
						{spell.macro ? '✓ World Map' : '✗ World Map'}
					</span>
				</div>

				<!-- Equip button -->
				<div class="mt-auto flex items-center gap-4">
					{#if isEquipped}
						<span class="rounded border-2 px-6 py-2 text-sm font-bold" style="border-color: {color.positive}; color: {color.positive};">
							● Active Spell
						</span>
					{:else}
						<button
							onclick={() => equipSpell(spell)}
							class="rounded border-2 px-6 py-2 text-sm font-bold transition"
							{...btnProps('primary')}
						>
							Set Active
						</button>
					{/if}

					{#if castErr}
						<span class="text-sm italic" style="color: {color.negative};">{castErr}</span>
					{/if}
				</div>

			{:else}
				<div class="flex h-full items-center justify-center italic" style="color: {color.muted};">
					{learnedSpells.length > 0 ? 'Select a spell to view details.' : 'Find spell books to learn new spells.'}
				</div>
			{/if}
		</div>
	</div>
</div>
