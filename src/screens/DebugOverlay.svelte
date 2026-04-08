<script lang="ts">
	import type {NPC} from '../game/npc';
	import type {GameState} from '../game/state';
	import {getAtlas} from '../character/atlas-loader';

	type SubworldDebugEntity = {
		id: number;
		kind: string;
		x: number;
		y: number;
		hp?: number;
		maxHp?: number;
		label?: string;
		factionId?: string;
	};

	type SubworldDebugData = {
		entities: SubworldDebugEntity[];
		playerX: number;
		playerY: number;
		mode: string;
		view3d: boolean;
		friendlies: number;
		enemies: number;
		seed: number;
		mapSize: number;
	};

	type DebugData = {
		gState: GameState;
		npcs: NPC[];
		cityNpcs: NPC[];
		inCity: boolean;
		trees: Array<{x: number; y: number}>;
		mapW: number;
		mapH: number;
		visualPlayerX: number;
		visualPlayerY: number;
		fps: number;
		frameDt: number;
		simSpeed: number;
		zoom: number;
		cameraX: number;
		cameraY: number;
		canvasW: number;
		canvasH: number;
		dpr: number;
		atlasUploaded: boolean;
		subworld?: SubworldDebugData;
	};

	type Props = {
		data: DebugData;
		onClose: () => void;
		onTeleport?: (x: number, y: number) => void;
		onSetGold?: (amount: number) => void;
		onSetSpeed?: (speed: number) => void;
		onHealPlayer: () => void;
		onSetZoom?: (zoom: number) => void;
		onLearnAllSpells?: () => void;
		onAddExp?: (amount: number) => void;
		onKillAllEnemies?: () => void;
	};

	const {data, onClose, onTeleport, onSetGold, onSetSpeed, onHealPlayer, onSetZoom, onLearnAllSpells, onAddExp, onKillAllEnemies}: Props = $props();

	const isSubworld = $derived(Boolean(data.subworld));

	let expAmount = $state('1000');

	let teleportX = $state('');
	let teleportY = $state('');
	let goldAmount = $state('1000');
	let zoomValue = $state('40');
	let selectedTab = $state<'info' | 'npcs' | 'cheats' | 'journal' | 'entities'>('info');
	let logFilter = $state<'all' | 'combat' | 'economy' | 'politics'>('all');

	const subworldNearby = $derived.by(() => {
		if (!data.subworld) {
			return [];
		}

		const px = data.subworld.playerX;
		const py = data.subworld.playerY;
		return data.subworld.entities
			.filter(e => e.kind !== 'player' && (e.hp ?? 0) > 0)
			.map(e => ({...e, dist: Math.sqrt((e.x - px) ** 2 + (e.y - py) ** 2)}))
			.sort((a, b) => a.dist - b.dist)
			.slice(0, 30);
	});

	const atlas = $derived(getAtlas());

	function handleTeleport() {
		const x = Number.parseInt(teleportX, 10);
		const y = Number.parseInt(teleportY, 10);
		if (!Number.isNaN(x) && !Number.isNaN(y)) {
			onTeleport?.(x, y);
		}
	}

	function teleportToNpc(npc: NPC) {
		onTeleport?.(npc.x, npc.y);
	}

	function teleportToSettlement(s: {x: number; y: number}) {
		onTeleport?.(s.x, s.y);
	}

	const activeNpcs = $derived(data.inCity ? data.cityNpcs : data.npcs);
	const aliveNpcs = $derived(activeNpcs.filter(n => n.hp > 0));
	const nearbyNpcs = $derived(aliveNpcs.filter(n => {
		const dx = Math.abs(n.x - data.gState.player.x);
		const dy = Math.abs(n.y - data.gState.player.y);
		return dx < 30 && dy < 30;
	}));
	const visibleNpcs = $derived(aliveNpcs.filter(n => {
		const dx = Math.abs(n.x - data.gState.player.x);
		const dy = Math.abs(n.y - data.gState.player.y);
		const halfView = (data.zoom / 2) + 2;
		return dx < halfView && dy < halfView;
	}));

	const npcTypeNames: Record<number, string> = {
		0: 'Peasant',
		1: 'Woodcutter',
		2: 'Merchant',
		3: 'Caravan',
		4: 'Bandit',
		5: 'Guard',
		6: 'Witch',
		7: 'Sorceress',
	};

	const nearestSettlement = $derived.by(() => {
		let best: {name: string; x: number; y: number; dist: number} | undefined;
		for (const s of data.gState.settlements) {
			const dx = Math.abs(s.x - data.gState.player.x);
			const dy = Math.abs(s.y - data.gState.player.y);
			const dist = Math.sqrt(dx * dx + dy * dy);
			if (!best || dist < best.dist) {
				best = {
					name: s.name, x: s.x, y: s.y, dist,
				};
			}
		}

		return best;
	});
</script>

<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
	class="absolute inset-0 z-50 flex items-start justify-end p-2 pointer-events-none"
	onkeydown={e => {
		if (e.key === '`' || e.key === 'Escape') {
			onClose();
		}
	}}
>
	<div class="pointer-events-auto flex max-h-[90vh] w-96 flex-col rounded border border-green-800/60 bg-black/90 font-mono text-xs text-green-300 shadow-2xl">
		<!-- Title bar -->
		<div class="flex items-center justify-between border-b border-green-800/40 px-3 py-1.5">
			<span class="text-green-400 font-bold tracking-wider">DEBUG {isSubworld ? '· SUBWORLD' : '· MACROWORLD'}</span>
			<div class="flex gap-1">
				{#each (isSubworld ? ['info', 'entities', 'cheats'] : ['info', 'npcs', 'cheats', 'journal']) as tab}
					<button
						onclick={() => {
							selectedTab = tab as 'info' | 'npcs' | 'cheats' | 'journal' | 'entities';
						}}
						class="px-2 py-0.5 rounded text-[10px] uppercase tracking-wide transition
							{selectedTab === tab ? 'bg-green-800/60 text-green-200' : 'text-green-600 hover:text-green-400'}"
					>{tab}</button>
				{/each}
				<button onclick={onClose} class="ml-2 px-1 text-red-500 hover:text-red-300">&times;</button>
			</div>
		</div>

		<!-- Content -->
		<div class="overflow-y-auto p-3 space-y-2" style="scrollbar-width: none;">

			{#if selectedTab === 'info'}
			{#if isSubworld && data.subworld}
				<!-- Subworld Performance -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Performance</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>FPS</span><span class="text-yellow-300">{data.fps.toFixed(0)}</span>
						<span>Frame dt</span><span class="text-yellow-300">{data.frameDt.toFixed(1)} ms</span>
						<span>View</span><span class="text-yellow-300">{data.subworld.view3d ? '3D' : '2D'}</span>
						<span>DPR</span><span class="text-yellow-300">{data.dpr.toFixed(2)}</span>
						<span>Canvas</span><span class="text-yellow-300">{data.canvasW}&times;{data.canvasH}</span>
					</div>
				</div>

				<!-- Subworld Player -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Player</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>Subworld pos</span><span class="text-yellow-300">{data.subworld.playerX.toFixed(1)}, {data.subworld.playerY.toFixed(1)}</span>
						<span>Macro pos</span><span class="text-yellow-300">{data.gState.player.x}, {data.gState.player.y}</span>
						<span>HP</span><span class="text-yellow-300">{data.gState.player.combatStats.currentHp}/{data.gState.player.combatStats.maxHp}</span>
						<span>MP</span><span class="text-yellow-300">{data.gState.player.combatStats.currentMp}/{data.gState.player.combatStats.maxMp}</span>
						<span>SP</span><span class="text-yellow-300">{data.gState.player.combatStats.currentSp}/{data.gState.player.combatStats.maxSp}</span>
						<span>Gold</span><span class="text-yellow-300">{data.gState.player.gold}</span>
						<span>Level</span><span class="text-yellow-300">{data.gState.player.levelData.level}</span>
					</div>
				</div>

				<!-- Subworld Info -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Subworld</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>Mode</span><span class="text-yellow-300">{data.subworld.mode}</span>
						<span>Map size</span><span class="text-yellow-300">{data.subworld.mapSize}&times;{data.subworld.mapSize}</span>
						<span>Seed</span><span class="text-yellow-300">{data.subworld.seed}</span>
						<span>Entities</span><span class="text-yellow-300">{data.subworld.entities.length}</span>
						<span>Friendlies</span><span class="text-green-400">{data.subworld.friendlies}</span>
						<span>Enemies</span><span class="text-red-400">{data.subworld.enemies}</span>
					</div>
				</div>

				<!-- Combat Stats -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Combat Stats</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>HP Regen</span><span class="text-yellow-300">{data.gState.player.combatStats.hpRegen.toFixed(1)}/s</span>
						<span>MP Regen</span><span class="text-yellow-300">{data.gState.player.combatStats.mpRegen.toFixed(1)}/s</span>
						<span>SP Regen</span><span class="text-yellow-300">{data.gState.player.combatStats.spRegen.toFixed(1)}/s</span>
					</div>
				</div>

			{:else}
				<!-- Performance -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Performance</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>FPS</span><span class="text-yellow-300">{data.fps.toFixed(0)}</span>
						<span>Frame dt</span><span class="text-yellow-300">{data.frameDt.toFixed(1)} ms</span>
						<span>Sim speed</span><span class="text-yellow-300">{data.simSpeed}x</span>
						<span>DPR</span><span class="text-yellow-300">{data.dpr.toFixed(2)}</span>
						<span>Canvas</span><span class="text-yellow-300">{data.canvasW}&times;{data.canvasH}</span>
					</div>
				</div>

				<!-- Player -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Player</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>Logical pos</span><span class="text-yellow-300">{data.gState.player.x}, {data.gState.player.y}</span>
						<span>Visual pos</span><span class="text-yellow-300">{data.visualPlayerX.toFixed(1)}, {data.visualPlayerY.toFixed(1)}</span>
						<span>Gold</span><span class="text-yellow-300">{data.gState.player.gold}</span>
						<span>HP</span><span class="text-yellow-300">{data.gState.player.combatStats.currentHp}/{data.gState.player.combatStats.maxHp}</span>
						<span>Level</span><span class="text-yellow-300">{data.gState.player.levelData.level}</span>
					</div>
				</div>

				<!-- Camera / Map -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Camera / Map</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>Camera</span><span class="text-yellow-300">{data.cameraX.toFixed(4)}, {data.cameraY.toFixed(4)}</span>
						<span>Zoom</span><span class="text-yellow-300">{data.zoom.toFixed(1)} tiles</span>
						<span>Map size</span><span class="text-yellow-300">{data.mapW}&times;{data.mapH}</span>
						<span>In city</span><span class="text-yellow-300">{data.inCity}</span>
					</div>
				</div>

				<!-- World -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">World</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>Settlements</span><span class="text-yellow-300">{data.gState.settlements.length}</span>
						<span>Trees</span><span class="text-yellow-300">{data.trees.length}</span>
						<span>Day</span><span class="text-yellow-300">{data.gState.worldTime.day}</span>
						<span>Time</span><span class="text-yellow-300">{String(data.gState.worldTime.hour).padStart(2, '0')}:{String(data.gState.worldTime.minute).padStart(2, '0')}</span>
						<span>Seed</span><span class="text-yellow-300">{data.gState.seed}</span>
					</div>
				</div>

				<!-- Rendering -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Rendering</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>Atlas loaded</span><span class={atlas ? 'text-green-400' : 'text-red-400'}>{atlas ? 'yes' : 'no'}</span>
						<span>Atlas uploaded</span><span class={data.atlasUploaded ? 'text-green-400' : 'text-red-400'}>{data.atlasUploaded ? 'yes' : 'no'}</span>
						{#if atlas}
							<span>Atlas size</span><span class="text-yellow-300">{atlas.atlasWidth}&times;{atlas.atlasHeight}</span>
							<span>Sheets</span><span class="text-yellow-300">{atlas.sheetCount}</span>
							<span>Entries</span><span class="text-yellow-300">{atlas.entryCount}</span>
						{/if}
					</div>
				</div>

				<!-- Nearest settlement -->
				{#if nearestSettlement}
					<div class="space-y-0.5">
						<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Nearest Settlement</div>
						<div class="grid grid-cols-2 gap-x-4">
							<span>Name</span><span class="text-yellow-300">{nearestSettlement.name}</span>
							<span>Position</span><span class="text-yellow-300">{nearestSettlement.x}, {nearestSettlement.y}</span>
							<span>Distance</span><span class="text-yellow-300">{nearestSettlement.dist.toFixed(1)} tiles</span>
						</div>
					</div>
				{/if}

			{/if}

			{:else if selectedTab === 'entities' && data.subworld}
				<!-- Subworld Entity Summary -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Entity Summary</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>Total</span><span class="text-yellow-300">{data.subworld.entities.length}</span>
						<span>Alive NPCs</span><span class="text-yellow-300">{data.subworld.entities.filter(e => e.kind === 'npc' && (e.hp ?? 0) > 0).length}</span>
						<span>Friendlies</span><span class="text-green-400">{data.subworld.friendlies}</span>
						<span>Enemies</span><span class="text-red-400">{data.subworld.enemies}</span>
					</div>
				</div>

				<!-- Nearby entities -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">
						Nearby Entities
					</div>
					{#if subworldNearby.length === 0}
						<div class="text-gray-500 italic">No living entities nearby</div>
					{/if}
					<div class="max-h-64 overflow-y-auto space-y-0.5" style="scrollbar-width: none;">
						{#each subworldNearby as ent}
							<div class="flex w-full items-center justify-between rounded px-1.5 py-0.5 text-left hover:bg-green-900/30 transition">
								<span>
									<span class="text-cyan-300">{ent.label ?? ent.kind}</span>
									{#if ent.factionId}
										<span class="text-gray-500">({ent.factionId})</span>
									{/if}
								</span>
								<span class="text-gray-400">
									{ent.x.toFixed(0)},{ent.y.toFixed(0)}
									{#if ent.hp !== undefined}
										<span class="text-red-300 ml-1">{ent.hp}/{ent.maxHp ?? '?'}</span>
									{/if}
									<span class="text-gray-600 ml-1">d={ent.dist.toFixed(0)}</span>
								</span>
							</div>
						{/each}
					</div>
				</div>

			{:else if selectedTab === 'npcs'}
				<!-- NPC Summary -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">NPC Summary</div>
					<div class="grid grid-cols-2 gap-x-4">
						<span>Total (world)</span><span class="text-yellow-300">{data.npcs.length}</span>
						<span>Total (city)</span><span class="text-yellow-300">{data.cityNpcs.length}</span>
						<span>Active list</span><span class="text-yellow-300">{activeNpcs.length} ({data.inCity ? 'city' : 'world'})</span>
						<span>Alive</span><span class="text-yellow-300">{aliveNpcs.length}</span>
						<span>Nearby (&lt;30)</span><span class="text-yellow-300">{nearbyNpcs.length}</span>
						<span>In viewport</span><span class="text-yellow-300">{visibleNpcs.length}</span>
					</div>
				</div>

				<!-- Nearby NPC list -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">
						Nearby NPCs ({nearbyNpcs.length})
					</div>
					{#if nearbyNpcs.length === 0}
						<div class="text-gray-500 italic">No NPCs within 30 tiles</div>
					{/if}
					<div class="max-h-48 overflow-y-auto space-y-0.5" style="scrollbar-width: none;">
						{#each nearbyNpcs.slice(0, 50) as npc}
							<button
								onclick={() => teleportToNpc(npc)}
								class="flex w-full items-center justify-between rounded px-1.5 py-0.5 text-left hover:bg-green-900/30 transition"
							>
								<span>
									<span class="text-cyan-300">{npc.name}</span>
									<span class="text-gray-500">({npcTypeNames[npc.type] ?? '?'})</span>
								</span>
								<span class="text-gray-400">
									{npc.x},{npc.y}
									<span class="text-gray-600 ml-1">d={Math.sqrt((npc.x - data.gState.player.x) ** 2 + (npc.y - data.gState.player.y) ** 2).toFixed(0)}</span>
								</span>
							</button>
						{/each}
					</div>
				</div>

				<!-- Settlement list (first 10) -->
				<div class="space-y-0.5">
					<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">
						Settlements (nearest 10)
					</div>
					<div class="max-h-36 overflow-y-auto space-y-0.5" style="scrollbar-width: none;">
						{#each data.gState.settlements
							.map(s => ({...s, dist: Math.sqrt((s.x - data.gState.player.x) ** 2 + (s.y - data.gState.player.y) ** 2)}))
							.sort((a, b) => a.dist - b.dist)
							.slice(0, 10) as s}
							<button
								onclick={() => teleportToSettlement(s)}
								class="flex w-full items-center justify-between rounded px-1.5 py-0.5 text-left hover:bg-green-900/30 transition"
							>
								<span class="text-cyan-300">{s.name}</span>
								<span class="text-gray-400">{s.x},{s.y} <span class="text-gray-600">d={s.dist.toFixed(0)}</span></span>
							</button>
						{/each}
					</div>
				</div>

			{:else if selectedTab === 'cheats'}
				<div class="space-y-3">
					<!-- Teleport (macro only) -->
					{#if onTeleport}
						<div class="space-y-1">
							<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Teleport</div>
							<div class="flex gap-1">
								<input
									bind:value={teleportX}
									placeholder="X"
									class="w-16 rounded bg-gray-900 border border-green-800/40 px-1.5 py-0.5 text-green-300 placeholder:text-gray-600"
								/>
								<input
									bind:value={teleportY}
									placeholder="Y"
									class="w-16 rounded bg-gray-900 border border-green-800/40 px-1.5 py-0.5 text-green-300 placeholder:text-gray-600"
								/>
								<button onclick={handleTeleport} class="rounded bg-green-800/50 px-2 py-0.5 hover:bg-green-700/50 transition">Go</button>
							</div>
							{#if nearestSettlement}
								<button
									onclick={() => teleportToSettlement(nearestSettlement)}
									class="rounded bg-green-800/30 px-2 py-0.5 hover:bg-green-700/40 transition text-[10px]"
								>Teleport to nearest settlement ({nearestSettlement.name})</button>
							{/if}
						</div>
					{/if}

					<!-- Gold (macro only) -->
					{#if onSetGold}
						<div class="space-y-1">
							<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Set Gold</div>
							<div class="flex gap-1">
								<input
									bind:value={goldAmount}
									class="w-20 rounded bg-gray-900 border border-green-800/40 px-1.5 py-0.5 text-green-300"
								/>
								<button onclick={() => onSetGold(Number.parseInt(goldAmount, 10) || 0)} class="rounded bg-yellow-800/50 px-2 py-0.5 hover:bg-yellow-700/50 transition">Set</button>
							</div>
							<div class="flex gap-1">
								<button onclick={() => onSetGold(data.gState.player.gold + 1000)} class="rounded bg-yellow-800/30 px-2 py-0.5 hover:bg-yellow-700/40 transition text-[10px]">+1000</button>
								<button onclick={() => onSetGold(data.gState.player.gold + 10_000)} class="rounded bg-yellow-800/30 px-2 py-0.5 hover:bg-yellow-700/40 transition text-[10px]">+10000</button>
							</div>
						</div>
					{/if}

					<!-- Heal -->
					<div class="space-y-1">
						<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Health</div>
						<button onclick={onHealPlayer} class="rounded bg-red-800/40 px-2 py-0.5 hover:bg-red-700/50 transition">Full Heal (HP/MP/SP)</button>
					</div>

					<!-- Kill all enemies (subworld only) -->
					{#if onKillAllEnemies}
						<div class="space-y-1">
							<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Combat</div>
							<button onclick={onKillAllEnemies} class="rounded bg-red-800/40 px-2 py-0.5 hover:bg-red-700/50 transition">Kill All Enemies</button>
						</div>
					{/if}

					<!-- Spells -->
					{#if onLearnAllSpells}
						<div class="space-y-1">
							<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Spells</div>
							<button onclick={onLearnAllSpells} class="rounded bg-purple-800/40 px-2 py-0.5 hover:bg-purple-700/50 transition">Learn All Spells</button>
							<div class="text-gray-500 text-[9px]">Known: {data.gState.player.spellBook.learned.length} spell(s)</div>
						</div>
					{/if}

					<!-- Experience -->
					{#if onAddExp}
						<div class="space-y-1">
							<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Experience</div>
							<div class="flex gap-1">
								<input
									bind:value={expAmount}
									class="w-20 rounded bg-gray-900 border border-green-800/40 px-1.5 py-0.5 text-green-300"
								/>
								<button onclick={() => onAddExp(Number.parseInt(expAmount, 10) || 0)} class="rounded bg-blue-800/50 px-2 py-0.5 hover:bg-blue-700/50 transition">Add</button>
							</div>
							<div class="flex gap-1">
								<button onclick={() => onAddExp(100)} class="rounded bg-blue-800/30 px-2 py-0.5 hover:bg-blue-700/40 transition text-[10px]">+100</button>
								<button onclick={() => onAddExp(1000)} class="rounded bg-blue-800/30 px-2 py-0.5 hover:bg-blue-700/40 transition text-[10px]">+1000</button>
								<button onclick={() => onAddExp(10_000)} class="rounded bg-blue-800/30 px-2 py-0.5 hover:bg-blue-700/40 transition text-[10px]">+10000</button>
							</div>
							<div class="text-gray-500 text-[9px]">Lv {data.gState.player.levelData.level} &mdash; {data.gState.player.levelData.exp}/{data.gState.player.levelData.expToNext} EXP</div>
						</div>
					{/if}

					<!-- Speed (macro only) -->
					{#if onSetSpeed}
						<div class="space-y-1">
							<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Sim Speed</div>
							<div class="flex gap-1">
								{#each [0, 1, 2, 5, 10] as s}
									<button
										onclick={() => onSetSpeed(s)}
										class="rounded px-2 py-0.5 transition text-[10px]
											{data.simSpeed === s ? 'bg-cyan-700 text-white' : 'bg-gray-800 hover:bg-gray-700'}"
									>{s}x</button>
								{/each}
							</div>
						</div>
					{/if}

					<!-- Zoom (macro only) -->
					{#if onSetZoom}
						<div class="space-y-1">
							<div class="text-green-500 font-bold text-[10px] uppercase tracking-widest">Zoom</div>
							<div class="flex gap-1">
								<input
									bind:value={zoomValue}
									class="w-16 rounded bg-gray-900 border border-green-800/40 px-1.5 py-0.5 text-green-300"
								/>
								<button onclick={() => onSetZoom(Number.parseFloat(zoomValue) || 40)} class="rounded bg-green-800/50 px-2 py-0.5 hover:bg-green-700/50 transition">Set</button>
							</div>
							<div class="flex gap-1">
								{#each [20, 40, 80, 120, 200] as z}
									<button
										onclick={() => {
											onSetZoom(z);
											zoomValue = String(z);
										}}
										class="rounded bg-gray-800 px-2 py-0.5 hover:bg-gray-700 transition text-[10px]"
									>{z}</button>
								{/each}
							</div>
						</div>
					{/if}
				</div>

			{:else if selectedTab === 'journal'}
				<div class="space-y-2">
					<div class="flex gap-1 border-b border-green-800/30 pb-1">
						{#each ['all', 'combat', 'economy', 'politics'] as f}
							<button
								onclick={() => {
									logFilter = f as any;
								}}
								class="px-2 py-0.5 text-[9px] uppercase rounded transition {logFilter === f ? 'bg-green-700 text-white' : 'text-green-600 hover:text-green-400'}"
							>{f}</button>
						{/each}
					</div>
					<div class="flex flex-col gap-1 max-h-64 overflow-y-auto" style="scrollbar-width: none;">
						{#each data.gState.player.eventLog.filter(l => logFilter === 'all' || l.type === logFilter).reverse() as log}
							{@const logStyle = ({combat: 'border-red-500 text-red-200', economy: 'border-yellow-500 text-yellow-200', politics: 'border-purple-500 text-purple-200'} as Record<string, string>)[log.type] ?? 'border-gray-500 text-gray-300'}
							<div class="text-[10px] border-l-2 pl-2 py-0.5 {logStyle}">
								<span class="opacity-50 mr-1">[Day {log.day}]</span>
								{log.message}
							</div>
						{/each}
						{#if data.gState.player.eventLog.length === 0}
							<div class="text-gray-500 italic">No events recorded yet.</div>
						{/if}
					</div>
				</div>
			{/if}
		</div>
	</div>
</div>
