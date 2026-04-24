// Registry of overlays that can be opened in BOTH the macroworld (GameScreen)
// and the subworld (SubworldScreen). Adding a new shared overlay = one entry
// here + one rendering branch in SharedOverlays.svelte. Non-shared overlays
// continue to live in their owning screen and never appear here.

export type SharedOverlayId = 'inventory' | 'spells';

export type SharedOverlayDef = {
	id: SharedOverlayId;
	keys: string[]; // Lowercase keys that toggle this overlay
	label: string;
};

export const SHARED_OVERLAYS: readonly SharedOverlayDef[] = [
	{id: 'inventory', keys: ['i'], label: 'Inventory'},
	{id: 'spells', keys: ['b'], label: 'Spellbook'},
];

const KEY_TO_ID: Record<string, SharedOverlayId> = (() => {
	const map: Record<string, SharedOverlayId> = {};
	for (const def of SHARED_OVERLAYS) {
		for (const key of def.keys) {
			map[key] = def.id;
		}
	}

	return map;
})();

export function sharedOverlayForKey(key: string): SharedOverlayId | undefined {
	return KEY_TO_ID[key.toLowerCase()];
}

export function toggleSharedOverlay(
	current: SharedOverlayId | undefined,
	id: SharedOverlayId,
): SharedOverlayId | undefined {
	return current === id ? undefined : id;
}
