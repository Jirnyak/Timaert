<script lang="ts">
	import type {PlayerState, Faction} from '../game/state';
	import type {Politik} from '../game/politik';
	import {
		color, backdropStyle, panelStyle, headingStyle, dividerStyle, btnProps,
	} from '../ui/theme';

	type Props = {
		player: PlayerState;
		factions: Record<string, Faction>;
		politik?: Politik | null;
		onClose: () => void;
	};

	const {player, factions, politik, onClose}: Props = $props();

	const factionList = $derived(Object.values(factions));
	const kingdomList = $derived(politik ? Object.values(politik.kingdoms) : []);

	// Relation tier table — single source of truth for label + color.
	// Sorted high → low; first matching threshold wins.
	const RELATION_TIERS: ReadonlyArray<{min: number; label: string; color: string}> = [
		{min: 80, label: 'Ally', color: '#4a7c4a'},
		{min: 40, label: 'Friendly', color: '#4a7c4a'},
		{min: 10, label: 'Neutral', color: '#b8935a'},
		{min: -10, label: 'Wary', color: '#b8935a'},
		{min: -50, label: 'Hostile', color: '#8a3a3a'},
		{min: Number.NEGATIVE_INFINITY, label: 'War', color: '#8a3a3a'},
	];

	function getRelationTier(value: number): {label: string; color: string} {
		for (const t of RELATION_TIERS) {
			if (value >= t.min) {
				return t;
			}
		}

		return RELATION_TIERS.at(-1);
	}

	function getRelationLabel(value: number): string {
		return getRelationTier(value).label;
	}

	function getRelationColor(value: number): string {
		return getRelationTier(value).color;
	}
</script>

<svelte:window onkeydown={e => {
	if (e.key === 'Escape') {
		onClose();
	}
}} />

<div class="absolute inset-0 flex items-center justify-center" style={backdropStyle('medium')}>
	<div class="flex h-[600px] w-[800px] flex-col rounded-lg border-4 font-sans" style={panelStyle()}>
		<div class="flex items-center justify-between border-b px-6 py-4" style={dividerStyle}>
			<h2 class="text-2xl font-black uppercase tracking-widest" style={headingStyle}>World Politics</h2>
			<button onclick={onClose} class="rounded border-2 px-3 py-1 text-sm font-bold transition" {...btnProps('close')}>Close [Esc]</button>
		</div>

		<div class="flex-1 overflow-y-auto p-6 grid grid-cols-1 gap-4" style="scrollbar-width: none;">
			{#if kingdomList.length > 0}
				<div class="flex flex-col gap-2 rounded border-2 p-4 shadow-sm" style="background: rgba(255, 255, 255, 0.15); border-color: {color.divider};">
					<h3 class="text-lg font-black uppercase" style={headingStyle}>Kingdoms of the World</h3>
					<div class="grid grid-cols-2 gap-2 mt-1">
						{#each kingdomList as kingdom}
							<div class="flex flex-col rounded border-2 p-2" style="border-color: {kingdom.color}; background: {kingdom.color}15;">
								<span class="text-sm font-black uppercase" style="color: {kingdom.color}; text-shadow: 0 1px 1px rgba(0,0,0,0.3);">{kingdom.name}</span>
								<span class="text-[10px] text-[#5a4a3a]">Cities: {kingdom.cityIdxs.length} · Lineage: {kingdom.lineage}</span>
							</div>
						{/each}
					</div>
				</div>
			{/if}
			{#each factionList as faction}
			{@const rep = player.reputation[faction.id] ?? 0}
			<div class="flex flex-col gap-2 rounded border-2 p-4 shadow-sm" style="background: rgba(255, 255, 255, 0.15); border-color: {color.divider};">
				<!-- Header -->
				<div class="flex justify-between items-start">
					<div>
						<h3 class="text-lg font-black uppercase" style="color: {faction.color}; text-shadow: 0 1px 1px rgba(0,0,0,0.3);">{faction.name}</h3>
						<p class="text-xs italic text-[#5a4a3a]">{faction.description}</p>
					</div>
					<div class="text-right">
						<div class="text-xs font-bold uppercase tracking-wide text-[#5a4a3a]">Your Standing</div>
					<div class="text-lg font-black" style="color: {getRelationColor(rep)};">
						{rep} ({getRelationLabel(rep)})
					</div>
				</div>
			</div>
					<div class="w-full bg-[#3d2817]/20 h-2 rounded-full mt-1 overflow-hidden border border-[#5a4a3a]/30">
						<div class="h-full transition-all duration-500"
							style="width: {Math.min(100, Math.max(0, (player.reputation[faction.id] ?? 0) + 100) / 2)}%; background: {getRelationColor(player.reputation[faction.id] ?? 0)};">
						</div>
					</div>

					<!-- Faction Relations -->
					<div class="mt-2 border-t border-[#8b6f47]/30 pt-2">
						<span class="text-[10px] font-bold uppercase tracking-widest text-[#6a5a4a]">Foreign Relations:</span>
						<div class="flex flex-wrap gap-2 mt-1">
							{#each Object.entries(faction.relations) as [targetId, value]}
								{#if factions[targetId]}
									<span class="text-xs px-2 py-0.5 rounded border"
										style="background: {getRelationColor(value)}20; border-color: {getRelationColor(value)}; color: {getRelationColor(value)};">
										{factions[targetId].name}: {value}
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
