<script lang="ts">
	import type {PlayerState} from '../game/state';
	import {NPCType} from '../game/npc';
	import {calculateDerived, expFromFight, tryLevelUp} from '../game/attributes';
	import {addItem, makePotion, makeGem, makeBread} from '../game/items';

	type Props = {
		player: PlayerState;
		enemyName: string;
		enemyType: NPCType;
		enemyLevel: number;
		onEnd: (victory: boolean, loot?: {gold: number}) => void;
	};

	let {player = $bindable(), enemyName, enemyType, enemyLevel, onEnd}: Props = $props();

	const ENEMY_SPRITES: Record<number, string> = {
		[NPCType.Peasant]: '/assets/sprites/peasant_256.png',
		[NPCType.Woodcutter]: '/assets/sprites/peasant_256.png',
		[NPCType.Merchant]: '/assets/sprites/corovan_256.png',
		[NPCType.Caravan]: '/assets/sprites/corovan_256.png',
		[NPCType.Bandit]: '/assets/sprites/imp_golem_256.png',
		[NPCType.Guard]: '/assets/sprites/peasant_256.png',
		[NPCType.Witch]: '/assets/sprites/witch_256.png',
		[NPCType.Sorceress]: '/assets/sprites/witch_256.png',
	};
	const enemySprite = ENEMY_SPRITES[enemyType] ?? '/assets/sprites/peasant_256.png';

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

	// Escape always succeeds
	let escapeChance = $derived(() => 100);

	function calcDodge(defAgi: number, atkAgi: number): number {
		return defAgi / (defAgi + atkAgi + 100);
	}

	function calcCrit(atkLck: number, defLck: number): number {
		return atkLck / (atkLck + defLck + 100);
	}

	function applyDamageToEnemy(baseDmg: number, label: string) {
		if (!playerTurn || battleEnded || turnDelay) {
			return;
		}

		const dodgeChance = calcDodge(enemyAgi, player.attributes.agi);
		if (Math.random() < dodgeChance) {
			battleLog = `${enemyName} dodged your ${label}!`;
			endPlayerTurn();
			return;
		}

		const critChance = calcCrit(player.attributes.lck, enemyLck);
		const isCrit = Math.random() < critChance;
		let damage = Math.floor(baseDmg * derived.physDamageMult);
		if (isCrit) {
			damage *= 2;
			battleLog = `Critical ${label}! ${damage} damage!`;
		} else {
			battleLog = `${label} deals ${damage} damage.`;
		}

		enemyHp -= damage;
		if (enemyHp <= 0) {
			enemyHp = 0;
			handleVictory();
			return;
		}

		endPlayerTurn();
	}

	function punch() {
		applyDamageToEnemy(12, 'Punch');
	}

	function wait() {
		if (!playerTurn || battleEnded || turnDelay) {
			return;
		}

		// Recover a bit of HP
		const heal = Math.min(5, player.combatStats.maxHp - player.combatStats.currentHp);
		if (heal > 0) {
			player.combatStats.currentHp += heal;
		}

		battleLog = `You brace yourself. (+${heal} HP)`;
		endPlayerTurn();
	}

	function tease() {
		if (!playerTurn || battleEnded || turnDelay) {
			return;
		}

		// Tease: lower enemy stats slightly, small damage
		const dodgeChance = calcDodge(enemyAgi, player.attributes.cha + player.attributes.agi);
		if (Math.random() < dodgeChance) {
			battleLog = `${enemyName} ignores your tease.`;
			endPlayerTurn();
			return;
		}

		// Reduce enemy agi/str slightly
		if (enemyAgi > 1) {
			enemyAgi = Math.max(1, enemyAgi - 1);
		}

		if (enemyStr > 1) {
			enemyStr = Math.max(1, enemyStr - 1);
		}

		const damage = Math.floor(3 * derived.physDamageMult);
		enemyHp -= damage;
		battleLog = `Tease hits for ${damage}. Enemy weakened!`;

		if (enemyHp <= 0) {
			enemyHp = 0;
			handleVictory();
			return;
		}

		endPlayerTurn();
	}

	function handleVictory() {
		battleEnded = true;
		const exp = expFromFight(enemyLevel, 1);
		player.levelData.exp += exp;
		let msg = `Victory! +${exp} XP.`;
		while (tryLevelUp(player.levelData)) {
			msg += ' Level up!';
		}

		battleLog = msg;
		showPostBattle = true;
	}

	function spare() {
		// Mercy: small reputation boost
		player.reputation.Wilderness = (player.reputation.Wilderness ?? 0) + 3;
		battleLog = 'You spare the defeated. (+3 rep)';
		setTimeout(() => onEnd(true), 600);
	}

	function loot() {
		// Rob: get gold + random item, reputation hit
		const goldLoot = 10 + enemyLevel * 5 + Math.floor(Math.random() * 20);
		player.gold += goldLoot;
		player.reputation.Wilderness = (player.reputation.Wilderness ?? 0) - 5;

		// Random loot item
		const roll = Math.random();
		if (roll < 0.3) {
			addItem(player.inventory, makePotion());
		} else if (roll < 0.5) {
			addItem(player.inventory, makeGem());
		} else if (roll < 0.7) {
			addItem(player.inventory, makeBread(2));
		}

		battleLog = `Looted ${goldLoot} gold! (-5 rep)`;
		setTimeout(() => onEnd(true, {gold: goldLoot}), 600);
	}

	function abuse() {
		// Theater/Abuse: big reputation hit, extra gold
		const goldLoot = 20 + enemyLevel * 10 + Math.floor(Math.random() * 30);
		player.gold += goldLoot;
		player.reputation.Wilderness = (player.reputation.Wilderness ?? 0) - 15;
		battleLog = `Intimidation yields ${goldLoot} gold! (-15 rep)`;
		setTimeout(() => onEnd(true, {gold: goldLoot}), 600);
	}

	function endPlayerTurn() {
		playerTurn = false;
		turnDelay = true;
		setTimeout(() => {
			enemyAttack();
			turnDelay = false;
		}, 700);
	}

	function enemyAttack() {
		if (battleEnded) {
			return;
		}

		const dodgeChance = calcDodge(player.attributes.agi, enemyAgi);
		if (Math.random() < dodgeChance) {
			battleLog = 'You dodged the enemy attack!';
			playerTurn = true;
			return;
		}

		const critChance = calcCrit(enemyLck, player.attributes.lck);
		const isCrit = Math.random() < critChance;
		const enemyDmgMult = 1 + enemyStr * 0.01;
		let damage = Math.floor(10 * enemyDmgMult);
		if (isCrit) {
			damage *= 2;
			battleLog = `${enemyName} critical hit! ${damage} damage!`;
		} else {
			battleLog = `${enemyName} attacks for ${damage}.`;
		}

		player.combatStats.currentHp -= damage;
		if (player.combatStats.currentHp <= 0) {
			player.combatStats.currentHp = 1;
			battleLog = 'Defeat... You collapse.';
			battleEnded = true;
			setTimeout(() => onEnd(false), 1200);
			return;
		}

		playerTurn = true;
	}

	function attemptRun() {
		if (!playerTurn || battleEnded || turnDelay) {
			return;
		}

		const chance = escapeChance();
		if (Math.random() * 100 < chance) {
			battleLog = 'You escaped!';
			battleEnded = true;
			setTimeout(() => onEnd(false), 600);
			return;
		}

		battleLog = `Run failed! (${chance}%)`;
		endPlayerTurn();
	}

	let playerHpPct = $derived(
		Math.max(0, player.combatStats.currentHp / player.combatStats.maxHp * 100),
	);
	let enemyHpPct = $derived(Math.max(0, enemyHp / enemyMaxHp * 100));
	let canAct = $derived(playerTurn && !turnDelay && !battleEnded);
</script>

<div class="absolute inset-0 flex items-center justify-center bg-black/85">
	<div class="flex h-[600px] w-[820px] flex-col rounded-lg border-2 border-amber-900/60 bg-gray-950/95 font-sans shadow-2xl">
		<!-- Top: Large combatant sprites with HP bars underneath -->
		<div class="flex items-end justify-center gap-6 border-b border-gray-800 px-6 pb-3 pt-4">
			<!-- Player side -->
			<div class="flex flex-col items-center gap-1">
				<img src="/assets/sprites/player.png" alt="Player" class="h-[192px] w-[192px] rounded-lg border-2 border-gray-700 bg-gray-900/50 object-contain" style="image-rendering:pixelated" />
				<div class="w-[192px]">
					<div class="mb-0.5 text-center text-xs font-bold text-gray-300">Player</div>
					<div class="h-3 w-full overflow-hidden rounded-full bg-gray-800">
						<div class="h-full rounded-full bg-red-600 transition-all" style="width:{playerHpPct}%"></div>
					</div>
					<div class="mt-0.5 text-center text-[10px] text-red-400">{player.combatStats.currentHp}/{player.combatStats.maxHp}</div>
				</div>
			</div>
			<!-- VS badge -->
			<div class="mb-16 text-2xl font-black text-amber-500">VS</div>
			<!-- Enemy side -->
			<div class="flex flex-col items-center gap-1">
				<img src={enemySprite} alt={enemyName} class="h-[192px] w-[192px] rounded-lg border-2 border-gray-700 bg-gray-900/50 object-contain" style="image-rendering:pixelated" />
				<div class="w-[192px]">
					<div class="mb-0.5 text-center text-xs font-bold text-gray-300">{enemyName} Lv.{enemyLevel}</div>
					<div class="h-3 w-full overflow-hidden rounded-full bg-gray-800">
						<div class="h-full rounded-full bg-orange-600 transition-all" style="width:{enemyHpPct}%"></div>
					</div>
					<div class="mt-0.5 text-center text-[10px] text-orange-400">{enemyHp}/{enemyMaxHp}</div>
				</div>
			</div>
		</div>

		<!-- Battle log + turn indicator (fixed height) -->
		<div class="flex h-14 flex-col items-center justify-center border-b border-gray-800 px-4">
			<p class="text-center text-sm text-cyan-300">{battleLog}</p>
			<p class="h-4 text-center text-xs text-gray-500">{!playerTurn && !battleEnded && !showPostBattle ? 'Enemy turn...' : ''}</p>
		</div>

		<!-- Actions area (fixed height, always present) -->
		<div class="flex flex-1 flex-col justify-center gap-2 px-8 py-3">
			{#if showPostBattle}
				<button onclick={spare} class="rounded border border-green-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white hover:bg-green-900/60">Spare (Mercy) — +3 rep</button>
				<button onclick={loot} class="rounded border border-yellow-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white hover:bg-yellow-900/60">Loot (Rob) — gold, -5 rep</button>
				<button onclick={abuse} class="rounded border border-red-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white hover:bg-red-900/60">Abuse (Theater) — more gold, -15 rep</button>
			{:else if battleEnded}
				<div class="text-center text-sm text-gray-500">Battle ended...</div>
			{:else}
				<div class="grid grid-cols-2 gap-2">
					<button onclick={punch} disabled={!canAct} class="rounded border border-cyan-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white transition {canAct ? 'hover:bg-cyan-900/40' : 'opacity-40 cursor-not-allowed'}">Punch</button>
					<button onclick={wait} disabled={!canAct} class="rounded border border-cyan-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white transition {canAct ? 'hover:bg-cyan-900/40' : 'opacity-40 cursor-not-allowed'}">Wait (heal)</button>
					<button onclick={tease} disabled={!canAct} class="rounded border border-cyan-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white transition {canAct ? 'hover:bg-cyan-900/40' : 'opacity-40 cursor-not-allowed'}">Tease (debuff)</button>
					<button onclick={attemptRun} disabled={!canAct} class="rounded border border-amber-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white transition {canAct ? 'hover:bg-amber-900/40' : 'opacity-40 cursor-not-allowed'}">Run ({escapeChance()}%)</button>
				</div>
			{/if}
		</div>
	</div>
</div>
