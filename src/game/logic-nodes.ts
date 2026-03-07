// === Logic Nodes — universal graph-based game-logic engine ===
//
// Pipeline (each game tick):
//   1. logicEngine.tick() — runs BEFORE game logic.
//      Check every active node: conditions fulfilled?
//        Yes → run effect → remove → add next to active.
//        No  → stays in active, checked again next tick.
//   2. Game logic runs, emits events into bus.
//   3. eventBus.flush() — current events become lastTickEvents.
//
// Condition slots can be:
//   • Event-tag scan:  { tag, predicate? }
//   • Pure predicate:  { check: (bus, player) => boolean }
// Both return 0 or 1. The mask vector selects which slots matter.

import {type GameEvent, type EventTag} from './event-types';
import type {EventBus} from './event-bus';
import type {PlayerState} from './state';

// ── Condition slot: anything that evaluates to 0 or 1 ──

export type ConditionSlot =
	| {
		/** Event tag to scan for in lastTickEvents. */
		tag: EventTag;
		/** Optional narrowing predicate on the matched event. */
		predicate?: (event: GameEvent) => boolean;
	}
	| {
		/** Any boolean function — math formula, state check, etc. */
		check: (bus: EventBus, player: PlayerState) => boolean;
	};

// ── Node effect & context ──

export type NodeEffect = (ctx: NodeContext) => void;

export type NodeContext = {
	bus: EventBus;
	player: PlayerState;
	/** Register a new node definition. */
	addNode: (node: LogicNode) => void;
	/** Remove a node definition by id. */
	removeNode: (id: string) => void;
	/** Activate a node (add to active set). */
	activate: (id: string) => void;
};

// ── Logic Node (pure data) ──

export type LogicNode = {
	/** Unique identifier. */
	id: string;
	/** Human-readable label for debug/UI. */
	label: string;

	/**
	 * Condition vector — parallel arrays.
	 * `conditions[i]` is the slot; `mask[i]` is 0 (wildcard) or 1 (required).
	 * Empty conditions → always fulfilled (effect runs every tick).
	 */
	conditions: ConditionSlot[];
	mask: number[];

	/**
	 * Effect to run when conditions are fulfilled.
	 * Optional — a node may exist purely to gate / route.
	 */
	effect?: NodeEffect;

	/**
	 * IDs of nodes to activate after this one fires.
	 * Own ID = self-link (goes back to active, checked again next tick).
	 */
	next: string[];

	/** Tags for filtering/querying ('quest', 'encounter', 'lore'). */
	tags: string[];
};

// ── Helper ──

export function createNode(partial: Partial<LogicNode> & Pick<LogicNode, 'id' | 'label'>): LogicNode {
	return {
		conditions: [],
		mask: [],
		next: [],
		tags: [],
		...partial,
	};
}

// ── Logic Node Engine ──

export class LogicNodeEngine {
	/** All registered node definitions (lookup table). */
	private readonly nodes_ = new Map<string, LogicNode>();

	/** Active nodes — checked every tick. */
	private readonly activeIds_ = new Set<string>();

	// ── Registration ──

	register(node: LogicNode): void {
		this.nodes_.set(node.id, node);
	}

	unregister(id: string): void {
		this.nodes_.delete(id);
		this.activeIds_.delete(id);
	}

	get(id: string): LogicNode | undefined {
		return this.nodes_.get(id);
	}

	has(id: string): boolean {
		return this.nodes_.has(id);
	}

	/** Add a node to the active set. */
	activate(id: string): void {
		this.activeIds_.add(id);
	}

	/** All registered nodes (debug). */
	get allNodes(): LogicNode[] {
		return [...this.nodes_.values()];
	}

	/** Currently active node IDs (debug). */
	get activeNodeIds(): ReadonlySet<string> {
		return this.activeIds_;
	}

	// ── Tick ──

	tick(bus: EventBus, player: PlayerState): void {
		const ctx: NodeContext = {
			bus,
			player,
			addNode: node => {
				this.register(node);
			},
			removeNode: id => {
				this.unregister(id);
			},
			activate: id => {
				this.activeIds_.add(id);
			},
		};

		// Check every active node. Fulfilled → effect → remove → add next.
		const toRemove: string[] = [];
		const toAdd: string[] = [];

		for (const id of this.activeIds_) {
			const node = this.nodes_.get(id);
			if (!node) {
				toRemove.push(id);
				continue;
			}

			if (this.checkConditions_(node, bus, player)) {
				if (node.effect) {
					node.effect(ctx);
				}

				toRemove.push(id);
				for (const nextId of node.next) {
					toAdd.push(nextId);
				}
			}
		}

		for (const id of toRemove) {
			this.activeIds_.delete(id);
		}

		for (const id of toAdd) {
			this.activeIds_.add(id);
		}
	}

	// ── Condition checking ──

	private checkConditions_(node: LogicNode, bus: EventBus, player: PlayerState): boolean {
		const {conditions, mask} = node;

		for (const [i, slot] of conditions.entries()) {
			if (mask[i] === 0) {
				continue; // Wildcard — skip
			}

			if ('check' in slot) {
				// Pure predicate — any math formula or state check
				if (!slot.check(bus, player)) {
					return false;
				}
			} else {
				// Event-tag scan against lastTickEvents
				let found = false;
				for (const event of bus.lastTickEvents) {
					if (event.tag !== slot.tag) {
						continue;
					}

					if (slot.predicate && !slot.predicate(event)) {
						continue;
					}

					found = true;
					break;
				}

				if (!found) {
					return false;
				}
			}
		}

		return true;
	}

	/** Reset everything (new game). */
	reset(): void {
		this.nodes_.clear();
		this.activeIds_.clear();
	}
}
