<script lang="ts">
	import {type NPC, NPCType} from '../game/npc';
	import type {Faction} from '../game/state';
	import {color} from '../ui/theme';

	type NearbyNpc = {
		npc: NPC;
		direction: string;
		sameCell: boolean;
	};

	type Props = {
		nearbyNpcs: NearbyNpc[];
		factions: Record<string, Faction>;
		onInteract: (npc: NPC) => void;
	};

	const {nearbyNpcs, factions, onInteract}: Props = $props();

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

	function factionColor(factionId: string): string {
		return factions[factionId]?.color ?? '#888';
	}

	function factionName(factionId: string): string {
		return factions[factionId]?.name ?? 'Unknown';
	}
</script>

{#if nearbyNpcs.length > 0}
	<div class="absolute right-2 top-20 flex max-h-[calc(100vh-120px)] flex-col gap-1.5 overflow-y-auto pr-1" style="scrollbar-width: none;">
		{#each nearbyNpcs as {npc, direction, sameCell} (npc.id)}
			<button
				class="flex w-52 cursor-pointer items-center gap-2 rounded border-2 px-2 py-1.5 font-sans text-xs shadow-lg transition hover:brightness-110"
				style="
					background: linear-gradient(to right, rgba(20,10,5,0.92), rgba(30,18,10,0.88));
					border-color: {factionColor(npc.factionId)}60;
				"
				onclick={() => onInteract(npc)}
				title="Interact with {npc.name}"
			>
				<img
					src={NPC_SPRITES[npc.type] ?? '/assets/sprites/peasant_256.png'}
					alt={npc.name}
					class="h-9 w-9 shrink-0 rounded object-contain"
					style="image-rendering: pixelated; background: rgba(0,0,0,0.3);"
				/>
				<div class="flex min-w-0 flex-1 flex-col items-start">
					<span class="w-full truncate font-bold" style="color: {factionColor(npc.factionId)};">
						{npc.name}
					</span>
					<span class="text-[10px]" style="color: {color.muted};">
						{TYPE_LABELS[npc.type] ?? 'Unknown'} · Lv.{npc.level}
					</span>
					<span class="text-[10px]" style="color: {factionColor(npc.factionId)}80;">
						{factionName(npc.factionId)}
					</span>
				</div>
				<div class="flex shrink-0 flex-col items-end gap-0.5">
					<span class="rounded bg-black/40 px-1 py-0.5 text-[9px] font-bold uppercase tracking-wide text-amber-300">
						{sameCell ? 'Here' : direction}
					</span>
					<span class="text-[9px]" style="color: {color.hp};">
						{npc.hp}/{npc.maxHp}
					</span>
				</div>
			</button>
		{/each}
	</div>
{/if}
