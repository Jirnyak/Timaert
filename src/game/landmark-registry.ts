// === Landmark Registry — universal debug listing of macroworld points ===
//
// Data-driven registry of landmark sources. Anything in the world that
// has a (x, y) location and can be enumerated from GameState plugs in
// here with one entry. The DebugOverlay reads this registry to render
// a unified, expandable landmark list.
//
// Adding a new landmark kind = one `registerLandmarkSource()` call.
// No DebugOverlay changes required.

import type {GameState} from './state';

export type LandmarkEntry = {
	/** Unique within its source. */
	id: string;
	name: string;
	x: number;
	y: number;
	/** Optional secondary line (e.g. status, faction). */
	detail?: string;
};

export type LandmarkSource = {
	/** Display label for the kind ('City', 'Village', ...). */
	kind: string;
	/** Tailwind text color class for the entries of this source. */
	color: string;
	/** Pull all current landmarks of this kind from game state. */
	collect(state: GameState): LandmarkEntry[];
};

export const LANDMARK_SOURCES: LandmarkSource[] = [];

export function registerLandmarkSource(source: LandmarkSource): void {
	LANDMARK_SOURCES.push(source);
}

// ── Built-in sources ──────────────────────────────────────────────

registerLandmarkSource({
	kind: 'City',
	color: 'text-amber-300',
	collect: state => state.settlements.map(s => ({
		id: `city_${s.id}`,
		name: s.name,
		x: s.x,
		y: s.y,
		detail: `pop ${s.population} · ${s.mood}`,
	})),
});

registerLandmarkSource({
	kind: 'Village',
	color: 'text-lime-300',
	collect: state => state.villages.map(v => ({
		id: `village_${v.id}`,
		name: v.name,
		x: v.x,
		y: v.y,
		detail: `pop ${v.population}`,
	})),
});

registerLandmarkSource({
	kind: 'Spire',
	color: 'text-purple-300',
	collect: state => state.spires.map(s => ({
		id: `spire_${s.id}`,
		name: `Spire of ${s.spellId}`,
		x: s.x,
		y: s.y,
		detail: s.depleted ? 'depleted' : 'active',
	})),
});

registerLandmarkSource({
	kind: 'Marker',
	color: 'text-cyan-300',
	collect: state => state.markers.map(m => ({
		id: `marker_${m.id}`,
		name: m.label ?? m.id,
		x: m.x,
		y: m.y,
		detail: m.style,
	})),
});
