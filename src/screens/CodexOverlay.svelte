<script lang="ts">
	import type {PlayerState} from '../game/state';
	import {
		color, panelStyle, dividerStyle, headingStyle, btnProps,
	} from '../ui/theme';

	type Props = {
		player: PlayerState;
		onClose: () => void;
	};

	const {player, onClose}: Props = $props();

	type Article = {id: string; title: string; content: string};
	type Category = {id: string; title: string; articles: Article[]};

	const codexData: Category[] = [
		{
			id: 'lore',
			title: 'Lore & World',
			articles: [
				{
					id: 'cosmology',
					title: 'Cosmology',
					content: 'Torus world created by dead gods.\n\nTwo opposing forces:\n- Pure Magic: natural, impersonal, knowable energy.\n- Black Force: void/negation of people desires and dead gods whispers.\n\nWhen they meet → mutual annihilation. Black artifacts of dead gods exist and destabilize magic.\n\nCentral Prophecy: A "Black Child" will be born, marking the end of the Pure Magic era.',
				},
				{
					id: 'mage_rulers',
					title: 'Mage-Rulers',
					content: 'The Magocracy of the Remnants of Magika.\n\nArrogant and powerful mages — dukes, lords, archmages — rule the remnants of the Magika kingdoms. They exploit common people, considering them unworthy of true Pure Magic. In the kingdoms of Magika, magic is widespread and the primary measure of power. Even simple peasants possess basic spells. For elite mages, a peasant is a tool, a resource, expendable material.',
				},
				{
					id: 'empire_of_light',
					title: 'Empire of Light',
					content: 'Theocratic empire. Magic strictly forbidden (death penalty). Public religion; private corruption. Uses elite anti-mage warriors.\n\nGreat Eunuchs (Shadow Rulers): 13 secret rulers. Publicly religious leaders; secretly serve black cults. Manipulate prophecy to prepare world transition toward Black Child.',
				},
				{
					id: 'witches',
					title: 'The Witches',
					content: 'Immortal System Entities. Cannot be permanently killed (reincarnate). Represent metaphysical principles:\n\n- Nefesh (Life): Represents birth, transformation, cycle. Assigns arbitrary tasks.\n- Ain (Void): Represents entropy and dissolution. Appears near ruined areas.\n- Tiferet (Present): Represents "now". Focused on immediate action.\n- Hokma (Memory): Represents recorded past. Knows everything that was written or marked.',
				},
			],
		},
		{
			id: 'mechanics',
			title: 'RPG Mechanics',
			articles: [
				{
					id: 'attributes',
					title: 'Attributes',
					content: 'Nine primary attributes shape your character:\n\n- STR (Strength): Physical damage +1%, carry weight.\n- END (Endurance): HP regen +1%, Max HP.\n- AGI (Agility): Dodge, SP regen +1%.\n- WIL (Willpower): MP regen +1%, Max MP.\n- INT (Intelligence): Spell damage +1%, active spell slots.\n- WIS (Wisdom): EXP bonus +1%, learned spell slots.\n- LCK (Luck): Better loot, critical strike chance.\n- CHA (Charisma): Trade discount, relation bonus.\n- SPD (Speed): Movement speed, Max SP.',
				},
				{
					id: 'perks_skills',
					title: 'Skills & Perks',
					content: 'Skills: Provide flat base stat increases applied before attribute-based multipliers. They do not modify attributes directly. Examples include Bodybuilding (+1 base HP) and Swordsman.\n\nPerks: Powerful, build-defining choices that provide both significant advantages and disadvantages. Gained at level 1 and every 10 levels. Example: "Immortal" (Never die from old age, but 100% more EXP needed to level up).',
				},
			],
		},
		{
			id: 'economy',
			title: 'Economics',
			articles: [
				{
					id: 'market',
					title: 'Market System',
					content: 'No global market. All trade is local and emergent. Prices fluctuate based on Local Supply and Local Demand.\n\nDemand Factor increases when demand outpaces supply, raising the target price. Charisma reduces the commission when buying from NPCs.',
				},
				{
					id: 'settlements',
					title: 'Settlements & Caravans',
					content: 'Villages: Gather resources via peasant squads. Store inventory locally and sell to caravans/cities.\n\nCities: Buy resources, produce goods via production chains, spawn caravans for trade. Collect taxes based on population and trade volume.\n\nCaravans: Spawn at cities, load surplus goods, and travel using pathfinding to destinations where profit estimates are high.',
				},
			],
		},
	];

	// Filter categories to only those containing at least one unlocked article
	const visibleCategories = $derived(codexData
		.map(cat => ({
			...cat,
			articles: cat.articles.filter(a => player.codexUnlocked.includes(a.id)),
		}))
		.filter(cat => cat.articles.length > 0));

	let activeCategory = $state(codexData[0]);
	let activeArticle = $state<Article | undefined>(undefined);

	// Select first available article when opening or switching categories
	$effect(() => {
		if (visibleCategories.length > 0 && !visibleCategories.some(c => c.id === activeCategory.id)) {
			activeCategory = visibleCategories[0];
		}

		// Refresh active article reference from the filtered list
		const currentCat = visibleCategories.find(c => c.id === activeCategory.id);
		if (currentCat && currentCat.articles.length > 0) {
			if (!activeArticle || !currentCat.articles.some(a => a.id === activeArticle!.id)) {
				activeArticle = currentCat.articles[0];
			}
		} else {
			activeArticle = undefined;
		}
	});

	function selectCategory(cat: Category) {
		activeCategory = cat;
		// ActiveArticle will be auto-selected by the effect
	}
</script>

<svelte:window onkeydown={e => {
	if (e.key === 'Escape') {
		onClose();
	}
}} />

<div class="absolute inset-0 z-200 flex items-center justify-center" style="background: {color.backdropMedium};">
	<div class="flex h-[80vh] w-[900px] overflow-hidden rounded-lg border-4 font-sans" style={panelStyle('large')}>

		<!-- Sidebar -->
		<div class="flex w-64 flex-col border-r-2 p-4" style="{dividerStyle} background: {color.sidebarBg};">
			<h2 class="mb-4 text-2xl font-black uppercase tracking-widest text-center" style={headingStyle}>Codex</h2>

			<div class="flex flex-col gap-4 overflow-y-auto" style="scrollbar-width: none;">
				{#each visibleCategories as cat}
					<div class="flex flex-col gap-1">
						<button
							onclick={() => selectCategory(cat)}
							class="text-left font-black uppercase tracking-wide text-sm px-2 py-1 rounded transition {activeCategory.id === cat.id ? 'bg-[#8b6f47] text-[#f0e8d8]' : 'text-[#5a4a3a] hover:bg-[#b8a88f]'}"
						>
							{cat.title}
						</button>

						{#if activeCategory.id === cat.id}
							<div class="ml-2 flex flex-col gap-0.5 border-l-2 pl-2" style={dividerStyle}>
								{#each cat.articles as article}
									<button
										onclick={() => activeArticle = article}
										class="text-left text-sm px-2 py-1 rounded transition {activeArticle.id === article.id ? 'font-bold text-[#3d2817] bg-[#d8c8af]' : 'text-[#6a5a4a] hover:text-[#3d2817]'}"
									>
										{article.title}
									</button>
								{/each}
							</div>
						{/if}
					</div>
				{/each}
			</div>

			<div class="mt-auto pt-4">
				<button
					onclick={onClose}
					class="w-full rounded border-2 px-4 py-2 text-sm font-bold transition"
					{...btnProps('muted')}
				>
					Close [Esc]
				</button>
			</div>
		</div>

		<!-- Main Content -->
		<div class="flex-1 overflow-y-auto p-8" style="background: {color.contentBg};">
			{#if activeArticle}
				<h1 class="mb-6 text-3xl font-black" style="color: {color.hp}; text-shadow: 0 1px 1px rgba(0,0,0,0.2);">{activeArticle.title}</h1>
				<div class="whitespace-pre-wrap text-base leading-relaxed" style="color: {color.heading}; font-family: 'Times New Roman', serif;">
					{activeArticle.content}
				</div>
			{:else}
				<div class="flex h-full items-center justify-center italic" style="color: {color.divider};">
					Select an article to read.
				</div>
			{/if}
		</div>
	</div>
</div>
