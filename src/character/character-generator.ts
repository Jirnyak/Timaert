import type {Category, CharacterData, PaletteState} from './types';
import {clampSpriteIndex, spriteCounts} from './sprite-counts';
import {SPRITE_LABEL_MAP} from './sprite-labels';
import {
	SPRITE_LAYERS,
	SECONDARY_MIRROR_MAP,
	SECONDARY_ONLY_LAYERS,
} from './sprite-data';
import {mapCategoryToDirectory, toPaletteAlias} from './category-mapping';
import {paletteManager} from './palette';
import {DEFAULT_PALETTE_STATE, DEFAULT_SPRITE_INDICES} from './defaults';

const generationChances: Record<string, number> = {
	AccessoryA: 10,
	AccessoryB: 10,
	AccessoryC: 10,
	AccessoryD: 10,
	HairB: 10,
	HairC: 10,
	JacketA: 10,
	Arms: 10,
	Ears: 10,
	Eyebrows: 10,
	Face: 10,
	Gloves: 10,
	Horns: 10,
	Mid: 10,
	BackA: 10,
	ShoulderA: 10,
	ChopA: 100,
	StrikeA: 100,
	Reap: 100,
	Water: 100,
	Seed: 100,
};

type EyeChances = Record<number, number>;

const eyeChances: EyeChances = {
	1: 100, 2: 100, 3: 100, 4: 100, 5: 100, 6: 100, 7: 100,
	8: 10, 9: 10, 10: 10, 11: 10, 12: 10, 13: 10, 14: 10,
};

// Track which sprite directories have an explicit _000 baseline file
const zeroSpriteDirs: Set<string> = (() => {
	const dirs = new Set<string>();
	for (const path of Object.keys(SPRITE_LABEL_MAP)) {
		const parts = path.split('/');
		const dir = parts.at(-2);
		const file = parts.at(-1);
		if (dir && file?.includes('_000')) {
			dirs.add(dir);
		}
	}

	return dirs;
})();

function hasZeroSprite(category: Category): boolean {
	const dir = mapCategoryToDirectory(category);
	// Arms has no _000 file in assets; treat arms_001 as the baseline fallback
	if (category === 'Arms') {
		return true;
	}

	return zeroSpriteDirs.has(dir);
}

// Hide TopB when JacketB has sleeves (non-zero).
function applyJacketTopbRule(character: CharacterData): CharacterData {
	const updated: CharacterData = {
		...character,
		sprites: {...character.sprites},
		hidden: [...(character.hidden ?? [])],
	};

	if ((updated.sprites.JacketA ?? 0) === 0) {
		updated.sprites.JacketB = 0;
	}

	const jacketBIndex = updated.sprites.JacketB ?? 0;
	if (jacketBIndex === 0) {
		updated.hidden = updated.hidden.filter(c => c !== 'TopB');
	} else if (!updated.hidden.includes('TopB')) {
		updated.hidden.push('TopB');
	}

	return updated;
}

function getSpriteCount(category: Category): number {
	const directory = mapCategoryToDirectory(category);
	return spriteCounts[directory] ?? 0;
}

function createEmptyCharacter(): CharacterData {
	paletteManager.loadPalettes();
	return {
		name: 'New Sprite',
		desc: '',
		sprites: Object.fromEntries(SPRITE_LAYERS.map(c => [c, DEFAULT_SPRITE_INDICES[c] ?? 0])),
		hidden: [],
		paletteState: {...DEFAULT_PALETTE_STATE},
	};
}

function generateRandomCharacter(
	paletteState: PaletteState,
	options?: {lockedSprites?: Partial<Record<string, boolean>>; lockedPalettes?: Partial<Record<string, boolean>>},
): CharacterData {
	const character = createEmptyCharacter();

	// Randomize palettes first, skipping locked palettes
	paletteManager.loadPalettes();
	let updatedPaletteState: PaletteState = {...paletteState};
	const lockedPalettes = options?.lockedPalettes ?? {};

	for (const paletteCat of Object.keys(updatedPaletteState)) {
		if (lockedPalettes[paletteCat]) {
			continue;
		}

		const paletteAlias = toPaletteAlias(paletteCat);
		const availablePalettes = paletteManager.getPalettes(paletteAlias) ?? [];
		if (availablePalettes.length === 0) {
			continue;
		}

		const randomRow = Math.floor(Math.random() * availablePalettes.length);
		updatedPaletteState = paletteManager.updatePaletteState(updatedPaletteState, paletteCat as Category, randomRow);
	}

	character.paletteState = updatedPaletteState;
	const lockedSprites = options?.lockedSprites ?? {};
	const spriteCountMap: Record<string, number> = {};
	for (const cat of SPRITE_LAYERS) {
		spriteCountMap[cat] = getSpriteCount(cat);
	}

	for (const category of SPRITE_LAYERS) {
		if (category === 'Body' || category === 'Head') {
			continue;
		}

		if (SECONDARY_ONLY_LAYERS.has(category)) {
			continue;
		}

		if (lockedSprites[category]) {
			continue;
		}

		const chance = generationChances[category];
		if (chance && Math.random() * 100 >= chance) {
			if (hasZeroSprite(category)) {
				character.sprites[category] = 0;
			} else {
				character.hidden.push(category);
			}

			continue;
		}

		if (category === 'Eyes') {
			const availableEyes = Object.keys(eyeChances).map(Number).filter(n => n <= (spriteCountMap[category] ?? 0));
			if (availableEyes.length === 0) {
				character.hidden.push(category);
				continue;
			}

			const totalWeight = availableEyes.reduce((sum, n) => sum + (eyeChances[n] ?? 0), 0);
			let random = Math.random() * totalWeight;
			for (const eyeNumber of availableEyes) {
				random -= (eyeChances[eyeNumber] ?? 0);
				if (random <= 0) {
					character.sprites[category] = eyeNumber - 1;
					break;
				}
			}
		} else {
			const forbidZero = ['HairA', 'TopA', 'TopB', 'BottomA', 'BottomB'].includes(category);
			const minIndex = forbidZero ? 1 : 0;
			const range = Math.max((spriteCountMap[category] ?? 0) - minIndex, 0);
			character.sprites[category] = range > 0 ? Math.floor(Math.random() * range) + minIndex : 0;
		}

		const secondary = SECONDARY_MIRROR_MAP[category];
		if (secondary) {
			const secondaryCount = spriteCountMap[secondary] ?? 0;
			const secondaryIndex = clampSpriteIndex(secondary, character.sprites[category] ?? 0);
			character.sprites[secondary] = Math.min(secondaryIndex, Math.max(secondaryCount - 1, 0));
		}
	}

	return applyJacketTopbRule(character);
}

function ensureCompleteCharacter(character: CharacterData): CharacterData {
	const paletteState: PaletteState = {...DEFAULT_PALETTE_STATE, ...character.paletteState};

	const mergedSprites: Record<string, number> = {};
	for (const category of SPRITE_LAYERS) {
		mergedSprites[category] = character.sprites?.[category] ?? DEFAULT_SPRITE_INDICES[category] ?? 0;
	}

	return {
		...character,
		paletteState,
		sprites: mergedSprites,
		hidden: character.hidden ?? [],
	};
}

export const CharacterManager = {
	getSpriteCount,
	createEmptyCharacter,
	generateRandomCharacter,
	ensureCompleteCharacter,
} as const;
