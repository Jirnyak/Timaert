<script lang="ts">
	import type {PlayerState} from '../game/state';
	import {type NPC, NPC_TYPE_DEFS, FALLBACK_NPC_PORTRAIT} from '../game/npc';
		import {
			color, panelStyle, dividerStyle, headingStyle, mutedStyle, btnProps, barTrackStyle, barFillStyle,
		} from '../ui/theme';

	type Props = {
		player: PlayerState;
		npc: NPC;
		isHostile?: boolean;
		fleeCost: number;
		fleeChance: number;
		bribeCost: number;
		bribeChance: number;
		onClose: () => void;
		onTrade: () => void;
		onFight: () => void;
		onFlee?: () => void;
		onBribe?: () => void;
	};

	let {
		player = $bindable(),
		npc,
		isHostile = false,
		fleeCost,
		fleeChance,
		bribeCost,
		bribeChance,
		onClose,
		onTrade,
		onFight,
		onFlee,
		onBribe,
	}: Props = $props();

	let talkMessage = $state('');
	let showTalk = $state(false);

	const def = $derived(NPC_TYPE_DEFS[npc.type]);
	const typeLabel = $derived(def?.label ?? 'Unknown');
	const npcSprite = $derived(def?.portrait ?? FALLBACK_NPC_PORTRAIT);

	const canFlee = $derived(isHostile && player.combatStats.currentSp >= fleeCost);
	const canBribe = $derived(isHostile && player.gold >= bribeCost);

	function talk() {
		const lines = def?.talkLines ?? ['...'];
		talkMessage = lines[Math.floor(Math.random() * lines.length)];
		showTalk = true;
	}

	function pct(p: number): string {
		return Math.round(Math.max(0, Math.min(1, p)) * 100) + '%';
	}
</script>

<svelte:window onkeydown={e => {
	if (e.key === 'Escape' && !isHostile) {
		onClose();
	}
}} />

<div class="absolute inset-0 flex items-center justify-center" style="background: {color.backdropMedium};">
	<div class="flex h-[520px] w-[700px] flex-col rounded-lg border-4 font-sans" style={panelStyle()}>
		<!-- Top: Large NPC portrait + info -->
		<div class="flex items-center gap-5 border-b p-5" style={dividerStyle}>
			<img src={npcSprite} alt={npc.name} class="h-[192px] w-[192px] rounded-lg border-2 object-contain" style="image-rendering:pixelated; border-color: {color.divider}; background: {color.messageBg};" />
			<div class="flex-1">
				<h2 class="text-xl font-black" style={headingStyle}>{npc.name}</h2>
				<div class="mb-1 text-sm" style="color: {color.subtitle};">{typeLabel} &middot; Lv.{npc.level}</div>
				{#if npc.traits && npc.traits.length > 0}
					<div class="mb-2 flex flex-wrap gap-1">
						{#each npc.traits as trait}
							<span class="cursor-help rounded bg-[{color.barTrack}]/10 border border-[{color.divider}]/40 px-1.5 py-0.5 text-[10px] font-bold uppercase tracking-widest" style="color: {color.accent};" title="A defining trait of this individual.">
								{trait}
							</span>
						{/each}
					</div>
				{/if}
				<div class="mb-1 h-3 w-48 overflow-hidden rounded-full border" style={barTrackStyle}>
					<div class="h-full rounded-full" style={barFillStyle(Math.max(0, npc.hp / npc.maxHp * 100))}></div>
				</div>
				<div class="text-xs" style="color: {color.hp};">HP: {npc.hp}/{npc.maxHp}</div>
			</div>
		</div>

		{#if isHostile}
			<!-- Ambush banner -->
			<div class="flex h-16 items-center justify-center border-b px-5" style="{dividerStyle} background: {color.messageBg};">
				<p class="text-center text-base font-black uppercase tracking-widest" style="color: {color.hp};">{npc.name} is attacking!</p>
			</div>

			<!-- Ambush options -->
			<div class="flex flex-1 flex-col justify-center gap-2 px-8 py-3">
				<button onclick={onFight} class="rounded border-2 px-4 py-3 text-sm font-bold transition" {...btnProps('close')}>Fight!</button>
				<button onclick={onFlee} disabled={!canFlee} class="rounded border-2 px-4 py-3 text-sm font-bold transition disabled:opacity-50" {...btnProps('action')}>
					Flee &middot; {fleeCost} SP &middot; {pct(fleeChance)} chance
				</button>
				<button onclick={onBribe} disabled={!canBribe} class="rounded border-2 px-4 py-3 text-sm font-bold transition disabled:opacity-50" {...btnProps('primary')}>
					Bribe &middot; {bribeCost}g &middot; {pct(bribeChance)} chance
				</button>
			</div>
		{:else}
			<!-- Talk message area (fixed height) -->
			<div class="flex h-16 items-center justify-center border-b px-5" style="{dividerStyle} background: {color.innerPanelBg};">
				{#if showTalk}
					<p class="text-center text-sm leading-relaxed" style="color: {color.heading};">&ldquo;{talkMessage}&rdquo;</p>
				{:else}
					<p class="text-center text-sm" style={mutedStyle}>Click Talk to speak with {npc.name}</p>
				{/if}
			</div>

			<!-- Standard action buttons -->
			<div class="flex flex-1 flex-col justify-center gap-2 px-8 py-3">
				<button onclick={talk} class="rounded border-2 px-4 py-3 text-sm font-bold transition" {...btnProps('action')}>Talk</button>
				<button onclick={onTrade} class="rounded border-2 px-4 py-3 text-sm font-bold transition" {...btnProps('primary')}>Trade</button>
				<button onclick={onFight} class="rounded border-2 px-4 py-3 text-sm font-bold transition" {...btnProps('close')}>Fight</button>
				<button onclick={onClose} class="rounded border-2 px-4 py-3 text-sm font-bold transition" {...btnProps('muted')}>Leave [Esc]</button>
			</div>
		{/if}
	</div>
</div>
