<script lang="ts">
	import {onMount} from 'svelte';
	import {fly} from 'svelte/transition';
	import type {PlayerState} from '../game/state';
	import {NPCType} from '../game/npc';
	import {calculateDerived, expFromFight, tryLevelUp} from '../game/attributes';
	import {addItem, makePotion, makeGem, makeBread} from '../game/items';
	import {MonsterGenerator} from '../game/monster-generator';

	type Props = {
		player: PlayerState;
		enemyName: string;
		enemyType: NPCType;
		enemyLevel: number;
		onEnd: (victory: boolean, loot?: {gold: number}) => void;
	};

	let {player = $bindable(), enemyName, enemyType, enemyLevel, onEnd}: Props = $props();

	let enemyCanvasUrl = $state<string | undefined>(undefined);
	let enemyShake = $state(false);
	let playerFlash = $state(false);
	let damageNumbers = $state<Array<{id: number; val: number; x: number; y: number; color: string}>>([]);

	const initHp = 80 + enemyLevel * 20;
	let enemyHp = $state(initHp);
	let enemyMaxHp = initHp;
	let enemyStr = 1 + enemyLevel;
	let enemyAgi = 1 + Math.floor(enemyLevel * 0.7);
	let enemyLck = 1 + Math.floor(enemyLevel * 0.5);

	let playerTurn = $state(true);
	let battleLog = $state(`${enemyName} approaches!`);
	let battleEnded = $state(false);
	let showPostBattle = $state(false);
	let turnDelay = $state(false);

	let derived = $derived(calculateDerived(player.attributes));
	let escapeChance = $derived(() => 100);

	onMount(() => {
		const gen = new MonsterGenerator(enemyType * 1000 + enemyLevel);
		const canvas = gen.generate();
		enemyCanvasUrl = canvas.toDataURL();
	});

	function spawnDamageNumber(val: number, isEnemy: boolean, isMiss = false) {
		const id = Math.random();
		const x = isEnemy ? 550 + Math.random() * 60 : 150 + Math.random() * 60;
		const y = 220 + Math.random() * 40;
		const color = isMiss ? 'text-gray-400' : (isEnemy ? 'text-yellow-400' : 'text-red-500');
		
		damageNumbers = [...damageNumbers, {id, val, x, y, color}];
		setTimeout(() => {
			damageNumbers = damageNumbers.filter(d => d.id !== id);
		}, 800);
	}

	function calcDodge(defAgi: number, atkAgi: number): number {
		return defAgi / (defAgi + atkAgi + 100);
	}

	function calcCrit(atkLck: number, defLck: number): number {
		return atkLck / (atkLck + defLck + 100);
	}

	function applyDamageToEnemy(baseDmg: number, label: string) {
		if (!playerTurn || battleEnded || turnDelay) return;

		const dodgeChance = calcDodge(enemyAgi, player.attributes.agi);
		if (Math.random() < dodgeChance) {
			battleLog = `${enemyName} dodged your ${label}!`;
			spawnDamageNumber(0, true, true);
			endPlayerTurn();
			return;
		}

		const critChance = calcCrit(player.attributes.lck, enemyLck);
		const isCrit = Math.random() < critChance;
		let damage = Math.floor(baseDmg * derived.physDamageMult * (0.9 + Math.random() * 0.2));
		if (isCrit) {
			damage *= 2;
			battleLog = `CRITICAL ${label.toUpperCase()}! ${damage} damage!`;
		} else {
			battleLog = `${label} deals ${damage} damage.`;
		}

		enemyHp -= damage;
		enemyShake = true;
		spawnDamageNumber(damage, true);
		setTimeout(() => { enemyShake = false; }, 200);

		if (enemyHp <= 0) {
			enemyHp = 0;
			handleVictory();
			return;
		}
		endPlayerTurn();
	}

	function punch() { applyDamageToEnemy(12, 'Punch'); }

	function wait() {
		if (!playerTurn || battleEnded || turnDelay) return;
		const heal = Math.min(8, player.combatStats.maxHp - player.combatStats.currentHp);
		if (heal > 0) player.combatStats.currentHp += heal;
		battleLog = `You brace yourself and recover. (+${heal} HP)`;
		endPlayerTurn();
	}

	function tease() {
		if (!playerTurn || battleEnded || turnDelay) return;
		const dodgeChance = calcDodge(enemyAgi, player.attributes.cha + player.attributes.agi);
		if (Math.random() < dodgeChance) {
			battleLog = `${enemyName} ignores your tease.`;
			endPlayerTurn();
			return;
		}
		if (enemyAgi > 1) enemyAgi--;
		if (enemyStr > 1) enemyStr--;
		const damage = Math.floor(5 * derived.physDamageMult);
		enemyHp -= damage;
		enemyShake = true;
		spawnDamageNumber(damage, true);
		setTimeout(() => { enemyShake = false; }, 200);
		battleLog = `You mock the enemy! Their stats drop and they take ${damage} dmg.`;
		if (enemyHp <= 0) {
			enemyHp = 0;
			handleVictory();
			return;
		}
		endPlayerTurn();
	}

	function handleVictory() {
		battleEnded = true;
		const exp = expFromFight(enemyLevel, 1.2);
		player.levelData.exp += exp;
		let msg = `Victory! +${exp} XP.`;
		while (tryLevelUp(player.levelData)) {
			msg += ' LEVEL UP!';
		}
		battleLog = msg;
		showPostBattle = true;
	}

	function spare() {
		player.reputation.Wilderness = (player.reputation.Wilderness ?? 0) + 5;
		battleLog = 'You show mercy. (+5 reputation)';
		setTimeout(() => onEnd(true), 800);
	}

	function loot() {
		const goldLoot = 15 + enemyLevel * 8 + Math.floor(Math.random() * 25);
		player.gold += goldLoot;
		player.reputation.Wilderness = (player.reputation.Wilderness ?? 0) - 3;
		const roll = Math.random();
		if (roll < 0.4) addItem(player.inventory, makePotion());
		else if (roll < 0.6) addItem(player.inventory, makeGem());
		battleLog = `You take what you need. +${goldLoot}g. (-3 reputation)`;
		setTimeout(() => onEnd(true, {gold: goldLoot}), 800);
	}

	function abuse() {
		const goldLoot = 40 + enemyLevel * 15 + Math.floor(Math.random() * 50);
		player.gold += goldLoot;
		player.reputation.Wilderness = (player.reputation.Wilderness ?? 0) - 15;
		battleLog = `You intimidate the fallen! +${goldLoot}g. (-15 reputation)`;
		setTimeout(() => onEnd(true, {gold: goldLoot}), 800);
	}

	function endPlayerTurn() {
		playerTurn = false;
		turnDelay = true;
		setTimeout(() => {
			enemyAttack();
			turnDelay = false;
		}, 800);
	}

	function enemyAttack() {
		if (battleEnded) return;
		const dodgeChance = calcDodge(player.attributes.agi, enemyAgi);
		if (Math.random() < dodgeChance) {
			battleLog = 'You dodged the enemy attack!';
			spawnDamageNumber(0, false, true);
			playerTurn = true;
			return;
		}
		const critChance = calcCrit(enemyLck, player.attributes.lck);
		const isCrit = Math.random() < critChance;
		const damage = Math.floor((10 + enemyStr * 1.5) * (0.8 + Math.random() * 0.4));
		const finalDamage = isCrit ? damage * 2 : damage;
		
		player.combatStats.currentHp -= finalDamage;
		playerFlash = true;
		spawnDamageNumber(finalDamage, false);
		setTimeout(() => { playerFlash = false; }, 150);

		if (isCrit) battleLog = `${enemyName} deals a CRITICAL hit: ${finalDamage} damage!`;
		else battleLog = `${enemyName} attacks for ${finalDamage} damage.`;

		if (player.combatStats.currentHp <= 0) {
			player.combatStats.currentHp = 1;
			battleLog = 'You were defeated... everything goes dark.';
			battleEnded = true;
			setTimeout(() => onEnd(false), 1500);
			return;
		}
		playerTurn = true;
	}

	function attemptRun() {
		if (!playerTurn || battleEnded || turnDelay) return;
		if (Math.random() * 100 < 100) {
			battleLog = 'You managed to escape!';
			battleEnded = true;
			setTimeout(() => onEnd(false), 600);
		}
	}

	let playerHpPct = $derived(Math.max(0, (player.combatStats.currentHp / player.combatStats.maxHp) * 100));
	let enemyHpPct = $derived(Math.max(0, (enemyHp / enemyMaxHp) * 100));
	let canAct = $derived(playerTurn && !turnDelay && !battleEnded);
</script>

<div class="absolute inset-0 flex items-center justify-center bg-black/90 overflow-hidden">
	{#each damageNumbers as d (d.id)}
		<div 
			in:fly={{y: 20, duration: 200}} 
			out:fly={{y: -100, duration: 600}}
			class="absolute z-50 text-3xl font-black {d.color} pointer-events-none drop-shadow-[0_2px_2px_rgba(0,0,0,1)]"
			style="left: {d.x}px; top: {d.y}px;"
		>
			{d.val > 0 ? `-${d.val}` : 'MISS'}
		</div>
	{/each}

	<div class="flex h-[620px] w-[860px] flex-col rounded-xl border-2 border-amber-900/40 bg-gray-950/95 font-sans shadow-[0_0_60px_rgba(0,0,0,0.8)]">
		<div class="flex items-end justify-center gap-10 border-b border-gray-800/50 px-8 pb-6 pt-8">
			<div class="flex flex-col items-center gap-3 transition-all duration-100 {playerFlash ? 'brightness-200 scale-105' : ''}">
				<div class="relative h-[220px] w-[220px] rounded-2xl border-2 border-gray-800 bg-gray-900/40 p-4">
					<img src="/assets/sprites/player.png" alt="Player" class="h-full w-full object-contain" style="image-rendering:pixelated" />
				</div>
				<div class="w-full">
					<div class="flex justify-between text-[10px] uppercase tracking-widest font-bold text-red-400 mb-1">
						<span>Health</span>
						<span>{player.combatStats.currentHp} / {player.combatStats.maxHp}</span>
					</div>
					<div class="h-3 w-full overflow-hidden rounded-full bg-gray-900 border border-gray-800">
						<div class="h-full bg-gradient-to-r from-red-600 to-red-400 transition-all duration-300" style="width:{playerHpPct}%"></div>
					</div>
				</div>
			</div>

			<div class="mb-24 text-4xl font-black text-amber-600/80 italic tracking-tighter">VS</div>

			<div class="flex flex-col items-center gap-3 transition-transform duration-75 {enemyShake ? 'animate-shake' : ''}">
				<div class="relative h-[220px] w-[220px] rounded-2xl border-2 border-amber-900/20 bg-gray-900/40 p-2 overflow-hidden flex items-center justify-center">
					{#if enemyCanvasUrl}
						<img src={enemyCanvasUrl} alt="Procedural Enemy" class="h-full w-full object-contain scale-125" />
					{:else}
						<div class="text-gray-700 animate-pulse uppercase text-xs tracking-widest font-bold">Summoning...</div>
					{/if}
				</div>
				<div class="w-full">
					<div class="flex justify-between text-[10px] uppercase tracking-widest font-bold text-amber-500 mb-1">
						<span>{enemyName} Lv.{enemyLevel}</span>
						<span>{enemyHp} / {enemyMaxHp}</span>
					</div>
					<div class="h-3 w-full overflow-hidden rounded-full bg-gray-900 border border-gray-800">
						<div class="h-full bg-gradient-to-r from-amber-600 to-yellow-400 transition-all duration-300" style="width:{enemyHpPct}%"></div>
					</div>
				</div>
			</div>
		</div>

		<div class="flex h-20 flex-col items-center justify-center border-b border-gray-800/50 bg-black/20 px-10">
			<p class="text-center text-base font-bold tracking-wide text-cyan-300 drop-shadow-sm">{battleLog}</p>
			{#if !playerTurn && !battleEnded && !showPostBattle}
				<p class="mt-1 text-[10px] uppercase tracking-[0.2em] text-gray-500 animate-pulse">Enemy is thinking...</p>
			{/if}
		</div>

		<div class="flex flex-1 flex-col justify-center gap-3 px-12 py-4">
			{#if showPostBattle}
				<div class="flex flex-col gap-2">
					<button onclick={spare} class="group relative overflow-hidden rounded-lg border border-green-900/50 bg-green-950/30 px-6 py-4 text-sm font-black uppercase tracking-widest text-green-400 transition hover:bg-green-900/40">
						Spare (Mercy) <span class="block text-[10px] font-normal normal-case text-green-600">+5 Reputation</span>
					</button>
					<button onclick={loot} class="group relative overflow-hidden rounded-lg border border-amber-900/50 bg-amber-950/30 px-6 py-4 text-sm font-black uppercase tracking-widest text-amber-400 transition hover:bg-amber-900/40">
						Loot (Rob) <span class="block text-[10px] font-normal normal-case text-amber-600">Take gold and items, -3 Rep</span>
					</button>
					<button onclick={abuse} class="group relative overflow-hidden rounded-lg border border-red-900/50 bg-red-950/30 px-6 py-4 text-sm font-black uppercase tracking-widest text-red-400 transition hover:bg-red-900/40">
						Abuse (Intimidate) <span class="block text-[10px] font-normal normal-case text-red-600">Maximum gold, -15 Rep</span>
					</button>
				</div>
			{:else if battleEnded}
				<div class="text-center text-sm font-bold uppercase tracking-widest text-gray-600 animate-pulse">Ending combat...</div>
			{:else}
				<div class="grid grid-cols-2 gap-3">
					<button onclick={punch} disabled={!canAct} class="rounded-lg border border-cyan-900/50 bg-cyan-950/30 px-6 py-4 text-sm font-black uppercase tracking-widest text-cyan-400 transition {canAct ? 'hover:bg-cyan-900/40 active:scale-95' : 'opacity-20 cursor-not-allowed'}">Punch</button>
					<button onclick={wait} disabled={!canAct} class="rounded-lg border border-cyan-900/50 bg-cyan-950/30 px-6 py-4 text-sm font-black uppercase tracking-widest text-cyan-400 transition {canAct ? 'hover:bg-cyan-900/40 active:scale-95' : 'opacity-20 cursor-not-allowed'}">Wait</button>
					<button onclick={tease} disabled={!canAct} class="rounded-lg border border-purple-900/50 bg-purple-950/30 px-6 py-4 text-sm font-black uppercase tracking-widest text-purple-400 transition {canAct ? 'hover:bg-purple-900/40 active:scale-95' : 'opacity-20 cursor-not-allowed'}">Mock</button>
					<button onclick={attemptRun} disabled={!canAct} class="rounded-lg border border-gray-700 bg-gray-800/30 px-6 py-4 text-sm font-black uppercase tracking-widest text-gray-400 transition {canAct ? 'hover:bg-gray-700/40 active:scale-95' : 'opacity-20 cursor-not-allowed'}">Run</button>
				</div>
			{/if}
		</div>
	</div>
</div>

<style>
	@keyframes shake {
		0%, 100% { transform: translate(0, 0); }
		20% { transform: translate(-8px, 2px); }
		40% { transform: translate(8px, -2px); }
		60% { transform: translate(-8px, -2px); }
		80% { transform: translate(8px, 2px); }
	}
	.animate-shake {
		animation: shake 0.1s ease-in-out infinite;
	}
</style>
