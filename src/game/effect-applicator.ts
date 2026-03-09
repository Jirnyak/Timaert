// === Effect Applicator — applies game-event effects to player state ===
//
// Layer 3 (Event System). Pure function: GameEvent[] → mutate PlayerState.
// No UI, no bus, no overlays — callers re-emit into bus if needed.

import {EventTag, type GameEvent} from './event-types';
import type {PlayerState} from './state';

/** Apply a batch of effect events to the player. Returns list of applied tags. */
export function applyEffects(player: PlayerState, effects: readonly GameEvent[]): EventTag[] {
	const applied: EventTag[] = [];

	for (const event of effects) {
		// eslint-disable-next-line @typescript-eslint/switch-exhaustiveness-check -- only effect-like tags handled here
		switch (event.tag) {
			case EventTag.PlayerGoldChange: {
				player.gold += event.delta;
				applied.push(event.tag);
				break;
			}

			case EventTag.ApplyEffect: {
				applyEffect_(player, event.effectType, event.value);
				applied.push(event.tag);
				break;
			}

			case EventTag.ReputationChange: {
				player.reputation[event.faction]
					= (player.reputation[event.faction] ?? 0) + event.delta;
				applied.push(event.tag);
				break;
			}

			case EventTag.CodexUnlock: {
				if (!player.codexUnlocked.includes(event.entryId)) {
					player.codexUnlocked.push(event.entryId);
				}

				applied.push(event.tag);
				break;
			}

			default: {
				break;
			}
		}
	}

	return applied;
}

// ── Internal effect dispatcher ──

function applyEffect_(player: PlayerState, effectType: string, value: number): void {
	const cs = player.combatStats;

	switch (effectType) {
		case 'heal_hp':
		case 'restore_hp': {
			cs.currentHp = Math.min(cs.currentHp + value, cs.maxHp);
			break;
		}

		case 'damage_hp': {
			cs.currentHp -= value;
			break;
		}

		case 'restore_mp': {
			cs.currentMp = Math.min(cs.currentMp + value, cs.maxMp);
			break;
		}

		case 'restore_sp': {
			cs.currentSp = Math.min(cs.currentSp + value, cs.maxSp);
			break;
		}

		case 'drain_sp': {
			cs.currentSp = Math.max(0, cs.currentSp - value);
			break;
		}

		case 'grant_xp': {
			player.levelData.exp += value;
			break;
		}

		default: {
			break;
		}
	}
}
