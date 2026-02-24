<script lang="ts">
	import type {PlayerState} from '../game/state';
	import type {NPC} from '../game/npc';
	import {NPCType} from '../game/npc';

	type Props = {
		player: PlayerState;
		npc: NPC;
		onClose: () => void;
		onFight: () => void;
		onTrade: () => void;
	};

	let {player = $bindable(), npc, onClose, onFight, onTrade}: Props = $props();

	let talkMessage = $state('');
	let showTalk = $state(false);

	const TYPE_LABELS: Record<number, string> = {
		[NPCType.Peasant]: 'Peasant',
		[NPCType.Woodcutter]: 'Woodcutter',
		[NPCType.Merchant]: 'Merchant',
		[NPCType.Caravan]: 'Caravan',
		[NPCType.Bandit]: 'Bandit',
		[NPCType.Guard]: 'Guard',
		[NPCType.Witch]: 'Witch',
		[NPCType.Sorceress]: 'Sorceress',
	};

	const TALK_LINES: Record<number, string[]> = {
		[NPCType.Peasant]: [
			'The harvest has been poor this year...',
			'Have you heard? Bandits roam the roads at night.',
			'Blessings upon you, traveler.',
			'I sell nothing of interest, but the merchant might.',
			'Stay safe out there. The wilderness is harsh.',
		],
		[NPCType.Woodcutter]: [
			'These woods hold many secrets.',
			'Good timber is hard to find lately.',
			'Watch for wolves near the tree line.',
			'I chop from dawn to dusk. Honest work.',
		],
		[NPCType.Merchant]: [
			'Looking to trade? I have fine wares!',
			'Gold makes the world go round, friend.',
			'I travel between settlements. The roads are dangerous.',
			'Business has been slow. Perhaps you need something?',
		],
		[NPCType.Caravan]: [
			'Long road ahead. Care to trade before I move on?',
			'I have seen many lands. Each stranger than the last.',
			'The roads between settlements grow more perilous.',
			'My oxen grow weary. We rest here briefly.',
		],
		[NPCType.Bandit]: [
			'Your gold or your life!',
			'Heh, another fool wandering the wilds.',
			'I take what I want. Got a problem with that?',
			'The strong survive. The weak feed us.',
		],
		[NPCType.Guard]: [
			'Move along, citizen. Nothing to see here.',
			'The settlement is safe under our watch.',
			'Report any bandit sightings to the elder.',
			'Stay on the roads if you value your life.',
		],
		[NPCType.Witch]: [
			'The spirits whisper of your coming...',
			'I see great trials ahead for you.',
			'Herbs and potions are my trade. Interested?',
			'The forest knows all. Listen carefully.',
		],
		[NPCType.Sorceress]: [
			'The arcane currents shift around you...',
			'Few mortals seek me out willingly.',
			'I deal in mysteries beyond your understanding.',
			'Power has a price. Are you willing to pay?',
		],
	};

	function talk() {
		const lines = TALK_LINES[npc.type] ?? ['...'];
		talkMessage = lines[Math.floor(Math.random() * lines.length)];
		showTalk = true;
	}

	function handleFight() {
		// Fighting a non-bandit lowers reputation
		if (npc.type !== NPCType.Bandit) {
			player.reputation.Wilderness = (player.reputation.Wilderness ?? 0) - 5;
		}

		onFight();
	}

	const NPC_SPRITES: Record<number, string> = {
		[NPCType.Peasant]: '/assets/sprites/peasant_256.png',
		[NPCType.Woodcutter]: '/assets/sprites/peasant_256.png',
		[NPCType.Merchant]: '/assets/sprites/corovan_256.png',
		[NPCType.Caravan]: '/assets/sprites/corovan_256.png',
		[NPCType.Bandit]: '/assets/sprites/imp_golem_256.png',
		[NPCType.Guard]: '/assets/sprites/peasant_256.png',
		[NPCType.Witch]: '/assets/sprites/witch_256.png',
		[NPCType.Sorceress]: '/assets/sprites/witch_256.png',
	};
	const npcSprite = NPC_SPRITES[npc.type] ?? '/assets/sprites/peasant_256.png';

	let typeLabel = $derived(TYPE_LABELS[npc.type] ?? 'Unknown');
</script>

<svelte:window onkeydown={e => { if (e.key === 'Escape') onClose(); }} />

<div class="absolute inset-0 flex items-center justify-center" style="background: rgba(20, 10, 5, 0.9);">
	<div class="flex h-[520px] w-[700px] flex-col rounded-lg border-4 font-sans" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
		<!-- Top: Large NPC portrait + info -->
		<div class="flex items-center gap-5 border-b p-5" style="border-color: #8b6f47;">
			<img src={npcSprite} alt={npc.name} class="h-[192px] w-[192px] rounded-lg border-2 object-contain" style="image-rendering:pixelated; border-color: #8b6f47; background: linear-gradient(to bottom, #b8a890, #a89880);" />
			<div class="flex-1">
				<h2 class="text-xl font-black" style="color: #3d2817; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">{npc.name}</h2>
				<div class="mb-1 text-sm" style="color: #6a5a4a;">{typeLabel} &middot; Lv.{npc.level}</div>
				{#if npc.traits && npc.traits.length > 0}
					<div class="mb-2 flex flex-wrap gap-1">
						{#each npc.traits as trait}
							<span class="cursor-help rounded bg-[#5a3a2a]/10 border border-[#8b6f47]/40 px-1.5 py-0.5 text-[10px] font-bold uppercase tracking-widest" style="color: #8b6f3a;" title="A defining trait of this individual.">
								{trait}
							</span>
						{/each}
					</div>
				{/if}
				<div class="mb-1 h-3 w-48 overflow-hidden rounded-full border" style="background: #5a3a2a; border-color: #3d2817;">
					<div class="h-full rounded-full" style="background: linear-gradient(to right, #c84a4a, #d86a6a); width:{Math.max(0, npc.hp / npc.maxHp * 100)}%"></div>
				</div>
				<div class="text-xs" style="color: #8b3a3a;">HP: {npc.hp}/{npc.maxHp}</div>
			</div>
		</div>

		<!-- Talk message area (fixed height) -->
		<div class="flex h-16 items-center justify-center border-b px-5" style="border-color: #8b6f47; background: linear-gradient(to bottom, #c8b89f, #b8a88f);">
			{#if showTalk}
				<p class="text-center text-sm leading-relaxed" style="color: #3d2817;">&ldquo;{talkMessage}&rdquo;</p>
			{:else}
				<p class="text-center text-sm" style="color: #7a6a5a;">Click Talk to speak with {npc.name}</p>
			{/if}
		</div>

		<!-- Action buttons (fixed position) -->
		<div class="flex flex-1 flex-col justify-center gap-2 px-8 py-3">
			<button onclick={talk} class="rounded border-2 px-4 py-3 text-sm font-bold transition" style="background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); border-color: #5a6a7a; color: #f0e8d8;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #9aaaba, #7a8a9a)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #8a9aaa, #6a7a8a)'}>Talk</button>
			<button onclick={onTrade} class="rounded border-2 px-4 py-3 text-sm font-bold transition" style="background: linear-gradient(to bottom, #d4a574, #b8935a); border-color: #8b6f47; color: #3d2817;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e4b584, #c8a36a)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d4a574, #b8935a)'}>Trade</button>
			<button onclick={handleFight} class="rounded border-2 px-4 py-3 text-sm font-bold transition" style="background: linear-gradient(to bottom, #c86a6a, #a84a4a); border-color: #8a3a3a; color: #f0e8d8;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d87a7a, #b85a5a)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c86a6a, #a84a4a)'}>Fight</button>
			<button onclick={onClose} class="rounded border-2 px-4 py-3 text-sm font-bold transition" style="background: linear-gradient(to bottom, #a89880, #988870); border-color: #7a6a5a; color: #3d2817;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #a89880, #988870)'}>Leave [Esc]</button>
		</div>
	</div>
</div>
