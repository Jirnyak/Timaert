<script lang="ts">
	import {onMount} from 'svelte';
	import {fly} from 'svelte/transition';
	import type {PlayerState} from '../game/state';
	import {NPCType} from '../game/npc';
	import {calculateDerived, expFromFight, tryLevelUp} from '../game/attributes';
	import {addItem, makePotion, makeGem} from '../game/items';
	import {MonsterGenerator} from '../game/monster-generator';
	import {color, panelStyle, dividerStyle, accentHeadingStyle, mutedStyle, btnProps, btnPropsIf, btnStyle, barTrackStyle, barFillStyle, messageStyle} from '../ui/theme';

	type Props = {
		player: PlayerState;
		enemyName: string;
		enemyType: NPCType;
		enemyLevel: number;
		enemyTraits: string[];
		onEnd: (victory: boolean, loot?: {gold: number}) => void;
	};

	let {player = $bindable(), enemyName, enemyType, enemyLevel, enemyTraits = [], onEnd}: Props = $props();

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
		let isCrit = Math.random() < critChance;
		if (enemyTraits.includes('Brave')) isCrit = Math.random() < (critChance * 1.5);

		let damage = Math.floor(baseDmg * derived.physDamageMult * (0.8 + Math.random() * 0.4));
		const finalDamage = isCrit ? damage * 2 : damage;

		enemyHp -= finalDamage;
		enemyShake = true;
		spawnDamageNumber(finalDamage, true);
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
		let isCrit = Math.random() < critChance;
		if (enemyTraits.includes('Brave')) isCrit = Math.random() < (critChance * 1.5);
		
		let damage = Math.floor((10 + enemyStr * 1.5) * (0.8 + Math.random() * 0.4));
		if (enemyTraits.includes('Aggressive')) damage = Math.floor(damage * 1.25);
		if (enemyTraits.includes('Cowardly')) damage = Math.floor(damage * 0.8);
		
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
		const chance = 30 + player.attributes.agi * 2 + player.attributes.spd;
		if (Math.random() * 100 < chance) {
			battleLog = 'You managed to escape!';
			battleEnded = true;
			setTimeout(() => onEnd(false), 600);
		} else {
			battleLog = 'Failed to escape!';
			endPlayerTurn();
		}
	}

	let playerHpPct = $derived(Math.max(0, (player.combatStats.currentHp / player.combatStats.maxHp) * 100));
	let enemyHpPct = $derived(Math.max(0, (enemyHp / enemyMaxHp) * 100));
	let canAct = $derived(playerTurn && !turnDelay && !battleEnded);
</script>

<div class="absolute inset-0 flex items-center justify-center overflow-hidden" style="background: linear-gradient(to bottom, {color.backdropHeavy}, {color.backdropHeavy});">
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

	<div class="flex h-[620px] w-[860px] flex-col rounded-xl border-4 font-sans" style={panelStyle('large')}>
		<div class="flex items-end justify-center gap-10 border-b px-8 pb-6 pt-8" style={dividerStyle}>
			<div class="flex flex-col items-center gap-3 transition-all duration-100 {playerFlash ? 'brightness-200 scale-105' : ''}">
				<div class="relative h-[220px] w-[220px] rounded-2xl border-2 p-4" style="border-color: {color.divider}; background: {color.innerPanelBg};">
					<img src="/assets/sprites/player.png" alt="Player" class="h-full w-full object-contain" style="image-rendering:pixelated" />
				</div>
				<div class="w-full">
					<div class="flex justify-between text-[10px] uppercase tracking-widest font-bold mb-1" style="color: {color.hp};">
						<span>Health</span>
						<span>{player.combatStats.currentHp} / {player.combatStats.maxHp}</span>
					</div>
					<div class="h-3 w-full overflow-hidden rounded-full border" style={barTrackStyle}>
						<div class="h-full bg-gradient-to-r transition-all duration-300" style={barFillStyle(playerHpPct)}></div>
					</div>
				</div>
			</div>

			<div class="mb-24 text-4xl font-black italic tracking-tighter" style="color: {color.accent}; text-shadow: 0 2px 4px rgba(0,0,0,0.3);">VS</div>

			<div class="flex flex-col items-center gap-3 transition-transform duration-75 {enemyShake ? 'animate-shake' : ''}">
				<div class="relative h-[220px] w-[220px] rounded-2xl border-2 p-2 overflow-hidden flex items-center justify-center" style="border-color: {color.divider}; background: {color.innerPanelBg};">
					{#if enemyCanvasUrl}
						<img src={enemyCanvasUrl} alt="Procedural Enemy" class="h-full w-full object-contain scale-125" />
					{:else}
						<div class="text-xs tracking-widest font-bold uppercase animate-pulse" style={mutedStyle}>Summoning...</div>
					{/if}
				</div>
				<div class="w-full">
					<div class="flex justify-between text-[10px] uppercase tracking-widest font-bold mb-1" style="color: {color.accent};">
						<div class="flex flex-col">
							<span>{enemyName} Lv.{enemyLevel}</span>
							<div class="flex gap-1 mt-0.5">
								{#each enemyTraits as trait}
									<span class="text-[8px] px-1 bg-[{color.barTrack}]/20 rounded border border-[{color.divider}]/30">{trait}</span>
								{/each}
							</div>
						</div>
						<span>{enemyHp} / {enemyMaxHp}</span>
					</div>
					<div class="h-3 w-full overflow-hidden rounded-full border" style={barTrackStyle}>
						<div class="h-full bg-gradient-to-r transition-all duration-300" style={barFillStyle(enemyHpPct, color.enemyHpFill)}></div>
					</div>
				</div>
			</div>
		</div>

		<div class="flex h-20 flex-col items-center justify-center border-b px-10" style="background: {color.messageBg}; {dividerStyle}">
			<p class="text-center text-base font-bold tracking-wide drop-shadow-sm" style="color: {color.heading};">{battleLog}</p>
			{#if !playerTurn && !battleEnded && !showPostBattle}
				<p class="mt-1 text-[10px] uppercase tracking-[0.2em] animate-pulse" style={mutedStyle}>Enemy is thinking...</p>
			{/if}
		</div>

		<div class="flex flex-1 flex-col justify-center gap-3 px-12 py-4">
			{#if showPostBattle}
				<div class="flex flex-col gap-2">
					<button onclick={spare} class="group relative overflow-hidden rounded-lg border-2 px-6 py-4 text-sm font-black uppercase tracking-widest transition" {...btnProps('success')}>
						Spare (Mercy) <span class="block text-[10px] font-normal normal-case" style="color: #4a6a4a;">+5 Reputation</span>
					</button>
					<button onclick={loot} class="group relative overflow-hidden rounded-lg border-2 px-6 py-4 text-sm font-black uppercase tracking-widest transition" {...btnProps('primary')}>
						Loot (Rob) <span class="block text-[10px] font-normal normal-case" style="color: #5a3a1a;">Take gold and items, -3 Rep</span>
					</button>
					<button onclick={abuse} class="group relative overflow-hidden rounded-lg border-2 px-6 py-4 text-sm font-black uppercase tracking-widest transition" style="{btnStyle('danger')} color: #2a0a0a;" onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d87a7a, #b85a5a)'} onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c86a6a, #a84a4a)'}>
						Abuse (Intimidate) <span class="block text-[10px] font-normal normal-case" style="color: #5a2a2a;">Maximum gold, -15 Rep</span>
					</button>
				</div>
			{:else if battleEnded}
				<div class="text-center text-sm font-bold uppercase tracking-widest animate-pulse" style={mutedStyle}>Ending combat...</div>
			{:else}
				<div class="grid grid-cols-2 gap-3">
					<button onclick={punch} disabled={!canAct} class="rounded-lg border-2 px-6 py-4 text-sm font-black uppercase tracking-widest transition {canAct ? 'active:scale-95' : 'opacity-20 cursor-not-allowed'}" {...btnPropsIf('combat', canAct)}>Punch</button>
					<button onclick={wait} disabled={!canAct} class="rounded-lg border-2 px-6 py-4 text-sm font-black uppercase tracking-widest transition {canAct ? 'active:scale-95' : 'opacity-20 cursor-not-allowed'}" {...btnPropsIf('combat', canAct)}>Wait</button>
					<button onclick={tease} disabled={!canAct} class="rounded-lg border-2 px-6 py-4 text-sm font-black uppercase tracking-widest transition {canAct ? 'active:scale-95' : 'opacity-20 cursor-not-allowed'}" {...btnPropsIf('purple', canAct)}>Mock</button>
					<button onclick={attemptRun} disabled={!canAct} class="rounded-lg border-2 px-6 py-4 text-sm font-black uppercase tracking-widest transition {canAct ? 'active:scale-95' : 'opacity-20 cursor-not-allowed'}" {...btnPropsIf('escape', canAct)}>Run</button>
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
