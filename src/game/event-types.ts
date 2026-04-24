// === Universal Event Types ===
// Data-oriented event descriptors for all notable world changes.
// Events are plain structs — no methods, no closures, fully serializable.

import type {NPCType} from './npc';

// ── Event tag discriminant ──

export const enum EventTag {
	// Entity events
	NpcDeath = 'npc_death',
	NpcSpawn = 'npc_spawn',
	NpcHpChange = 'npc_hp_change',

	// Landmark / settlement events
	LandmarkChangeOwner = 'landmark_change_owner',
	SettlementMoodChange = 'settlement_mood_change',

	// Player events
	PlayerStatChange = 'player_stat_change',
	PlayerLevelUp = 'player_level_up',
	PlayerGoldChange = 'player_gold_change',
	PlayerMove = 'player_move',
	PlayerEnterSettlement = 'player_enter_settlement',
	PlayerLeaveSettlement = 'player_leave_settlement',

	// Combat events
	BattleStart = 'battle_start',
	BattleEnd = 'battle_end',

	// World / nature events
	WorldCellChange = 'world_cell_change',
	MagicSurge = 'magic_surge',
	TimeAdvance = 'time_advance',

	// Diplomacy / faction events
	FactionRelationChange = 'faction_relation_change',
	ReputationChange = 'reputation_change',

	// Quest / logic
	CodexUnlock = 'codex_unlock',
	QuestStart = 'quest_start',
	QuestUpdate = 'quest_update',
	QuestComplete = 'quest_complete',
	QuestFail = 'quest_fail',
	DialogStart = 'dialog_start',

	// UI / presentation (non-historical, consumed same tick)
	ShowDialog = 'show_dialog',
	ShowStory = 'show_story',
	ApplyEffect = 'apply_effect',
	SpawnEntity = 'spawn_entity',
	CameraMove = 'camera_move',
}

// ── Event payload types (discriminated union) ──

export type NpcDeathEvent = {
	tag: EventTag.NpcDeath;
	npcId: number;
	npcName: string;
	npcType: NPCType;
	x: number;
	y: number;
	causeNpcId?: number; // Killed by whom (undefined = player)
};

export type NpcSpawnEvent = {
	tag: EventTag.NpcSpawn;
	npcId: number;
	npcType: NPCType;
	x: number;
	y: number;
};

export type NpcHpChangeEvent = {
	tag: EventTag.NpcHpChange;
	npcId: number;
	delta: number;
};

export type LandmarkChangeOwnerEvent = {
	tag: EventTag.LandmarkChangeOwner;
	landmarkId: number;
	oldFaction: string;
	newFaction: string;
};

export type SettlementMoodChangeEvent = {
	tag: EventTag.SettlementMoodChange;
	settlementId: number;
	oldMood: string;
	newMood: string;
};

export type PlayerStatChangeEvent = {
	tag: EventTag.PlayerStatChange;
	stat: string; // 'hp' | 'mp' | 'sp' | attribute key
	oldValue: number;
	newValue: number;
};

export type PlayerLevelUpEvent = {
	tag: EventTag.PlayerLevelUp;
	newLevel: number;
};

export type PlayerGoldChangeEvent = {
	tag: EventTag.PlayerGoldChange;
	delta: number;
	newTotal: number;
};

export type PlayerMoveEvent = {
	tag: EventTag.PlayerMove;
	fromX: number;
	fromY: number;
	toX: number;
	toY: number;
	steps: number; // Cumulative steps this session
};

export type PlayerEnterSettlementEvent = {
	tag: EventTag.PlayerEnterSettlement;
	settlementId: number;
	settlementName: string;
};

export type PlayerLeaveSettlementEvent = {
	tag: EventTag.PlayerLeaveSettlement;
	settlementId: number;
	settlementName: string;
};

export type BattleStartEvent = {
	tag: EventTag.BattleStart;
	enemyName: string;
	enemyType: NPCType;
	enemyLevel: number;
};

export type BattleEndEvent = {
	tag: EventTag.BattleEnd;
	victory: boolean;
	enemyName: string;
	lootGold: number;
};

export type WorldCellChangeEvent = {
	tag: EventTag.WorldCellChange;
	x: number;
	y: number;
	oldType: number;
	newType: number;
};

export type MagicSurgeEvent = {
	tag: EventTag.MagicSurge;
	x: number;
	y: number;
	intensity: number;
};

export type TimeAdvanceEvent = {
	tag: EventTag.TimeAdvance;
	day: number;
	hour: number;
};

export type FactionRelationChangeEvent = {
	tag: EventTag.FactionRelationChange;
	factionA: string;
	factionB: string;
	oldValue: number;
	newValue: number;
};

export type ReputationChangeEvent = {
	tag: EventTag.ReputationChange;
	faction: string;
	delta: number;
	newValue: number;
};

export type CodexUnlockEvent = {
	tag: EventTag.CodexUnlock;
	entryId: string;
};

export type QuestStartEvent = {
	tag: EventTag.QuestStart;
	questId: string;
	title: string;
};

export type QuestUpdateEvent = {
	tag: EventTag.QuestUpdate;
	questId: string;
};

export type QuestCompleteEvent = {
	tag: EventTag.QuestComplete;
	questId: string;
};

export type QuestFailEvent = {
	tag: EventTag.QuestFail;
	questId: string;
	reason: string;
};

export type DialogStartEvent = {
	tag: EventTag.DialogStart;
	dialogId: string;
	npcId?: number;
};

// ── UI / ephemeral events ──

export type DialogChoice = {
	label: string;
	nodeId?: string; // Logic node to activate on pick
	effects?: GameEvent[]; // Immediate events to emit
};

export type ShowDialogEvent = {
	tag: EventTag.ShowDialog;
	title: string;
	description: string;
	choices: DialogChoice[];
};

export type ApplyEffectEvent = {
	tag: EventTag.ApplyEffect;
	target: 'player' | number; // 'player' or NPC id
	effectType: string;
	value: number;
};

export type SpawnEntityEvent = {
	tag: EventTag.SpawnEntity;
	npcType: NPCType;
	x: number;
	y: number;
	level: number;
	name?: string;
	factionId?: string;
};

export type CameraMoveEvent = {
	tag: EventTag.CameraMove;
	x: number;
	y: number;
};

// ── Story sequence event (slides, choices — reused by all plot modules) ──

export type StorySlide = {
	image: string;
	narration?: string;
};

export type StoryChoice = {
	label: string;
	description: string;
	value: string;
	image?: string;
};

export type StoryPhase =
	| {type: 'slides'; slides: StorySlide[]}
	| {type: 'choice'; id: string; title: string; description: string; options: StoryChoice[]}
	| {type: 'input'; id: string; title: string; description: string; placeholder?: string; defaultValue?: string; maxLength?: number};

export type ShowStoryEvent = {
	tag: EventTag.ShowStory;
	phases: StoryPhase[];
	/** Callback id — plot nodes use this to route the result. */
	sourceNodeId?: string;
};

// ── Union of all game events ──

export type GameEvent =
	| NpcDeathEvent
	| NpcSpawnEvent
	| NpcHpChangeEvent
	| LandmarkChangeOwnerEvent
	| SettlementMoodChangeEvent
	| PlayerStatChangeEvent
	| PlayerLevelUpEvent
	| PlayerGoldChangeEvent
	| PlayerMoveEvent
	| PlayerEnterSettlementEvent
	| PlayerLeaveSettlementEvent
	| BattleStartEvent
	| BattleEndEvent
	| WorldCellChangeEvent
	| MagicSurgeEvent
	| TimeAdvanceEvent
	| FactionRelationChangeEvent
	| ReputationChangeEvent
	| CodexUnlockEvent
	| QuestStartEvent
	| QuestUpdateEvent
	| QuestCompleteEvent
	| QuestFailEvent
	| DialogStartEvent
	| ShowDialogEvent
	| ApplyEffectEvent
	| SpawnEntityEvent
	| CameraMoveEvent
	| ShowStoryEvent;

// ── Timestamped entry for the world history log ──

export type WorldHistoryEntry = {
	tick: number;
	day: number;
	hour: number;
	event: GameEvent;
};
