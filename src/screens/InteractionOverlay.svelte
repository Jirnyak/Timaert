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

<div class="absolute inset-0 flex items-center justify-center bg-black/85">
	<div class="flex h-[520px] w-[700px] flex-col rounded-lg border-2 border-amber-900/60 bg-gray-950/95 font-sans shadow-2xl">
		<!-- Top: Large NPC portrait + info -->
		<div class="flex items-center gap-5 border-b border-gray-800 p-5">
			<img src={npcSprite} alt={npc.name} class="h-[192px] w-[192px] rounded-lg border-2 border-gray-700 bg-gray-900/50 object-contain" style="image-rendering:pixelated" />
			<div class="flex-1">
				<h2 class="text-xl font-black text-white">{npc.name}</h2>
				<div class="mb-2 text-sm text-gray-400">{typeLabel} &middot; Lv.{npc.level}</div>
				<div class="mb-1 h-3 w-48 overflow-hidden rounded-full bg-gray-800">
					<div class="h-full rounded-full bg-red-600" style="width:{Math.max(0, npc.hp / npc.maxHp * 100)}%"></div>
				</div>
				<div class="text-xs text-red-400">HP: {npc.hp}/{npc.maxHp}</div>
			</div>
		</div>

		<!-- Talk message area (fixed height) -->
		<div class="flex h-16 items-center justify-center border-b border-gray-800 px-5">
			{#if showTalk}
				<p class="text-center text-sm leading-relaxed text-cyan-300">&ldquo;{talkMessage}&rdquo;</p>
			{:else}
				<p class="text-center text-sm text-gray-600">Click Talk to speak with {npc.name}</p>
			{/if}
		</div>

		<!-- Action buttons (fixed position) -->
		<div class="flex flex-1 flex-col justify-center gap-2 px-8 py-3">
			<button onclick={talk} class="rounded border border-cyan-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white hover:bg-cyan-900/40">Talk</button>
			{#if npc.type === NPCType.Merchant || npc.type === NPCType.Caravan || npc.type === NPCType.Witch || npc.type === NPCType.Sorceress}
				<button onclick={onTrade} class="rounded border border-yellow-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white hover:bg-yellow-900/40">Trade</button>
			{/if}
			<button onclick={handleFight} class="rounded border border-red-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white hover:bg-red-900/40">Fight</button>
			<button onclick={onClose} class="rounded border border-gray-600 bg-gray-800/80 px-4 py-3 text-sm font-bold text-gray-300 hover:bg-gray-700/60">Leave [Esc]</button>
		</div>
	</div>
</div>
