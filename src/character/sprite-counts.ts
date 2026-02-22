import type {Category} from './types';
import {SPRITE_LABEL_MAP} from './sprite-labels';

function computeSpriteCountsFromLabels(): Record<string, number> {
	const counts: Record<string, number> = {};
	for (const path of Object.keys(SPRITE_LABEL_MAP)) {
		// Paths look like /assets/character/<Dir>/<file>.png
		const parts = path.split('/');
		const dir = parts.at(-2);
		if (!dir) {
			continue;
		}

		counts[dir] = (counts[dir] ?? 0) + 1;
	}

	return counts;
}

// Derive counts from the label map so it stays in sync with assets
const derivedCounts = computeSpriteCountsFromLabels();

// Some variants (B/D) mirror directly from their primary counterparts
// even though the label map only lists the primary. Mirror those counts here.
export const mirroredVariantMap: Partial<Record<Category, Category>> = {
	BackB: 'BackA',
	ShoulderB: 'ShoulderA',
	ChopB: 'ChopA',
	StrikeB: 'StrikeA',
	HairD: 'HairB',
} as const;

for (const [mirror, base] of Object.entries(mirroredVariantMap)) {
	const baseCount = derivedCounts[base];
	if (baseCount !== undefined) {
		derivedCounts[mirror] = baseCount;
	}
}

export const spriteCounts: Record<string, number> = derivedCounts;

export function clampSpriteIndex(category: Category | string, index: number): number {
	const count = spriteCounts[category] ?? 1;
	return count <= 0 ? 0 : Math.max(0, Math.min(index, count - 1));
}
