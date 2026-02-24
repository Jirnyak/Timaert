<script lang="ts">
	import type {PlayerState, Faction} from '../game/state';

	type Props = {
		player: PlayerState;
		factions: Record<string, Faction>;
		onClose: () => void;
	};

	let {player, factions, onClose}: Props = $props();

	const factionList = $derived(Object.values(factions));

	function getRelationLabel(val: number): string {
		if (val >= 80) return 'Ally';
		if (val >= 40) return 'Friendly';
		if (val >= 10) return 'Neutral';
		if (val >= -10) return 'Wary';
		if (val >= -50) return 'Hostile';
		return 'War';
	}

	function getRelationColor(val: number): string {
		if (val >= 40) return '#4a7c4a'; // Green
		if (val >= -10) return '#b8935a'; // Yellow
		return '#8a3a3a'; // Red
	}
</script>

<svelte:window onkeydown={e => { if (e.key === 'Escape') onClose(); }} />

<div class="absolute inset-0 flex items-center justify-center" style="background: rgba(20, 10, 5, 0.9);">
	<div class="flex h-[600px] w-[800px] flex-col rounded-lg border-4 font-sans" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
		<div class="flex items-center justify-between border-b px-6 py-4" style="border-color: #8b6f47;">
			<h2 class="text-2xl font-black uppercase tracking-widest" style="color: #3d2817; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">World Politics</h2>
			<button onclick={onClose} class="rounded border-2 px-3 py-1 text-sm font-bold transition" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b8a0, #b8a890)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #b8a890, #a89880)'}>Close [Esc]</button>
		</div>

		<div class="flex-1 overflow-y-auto p-6 grid grid-cols-1 gap-4" style="scrollbar-width: none;">
			{#each factionList as faction}
				<div class="flex flex-col gap-2 rounded border-2 p-4 shadow-sm" style="background: rgba(255, 255, 255, 0.15); border-color: #8b6f47;">
					<!-- Header -->
					<div class="flex justify-between items-start">
						<div>
							<h3 class="text-lg font-black uppercase" style="color: {faction.color}; text-shadow: 0 1px 1px rgba(0,0,0,0.3);">{faction.name}</h3>
							<p class="text-xs italic text-[#5a4a3a]">{faction.description}</p>
						</div>
						<div class="text-right">
							<div class="text-xs font-bold uppercase tracking-wide text-[#5a4a3a]">Your Standing</div>
							{@const rep = player.reputation[faction.id] ?? 0}
							<div class="text-lg font-black" style="color: {getRelationColor(rep)};">
								{rep} ({getRelationLabel(rep)})
							</div>
						</div>
					</div>

					<!-- Relations Bar -->
					<div class="w-full bg-[#3d2817]/20 h-2 rounded-full mt-1 overflow-hidden border border-[#5a4a3a]/30">
						<div class="h-full transition-all duration-500" 
							 style="width: {Math.min(100, Math.max(0, (player.reputation[faction.id] ?? 0) + 100) / 2)}%; background: {getRelationColor(player.reputation[faction.id] ?? 0)};">
						</div>
					</div>

					<!-- Faction Relations -->
					<div class="mt-2 border-t border-[#8b6f47]/30 pt-2">
						<span class="text-[10px] font-bold uppercase tracking-widest text-[#6a5a4a]">Foreign Relations:</span>
						<div class="flex flex-wrap gap-2 mt-1">
							{#each Object.entries(faction.relations) as [targetId, val]}
								{#if factions[targetId]}
									<span class="text-xs px-2 py-0.5 rounded border" 
										style="background: {getRelationColor(val)}20; border-color: {getRelationColor(val)}; color: {getRelationColor(val)};">
										{factions[targetId].name}: {val}
									</span>
								{/if}
							{/each}
						</div>
					</div>
				</div>
			{/each}
		</div>
	</div>
</div>
