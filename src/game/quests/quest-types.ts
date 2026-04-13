// === Quest Types — universal quest data structures ===
//
// Layer 1. Pure types, no logic. Every quest is a plain data struct —
// serializable, no closures, no methods.
//
// Quest objective types are a closed set of universal verbs:
//   visit | find | deliver | destroy | wait | interact
// Adding a new verb = one discriminant + one checker in quest-engine.

import type {NPCType} from '../npc';
import type {GameEvent} from '../event-types';

// ── Quest category ──

export type QuestCategory = 'main' | 'side' | 'procedural';

// ── Quest objectives (discriminated union) ──

export type QuestObjective =
	| {
		type: 'visit_cell';
		x: number;
		y: number;
		/** Acceptance radius in map units. */
		radius: number;
		completed: boolean;
	}
	| {
		type: 'find_location';
		/** Macroworld cell for subworld entry. */
		cellX: number;
		cellY: number;
		/** Target point inside subworld tile. */
		subX: number;
		subY: number;
		radius: number;
		completed: boolean;
	}
	| {
		type: 'deliver_items';
		itemId: string;
		quantity: number;
		targetSettlementId: number;
		completed: boolean;
	}
	| {
		type: 'destroy_npc';
		npcType: NPCType;
		count: number;
		killed: number;
		/** Zone center where targets spawn/roam. */
		zoneX: number;
		zoneY: number;
		zoneRadius: number;
		completed: boolean;
	}
	| {
		type: 'wait_at';
		x: number;
		y: number;
		radius: number;
		/** Game hours to remain in zone. */
		hoursRequired: number;
		hoursWaited: number;
		completed: boolean;
	}
	| {
		type: 'interact_cell';
		x: number;
		y: number;
		/** Action verb — matches against event payload. */
		action: string;
		completed: boolean;
	};

// ── Quest rewards (discriminated union) ──

export type QuestReward =
	| {type: 'gold'; amount: number}
	| {type: 'xp'; amount: number}
	| {type: 'item'; itemId: string; quantity: number}
	| {type: 'reputation'; faction: string; delta: number}
	| {type: 'event'; event: GameEvent};

// ── Quest (runtime instance — serializable) ──

export type Quest = {
	id: string;
	title: string;
	description: string;
	category: QuestCategory;
	/** Settlement where quest was given (for return-to logic). */
	giverSettlementId: number;
	objectives: QuestObjective[];
	rewards: QuestReward[];
	/** Events emitted on accept (e.g. spawn bandits for protect quests). */
	onAccept?: GameEvent[];
	/** Day the quest expires (undefined = no deadline). */
	expireDay?: number;
	/** 1–10 difficulty for UI and reward scaling. */
	difficulty: number;
};
