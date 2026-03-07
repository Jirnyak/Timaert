// === Event Bus — Universal Dispatcher + World History ===
// Data-oriented: flat arrays, no closures in hot path.
// Each tick: emit events → flush to history → logic nodes scan.

import {type GameEvent, type WorldHistoryEntry, type EventTag} from './event-types';

// ── Per-tag callback registry (for imperative subscribers like UI) ──

type EventHandler = (event: GameEvent) => void;

// ── EventBus ──

export class EventBus {
	// — Tick event buffer (cleared every tick) —
	private readonly tickEvents_: GameEvent[] = [];

	// — Previous tick's events (for logic-node condition checks) —
	private lastTickEvents_: readonly GameEvent[] = [];

	// — Persistent world history —
	private readonly history_: WorldHistoryEntry[] = [];
	private tick_ = 0;

	// — Tag-indexed listeners (for UI / imperative side-effects) —
	private readonly listeners_ = new Map<EventTag, EventHandler[]>();

	// ── Emit an event into the current tick buffer ──

	emit(event: GameEvent): void {
		this.tickEvents_.push(event);

		// Immediately notify imperative listeners
		const handlers = this.listeners_.get(event.tag);
		if (handlers) {
			for (const handler of handlers) {
				handler(event);
			}
		}
	}

	// ── Emit multiple events at once ──

	emitAll(events: readonly GameEvent[]): void {
		for (const ev of events) {
			this.emit(ev);
		}
	}

	// ── Subscribe to a specific event tag ──

	on(tag: EventTag, handler: EventHandler): () => void {
		let handlers = this.listeners_.get(tag);
		if (!handlers) {
			handlers = [];
			this.listeners_.set(tag, handlers);
		}

		handlers.push(handler);

		// Return unsubscribe function
		return () => {
			const array = this.listeners_.get(tag);
			if (array) {
				const idx = array.indexOf(handler);
				if (idx !== -1) {
					array.splice(idx, 1);
				}
			}
		};
	}

	// ── Flush tick buffer → history, then clear buffer ──
	// Call this at the END of each tick, after game logic has emitted events.

	flush(day: number, hour: number): void {
		for (const event of this.tickEvents_) {
			this.history_.push({
				tick: this.tick_,
				day,
				hour,
				event,
			});
		}

		// Save for next tick's logic-node condition checks
		this.lastTickEvents_ = [...this.tickEvents_];
		this.tickEvents_.length = 0;
		this.tick_++;
	}

	// ── Read-only access for logic nodes and UI ──

	/** Events emitted during the current tick (before flush). */
	get tickEvents(): readonly GameEvent[] {
		return this.tickEvents_;
	}

	/** Events from the previous tick (for logic-node condition checks). */
	get lastTickEvents(): readonly GameEvent[] {
		return this.lastTickEvents_;
	}

	/** Whether any event with the given tag exists in this tick. */
	hasTag(tag: EventTag): boolean {
		for (const ev of this.tickEvents_) {
			if (ev.tag === tag) {
				return true;
			}
		}

		return false;
	}

	/** Find first event matching a tag in current tick. */
	find<T extends GameEvent>(tag: T['tag']): T | undefined {
		for (const ev of this.tickEvents_) {
			if (ev.tag === tag) {
				return ev as T;
			}
		}

		return undefined;
	}

	/** Find all events matching a tag in current tick. */
	findAll<T extends GameEvent>(tag: T['tag']): T[] {
		const result: T[] = [];
		for (const ev of this.tickEvents_) {
			if (ev.tag === tag) {
				result.push(ev as T);
			}
		}

		return result;
	}

	/** Full world history (oldest first). */
	get history(): readonly WorldHistoryEntry[] {
		return this.history_;
	}

	/** Current tick counter. */
	get tick(): number {
		return this.tick_;
	}

	/** Number of recorded history entries. */
	get historyLength(): number {
		return this.history_.length;
	}

	/** Query history by tag (linear scan — use sparingly for UI). */
	queryHistory(tag: EventTag, limit = 50): WorldHistoryEntry[] {
		const result: WorldHistoryEntry[] = [];
		// Scan from newest
		for (let i = this.history_.length - 1; i >= 0 && result.length < limit; i--) {
			if (this.history_[i].event.tag === tag) {
				result.push(this.history_[i]);
			}
		}

		return result;
	}

	/** Trim old history to prevent unbounded memory growth. */
	trimHistory(maxEntries: number): void {
		if (this.history_.length > maxEntries) {
			this.history_.splice(0, this.history_.length - maxEntries);
		}
	}

	/** Reset everything (new game). */
	reset(): void {
		this.tickEvents_.length = 0;
		this.lastTickEvents_ = [];
		this.history_.length = 0;
		this.tick_ = 0;
		this.listeners_.clear();
	}
}
