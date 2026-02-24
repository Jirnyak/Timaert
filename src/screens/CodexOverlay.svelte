<script lang="ts">
	type Props = {
		onClose: () => void;
	};

	let {onClose}: Props = $props();

	type Article = { id: string; title: string; content: string };
	type Category = { id: string; title: string; articles: Article[] };

	const codexData: Category[] = [
		{
			id: 'lore',
			title: 'Lore & World',
			articles: [
				{
					id: 'cosmology',
					title: 'Cosmology',
					content: 'Torus world created by dead gods.\n\nTwo opposing forces:\n- Pure Magic: natural, impersonal, knowable energy.\n- Black Force: void/negation of people desires and dead gods whispers.\n\nWhen they meet → mutual annihilation. Black artifacts of dead gods exist and destabilize magic.\n\nCentral Prophecy: A "Black Child" will be born, marking the end of the Pure Magic era.'
				},
				{
					id: 'mage_rulers',
					title: 'Mage-Rulers',
					content: 'The Magocracy of the Remnants of Magika.\n\nArrogant and powerful mages — dukes, lords, archmages — rule the remnants of the Magika kingdoms. They exploit common people, considering them unworthy of true Pure Magic. In the kingdoms of Magika, magic is widespread and the primary measure of power. Even simple peasants possess basic spells. For elite mages, a peasant is a tool, a resource, expendable material.'
				},
				{
					id: 'empire_of_light',
					title: 'Empire of Light',
					content: 'Theocratic empire. Magic strictly forbidden (death penalty). Public religion; private corruption. Uses elite anti-mage warriors.\n\nGreat Eunuchs (Shadow Rulers): 13 secret rulers. Publicly religious leaders; secretly serve black cults. Manipulate prophecy to prepare world transition toward Black Child.'
				},
				{
					id: 'witches',
					title: 'The Witches',
					content: 'Immortal System Entities. Cannot be permanently killed (reincarnate). Represent metaphysical principles:\n\n- Nefesh (Life): Represents birth, transformation, cycle. Assigns arbitrary tasks.\n- Ain (Void): Represents entropy and dissolution. Appears near ruined areas.\n- Tiferet (Present): Represents "now". Focused on immediate action.\n- Hokma (Memory): Represents recorded past. Knows everything that was written or marked.'
				}
			]
		},
		{
			id: 'mechanics',
			title: 'RPG Mechanics',
			articles: [
				{
					id: 'attributes',
					title: 'Attributes',
					content: 'Nine primary attributes shape your character:\n\n- STR (Strength): Physical damage +1%, carry weight.\n- END (Endurance): HP regen +1%, Max HP.\n- AGI (Agility): Dodge, SP regen +1%.\n- WIL (Willpower): MP regen +1%, Max MP.\n- INT (Intelligence): Spell damage +1%, active spell slots.\n- WIS (Wisdom): EXP bonus +1%, learned spell slots.\n- LCK (Luck): Better loot, critical strike chance.\n- CHA (Charisma): Trade discount, relation bonus.\n- SPD (Speed): Movement speed, Max SP.'
				},
				{
					id: 'perks_skills',
					title: 'Skills & Perks',
					content: 'Skills: Provide flat base stat increases applied before attribute-based multipliers. They do not modify attributes directly. Examples include Bodybuilding (+1 base HP) and Swordsman.\n\nPerks: Powerful, build-defining choices that provide both significant advantages and disadvantages. Gained at level 1 and every 10 levels. Example: "Immortal" (Never die from old age, but 100% more EXP needed to level up).'
				}
			]
		},
		{
			id: 'economy',
			title: 'Economics',
			articles: [
				{
					id: 'market',
					title: 'Market System',
					content: 'No global market. All trade is local and emergent. Prices fluctuate based on Local Supply and Local Demand.\n\nDemand Factor increases when demand outpaces supply, raising the target price. Charisma reduces the commission when buying from NPCs.'
				},
				{
					id: 'settlements',
					title: 'Settlements & Caravans',
					content: 'Villages: Gather resources via peasant squads. Store inventory locally and sell to caravans/cities.\n\nCities: Buy resources, produce goods via production chains, spawn caravans for trade. Collect taxes based on population and trade volume.\n\nCaravans: Spawn at cities, load surplus goods, and travel using pathfinding to destinations where profit estimates are high.'
				}
			]
		}
	];

	let activeCategory = $state(codexData[0]);
	let activeArticle = $state(codexData[0].articles[0]);

	function selectCategory(cat: Category) {
		activeCategory = cat;
		activeArticle = cat.articles[0];
	}
</script>

<svelte:window onkeydown={e => { if (e.key === 'Escape') onClose(); }} />

<div class="absolute inset-0 z-[200] flex items-center justify-center" style="background: rgba(20, 10, 5, 0.9);">
	<div class="flex h-[80vh] w-[900px] overflow-hidden rounded-lg border-4 font-sans" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 24px rgba(0,0,0,0.8), inset 0 2px 0 rgba(255,255,255,0.3);">
		
		<!-- Sidebar -->
		<div class="flex w-64 flex-col border-r-2 p-4" style="border-color: #8b6f47; background: linear-gradient(to right, #c8b89f, #d4bf9f);">
			<h2 class="mb-4 text-2xl font-black uppercase tracking-widest text-center" style="color: #3d2817; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">Codex</h2>
			
			<div class="flex flex-col gap-4 overflow-y-auto" style="scrollbar-width: none;">
				{#each codexData as cat}
					<div class="flex flex-col gap-1">
						<button 
							onclick={() => selectCategory(cat)}
							class="text-left font-black uppercase tracking-wide text-sm px-2 py-1 rounded transition {activeCategory.id === cat.id ? 'bg-[#8b6f47] text-[#f0e8d8]' : 'text-[#5a4a3a] hover:bg-[#b8a88f]'}"
						>
							{cat.title}
						</button>
						
						{#if activeCategory.id === cat.id}
							<div class="ml-2 flex flex-col gap-0.5 border-l-2 pl-2" style="border-color: #8b6f47;">
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
					style="background: linear-gradient(to bottom, #a89880, #988870); border-color: #7a6a5a; color: #3d2817;" 
					onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'} 
					onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #a89880, #988870)'}
				>
					Close [Esc]
				</button>
			</div>
		</div>

		<!-- Main Content -->
		<div class="flex-1 overflow-y-auto p-8" style="background: linear-gradient(to bottom, #f4e8d4, #e8d4b8);">
			{#if activeArticle}
				<h1 class="mb-6 text-3xl font-black" style="color: #8b3a3a; text-shadow: 0 1px 1px rgba(0,0,0,0.2);">{activeArticle.title}</h1>
				<div class="whitespace-pre-wrap text-base leading-relaxed" style="color: #3d2817; font-family: 'Times New Roman', serif;">
					{activeArticle.content}
				</div>
			{/if}
		</div>
	</div>
</div>
