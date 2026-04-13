// === Markers — universal macroworld point-of-interest system ===
//
// Layer 1 (Macroworld). Data-only markers placed and removed at runtime.
// Rendered as overlays on the macroworld map. Used by quests, POIs,
// and any system that needs to highlight a cell.
//
// Adding a marker = one push. Removing = one filter. No engine changes.

// ── Marker types ──

export type MarkerStyle = 'quest' | 'poi' | 'danger' | 'waypoint';

export type Marker = {
	id: string;
	x: number;
	y: number;
	style: MarkerStyle;
	label?: string;
};

// ── Marker store (mutable array on GameState) ──

export function addMarker(markers: Marker[], marker: Marker): void {
	if (!markers.some(m => m.id === marker.id)) {
		markers.push(marker);
	}
}

export function removeMarker(markers: Marker[], id: string): void {
	const index = markers.findIndex(m => m.id === id);
	if (index !== -1) {
		markers.splice(index, 1);
	}
}

export function removeMarkersByPrefix(markers: Marker[], prefix: string): void {
	for (let i = markers.length - 1; i >= 0; i--) {
		if (markers[i].id.startsWith(prefix)) {
			markers.splice(i, 1);
		}
	}
}

export function hasMarker(markers: Marker[], id: string): boolean {
	return markers.some(m => m.id === id);
}

// ── Render helpers ──

/** Marker colors per style. */
export const MARKER_COLORS: Record<MarkerStyle, string> = {
	quest: '#ffd700',
	poi: '#87ceeb',
	danger: '#ff4444',
	waypoint: '#90ee90',
};

/** Marker glyphs per style (rendered as text). */
export const MARKER_GLYPHS: Record<MarkerStyle, string> = {
	quest: '?',
	poi: '★',
	danger: '!',
	waypoint: '◆',
};
