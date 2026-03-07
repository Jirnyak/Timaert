<script lang="ts">
	import type {PlayerState, Settlement} from '../game/state';
	import {CityGenerator} from '../game/city-generator';
	import {color, panelStyle, dividerStyle, accentHeadingStyle, bodyStyle, mutedStyle, messageStyle, tabStyle, tabHover, tabOut, btnProps, barTrackStyle, barFillStyle, sectionStyle} from '../ui/theme';

	type Props = {
		player: PlayerState;
		settlement: Settlement;
		worldSeed: number;
		onClose: () => void;
		onEnter: () => void;
		onTrade: () => void;
	};

	let {player, settlement, worldSeed, onClose, onEnter, onTrade}: Props = $props();

	let tab = $state<'rest' | 'info' | 'map' | 'history'>('info');
	let message = $state('');
	let mapUrl = $state('');
	let mapGenerated = $state(false);

	const MOOD_INFO: Record<string, {color: string; desc: string}> = {
		Prosperous: {color: color.positive, desc: 'Trade is booming. Prices are fair.'},
		Stable: {color: color.body, desc: 'Life follows its routine.'},
		Tense: {color: color.warning, desc: 'Whispers of dissent in the taverns.'},
		Unrest: {color: '#c86a6a', desc: 'Guards are nervous. Prices fluctuate.'},
		Revolt: {color: color.negative, desc: 'Chaos reigns. Dangerous to linger.'},
	};

	// Lazy generate map only when map tab is accessed
	$effect(() => {
		if (tab === 'map' && !mapGenerated) {
			const seed = worldSeed + settlement.id * 123;
			const gen = new CityGenerator(seed, 128, 128, 'city');
			const data = gen.generate(settlement.population);
			mapUrl = data.visual.toDataURL();
			mapGenerated = true;
		}
	});

	function getRestCost(): number {
		const base = 10;
		if (settlement.mood === 'Prosperous') return 5;
		if (settlement.mood === 'Tense') return 15;
		if (settlement.mood === 'Unrest') return 20;
		if (settlement.mood === 'Revolt') return 30;
		return base;
	}

	function rest() {
		const cost = getRestCost();
		if (player.gold < cost) {
			message = `Not enough gold to rest! (${cost}g)`;
			return;
		}

		player.gold -= cost;
		player.combatStats.currentHp = player.combatStats.maxHp;
		player.combatStats.currentMp = player.combatStats.maxMp;
		player.combatStats.currentSp = player.combatStats.maxSp;
		message = 'Fully rested! HP/MP/SP restored.';
	}
</script>

<svelte:window onkeydown={event => { if (event.key === 'Escape') onClose(); }} />

<div class="absolute inset-0 flex items-center justify-center z-[100]" style="background: {color.backdrop};">
	<div class="w-[500px] rounded-lg border-4 p-5 font-sans overflow-hidden" style={panelStyle()}>
		<!-- Heraldic Header -->
		<div class="mb-4 flex items-start gap-4 border-b pb-4" style={dividerStyle}>
			<div class="h-20 w-20 shrink-0 overflow-hidden rounded border-2 shadow-lg" style="border-color: {color.divider}; background: {color.darkBg};">
				<img src={settlement.banner} alt="City Banner" class="h-full w-full object-cover" />
			</div>
			<div class="flex-1">
				<div class="flex items-center justify-between">
					<h2 class="text-2xl font-black tracking-tight uppercase" style={accentHeadingStyle}>{settlement.name}</h2>
					<button onclick={onClose} class="rounded border-2 px-2 py-1 text-[10px] font-bold uppercase tracking-tighter transition" {...btnProps('close')}>Leave [Esc]</button>
				</div>
				<div class="mt-1 flex gap-3 text-[10px] font-bold uppercase tracking-widest" style={mutedStyle}>
					<span class="cursor-help" title="The foundation of any world. Drives local production and caravan spawning.">Pop: <span style={bodyStyle}>{settlement.population}</span></span>
					<span class="cursor-help" title="Determines the types of goods produced in local production chains.">Econ: <span style="color: #6a7a8a;">{settlement.economy}</span></span>
				</div>
			</div>
		</div>

		<div class="mb-1 flex items-center justify-between text-xs" style={mutedStyle}>
			<span>Pop: {settlement.population} | Econ: {settlement.economy}</span>
			<span class="cursor-help font-bold uppercase tracking-wide" style="color: {MOOD_INFO[settlement.mood]?.color ?? color.muted};" title={MOOD_INFO[settlement.mood]?.desc}>
				Mood: {settlement.mood}
			</span>
		</div>

		<!-- Tabs -->
		<div class="mb-3 flex gap-1 border-b pb-2" style={dividerStyle}>
			{#each ['info', 'rest', 'map', 'history'] as t}
				<button
					onclick={() => { tab = t; }}
					class="rounded px-3 py-1 text-sm transition"
					style={tabStyle(tab === t)}
					onmouseover={tabHover(tab === t)}
					onmouseout={tabOut(tab === t)}
				>{t[0].toUpperCase() + t.slice(1)}</button>
			{/each}
		</div>

		<!-- Info tab -->
		{#if tab === 'info'}
			<div class="space-y-2 text-sm" style={bodyStyle}>
				<p>Welcome to <span style="color: {color.accent}; font-weight: bold;">{settlement.name}</span>.</p>
				<p>This is a settlement with a population of {settlement.population} and a {settlement.economy} economy.</p>
				<p style={mutedStyle}>You can trade goods or rest here to restore your vitals.</p>
				<div class="flex gap-2 pt-2">
					<button onclick={onEnter} class="rounded border-2 px-4 py-2 text-sm font-bold transition" {...btnProps('primary')}>Enter City</button>
					<button onclick={onTrade} class="rounded border-2 px-4 py-2 text-sm font-bold transition" {...btnProps('primary')}>Trade</button>
				</div>
			</div>
		{/if}

		<!-- Map tab -->
		{#if tab === 'map'}
			<div class="space-y-3 text-sm" style={bodyStyle}>
				<div class="flex items-center justify-between">
					<span style={mutedStyle}>City preview</span>
					<button
						onclick={() => {
							const seed = worldSeed + settlement.id * 123;
							const gen = new CityGenerator(seed, 128, 128, 'city');
							const data = gen.generate(settlement.population);
							mapUrl = data.visual.toDataURL();
						}}
						class="rounded border px-2 py-1 text-[10px] font-bold uppercase tracking-widest transition"
						{...btnProps('close')}
					>Refresh</button>
				</div>
				<div class="overflow-hidden rounded border-2" style="border-color: {color.divider}; background: {color.darkBg};">
					{#if mapUrl}
						<img src={mapUrl} alt="City map preview" class="h-64 w-full object-cover" />
					{:else}
						<div class="flex h-64 items-center justify-center" style={mutedStyle}>Generating map…</div>
					{/if}
				</div>
				<div class="text-xs" style={mutedStyle}>Seed: {worldSeed + settlement.id * 123} · Population: {settlement.population}</div>
			</div>
		{/if}

		<!-- Rest tab -->
		{#if tab === 'rest'}
			<div class="space-y-3 text-sm">
				<div style={bodyStyle}>
					<p>Rest at the inn to fully restore HP, MP, and SP.</p>
					<p class="mt-1" style={mutedStyle}>Cost: 10 gold</p>
					<p class="mt-2 text-xs italic text-amber-900/60" title="A warm hearth shields you from the whispers of dead gods.">"A safe haven in a fractured world."</p>
				</div>
				<div class="flex gap-3 text-xs">
					<span style="color: {color.hp};">HP: {player.combatStats.currentHp}/{player.combatStats.maxHp}</span>
					<span style="color: {color.mp};">MP: {player.combatStats.currentMp}/{player.combatStats.maxMp}</span>
					<span style="color: {color.sp};">SP: {Math.floor(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
				</div>
				<button onclick={rest} class="rounded border-2 px-4 py-2 text-sm font-bold transition" {...btnProps('action')}>Rest ({getRestCost()}g)</button>
			</div>
		{/if}

		{#if tab === 'history'}
			<div class="space-y-3">
				<div class="flex items-center justify-between text-xs" style={mutedStyle}>
					<span>Population Trend (Last 30 Days)</span>
					<span style="color: {color.positive};">Current: {settlement.population}</span>
				</div>
				
				<div class="h-40 w-full rounded border-2 p-2 bg-black/20" style={dividerStyle}>
					{#if settlement.history.population.length > 1}
					{@const minPop = Math.min(...settlement.history.population) * 0.9}
					{@const maxPop = Math.max(...settlement.history.population) * 1.1}
					{@const range = maxPop - minPop}
					{@const points = settlement.history.population.map((p, i) => 
						`${(i / (settlement.history.population.length - 1)) * 100},${40 - ((p - minPop) / range) * 40}`
					).join(' ')}
					<svg viewBox="0 0 100 40" class="h-full w-full" preserveAspectRatio="none">
							<polyline
								points={points}
								fill="none"
								stroke={color.accent}
								stroke-width="1"
								vector-effect="non-scaling-stroke"
							/>
							{#each settlement.history.population as p, i}
								<circle 
									cx={(i / (settlement.history.population.length - 1)) * 100} 
									cy={40 - ((p - minPop) / range) * 40} 
									r="1" 
									fill={color.heading}
								/>
							{/each}
						</svg>
					{:else}
						<div class="flex h-full items-center justify-center text-[10px] uppercase tracking-widest" style={mutedStyle}>
							Not enough data yet...
						</div>
					{/if}
				</div>
				<p class="text-[10px] italic" style={bodyStyle}>Historical shifts are recorded at the end of each game day.</p>
			</div>
		{/if}

		<!-- Message -->
		{#if message}
			<div class="mt-3 rounded border px-3 py-2 text-center text-sm" style={messageStyle}>{message}</div>
		{/if}
	</div>
</div>
