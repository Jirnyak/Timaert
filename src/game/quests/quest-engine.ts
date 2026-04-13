// === Quest Engine — objective evaluation + reward application ===
//
// Layer 3 (Event System). Ticked each game tick alongside LogicNodeEngine.
// Reads events from the bus, checks active quest objectives, emits
// QuestComplete / QuestUpdate / QuestFail events.
//
// Adding a new objective type = one entry in OBJECTIVE_CHECKERS.
// Adding a new reward type = one entry in REWARD_APPLIERS.

import {EventTag} from '../event-types';
import type {EventBus} from '../event-bus';
import {
	type PlayerState, type GameState, type AnySettlement,
} from '../state';
import {torusDist} from '../torus';
import {
	removeItem, addItem, makeItem,
} from '../items';
import type {Quest, QuestObjective, QuestReward} from './quest-types';

// ── Objective checker registry (data-driven) ──

type ObjectiveChecker = (
	object: QuestObjective,
	bus: EventBus,
	player: PlayerState,
	state: GameState,
	mapWidth: number,
	mapHeight: number,
) => boolean;

const OBJECTIVE_CHECKERS: Record<string, ObjectiveChecker> = {
	visit_cell(object, _bus, player, _state, mapWidth, mapHeight) {
		if (object.type !== 'visit_cell') {
			return false;
		}

		const d = torusDist(player.x, player.y, object.x, object.y, mapWidth, mapHeight);
		return d <= object.radius;
	},

	find_location(object, bus) {
		if (object.type !== 'find_location') {
			return false;
		}

		// Checked via a dedicated SubworldProximity event (future)
		// For now, check if player entered the right cell
		for (const ev of bus.lastTickEvents) {
			if (ev.tag === EventTag.PlayerMove
				&& 'toX' in ev && 'toY' in ev
				&& ev.toX === object.cellX && ev.toY === object.cellY) {
				return true;
			}
		}

		return false;
	},

	deliver_items(object, _bus, player, state, mapWidth, mapHeight) {
		if (object.type !== 'deliver_items') {
			return false;
		}

		// Player must be at target settlement AND have items
		const target = findSettlement(state, object.targetSettlementId);
		if (!target) {
			return false;
		}

		const d = torusDist(player.x, player.y, target.x, target.y, mapWidth, mapHeight);
		if (d > 3) {
			return false;
		}

		const slot = player.inventory.items.find(i => i.id === object.itemId);
		if (!slot || slot.quantity < object.quantity) {
			return false;
		}

		// Consume items on completion
		removeItem(player.inventory, object.itemId, object.quantity);
		return true;
	},

	destroy_npc(object, bus) {
		if (object.type !== 'destroy_npc') {
			return false;
		}

		for (const ev of bus.lastTickEvents) {
			if (ev.tag === EventTag.NpcDeath && 'npcType' in ev && ev.npcType === object.npcType) {
				object.killed++;
			}
		}

		return object.killed >= object.count;
	},

	wait_at(object, bus, player, _state, mapWidth, mapHeight) {
		if (object.type !== 'wait_at') {
			return false;
		}

		const d = torusDist(player.x, player.y, object.x, object.y, mapWidth, mapHeight);
		if (d > object.radius) {
			return false;
		}

		// Count hours from TimeAdvance events
		for (const ev of bus.lastTickEvents) {
			if (ev.tag === EventTag.TimeAdvance) {
				object.hoursWaited++;
			}
		}

		return object.hoursWaited >= object.hoursRequired;
	},

	interact_cell(object, bus) {
		if (object.type !== 'interact_cell') {
			return false;
		}

		for (const ev of bus.lastTickEvents) {
			if (ev.tag === EventTag.LandmarkChangeOwner
				&& 'landmarkId' in ev
				&& ev.landmarkId === object.x) {
				return true;
			}

			if (ev.tag === EventTag.WorldCellChange
				&& 'x' in ev && 'y' in ev
				&& ev.x === object.x && ev.y === object.y) {
				return true;
			}
		}

		return false;
	},
};

// ── Reward applier registry (data-driven) ──

type RewardApplier = (reward: QuestReward, player: PlayerState, bus: EventBus) => void;

const REWARD_APPLIERS: Record<string, RewardApplier> = {
	gold(reward, player, bus) {
		if (reward.type !== 'gold') {
			return;
		}

		player.gold += reward.amount;
		bus.emit({tag: EventTag.PlayerGoldChange, delta: reward.amount, newTotal: player.gold});
	},

	xp(reward, player) {
		if (reward.type !== 'xp') {
			return;
		}

		player.levelData.exp += reward.amount;
	},

	item(reward, player) {
		if (reward.type !== 'item') {
			return;
		}

		addItem(player.inventory, makeItem(reward.itemId, reward.quantity));
	},

	reputation(reward, player, bus) {
		if (reward.type !== 'reputation') {
			return;
		}

		const newValue = (player.reputation[reward.faction] ?? 0) + reward.delta;
		player.reputation[reward.faction] = newValue;
		bus.emit({
			tag: EventTag.ReputationChange,
			faction: reward.faction,
			delta: reward.delta,
			newValue,
		});
	},

	event(reward, _player, bus) {
		if (reward.type !== 'event') {
			return;
		}

		bus.emit(reward.event);
	},
};

// ── Quest engine ──

export type QuestTickContext = {
	bus: EventBus;
	player: PlayerState;
	state: GameState;
	mapWidth: number;
	mapHeight: number;
};

export class QuestEngine {
	/** Tick — check all active quest objectives, complete/fail as needed. */
	tick({bus, player, state, mapWidth, mapHeight}: QuestTickContext): void {
		const toComplete: number[] = [];

		for (let qi = player.activeQuests.length - 1; qi >= 0; qi--) {
			const quest = player.activeQuests[qi];

			// Check expiry
			if (quest.expireDay !== undefined && state.worldTime.day > quest.expireDay) {
				player.activeQuests.splice(qi, 1);
				player.completedQuestIds.push(quest.id); // Track as done (failed)
				bus.emit({tag: EventTag.QuestFail, questId: quest.id, reason: 'expired'});
				continue;
			}

			// Check each incomplete objective
			let allDone = true;
			let anyUpdated = false;
			for (const object of quest.objectives) {
				if (object.completed) {
					continue;
				}

				const checker = OBJECTIVE_CHECKERS[object.type];
				if (checker?.(object, bus, player, state, mapWidth, mapHeight)) {
					object.completed = true;
					anyUpdated = true;
				} else {
					allDone = false;
				}
			}

			if (anyUpdated && !allDone) {
				bus.emit({tag: EventTag.QuestUpdate, questId: quest.id});
			}

			if (allDone) {
				toComplete.push(qi);
			}
		}

		// Complete quests (reverse order to preserve indices)
		for (const qi of toComplete) {
			const quest = player.activeQuests[qi];
			player.activeQuests.splice(qi, 1);
			player.completedQuestIds.push(quest.id);
			// Apply rewards
			for (const reward of quest.rewards) {
				REWARD_APPLIERS[reward.type]?.(reward, player, bus);
			}

			bus.emit({tag: EventTag.QuestComplete, questId: quest.id});
		}
	}

	/** Accept a quest — move to active list + emit onAccept events. */
	accept(quest: Quest, player: PlayerState, bus: EventBus): void {
		player.activeQuests.push(quest);
		if (quest.onAccept) {
			for (const ev of quest.onAccept) {
				bus.emit(ev);
			}
		}

		bus.emit({tag: EventTag.QuestStart, questId: quest.id, title: quest.title});
	}

	/** Abandon an active quest. */
	abandon(questId: string, player: PlayerState): void {
		const idx = player.activeQuests.findIndex(q => q.id === questId);
		if (idx !== -1) {
			player.activeQuests.splice(idx, 1);
		}
	}

	/** Check if a quest is already active or completed. */
	isKnown(questId: string, player: PlayerState): boolean {
		return player.completedQuestIds.includes(questId)
			|| player.activeQuests.some(q => q.id === questId);
	}
}

// ── Helpers ──

function findSettlement(state: GameState, id: number): AnySettlement | undefined {
	return (state.settlements as AnySettlement[]).find(s => s.id === id)
		?? state.villages.find(v => v.id === id);
}
