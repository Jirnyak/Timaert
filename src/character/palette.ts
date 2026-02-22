import type {
	Category, SpecialPalette, PaletteData, PaletteState, PaletteConfig, PaletteCategoryData,
} from './types';
import palettesJson from './palette-data.json';
import {DEFAULT_PALETTE_STATE} from './defaults';
import {REVERSE_PALETTE_DICTIONARY, toPaletteAlias} from './category-mapping';

export class PaletteManager {
	private paletteData: PaletteData | undefined;
	private readonly specialPalettes: SpecialPalette[] = ['Skintone', 'Eye', 'Chop', 'Strike', 'Reap', 'Water', 'Seed'];

	loadPalettes(): PaletteData {
		if (this.paletteData) {
			return this.paletteData;
		}

		// Load palette data from bundled JSON
		this.paletteData = this.normalizePaletteData(palettesJson);
		if (!this.paletteData) {
			throw new Error('Palette data is empty');
		}

		// Ensure variant categories exist by cloning their base entries when missing
		const ensureVariant = (variant: Category, base: Category) => {
			if (!this.paletteData) {
				return;
			}

			const baseEntry = this.paletteData[base];
			if (!this.paletteData[variant] && baseEntry) {
				this.paletteData[variant] = {...baseEntry};
			}
		};

		// Accessory variants
		ensureVariant('AccessoryB', 'AccessoryA');
		ensureVariant('AccessoryC', 'AccessoryA');
		ensureVariant('AccessoryD', 'AccessoryA');
		// Hair variants
		ensureVariant('HairA', 'Hair');
		ensureVariant('HairB', 'Hair');
		ensureVariant('HairC', 'Hair');
		ensureVariant('HairD', 'Hair');
		// Top/Bottom/Jacket variants
		ensureVariant('TopA', 'Top');
		ensureVariant('TopB', 'Top');
		ensureVariant('BottomA', 'Bottom');
		ensureVariant('BottomB', 'Bottom');
		ensureVariant('JacketA', 'Jacket');
		ensureVariant('JacketB', 'Jacket');
		// Shoulder/Back variants
		ensureVariant('ShoulderA', 'Shoulder');
		ensureVariant('ShoulderB', 'Shoulder');
		ensureVariant('BackA', 'Back');
		ensureVariant('BackB', 'Back');
		// Shoe/Sock pluralization
		ensureVariant('Shoes', 'Shoe');
		ensureVariant('Socks', 'Sock');
		// Skintone variants
		ensureVariant('Face', 'Skintone');
		ensureVariant('Body', 'Skintone');
		ensureVariant('Head', 'Skintone');
		ensureVariant('Arms', 'Skintone');
		ensureVariant('Ears', 'Skintone');
		// Tool variants
		ensureVariant('ChopA', 'Chop');
		ensureVariant('ChopB', 'Chop');
		ensureVariant('StrikeA', 'Strike');
		ensureVariant('StrikeB', 'Strike');

		return this.paletteData;
	}

	getPalette(row: number): string[] {
		if (!this.paletteData || row < 0 || row >= this.paletteData.palettes.length) {
			return [];
		}

		return this.paletteData.palettes[row];
	}

	getSpecialPalette(category: SpecialPalette, row: number): string[] {
		if (!this.paletteData?.[category]) {
			return [];
		}

		const categoryData = this.paletteData[category];
		if (!categoryData.palettes || row < 0 || row >= categoryData.palettes.length) {
			return [];
		}

		return categoryData.palettes[row];
	}

	getGrayscaleColors(category: Category): string[] {
		return this.getPaletteEntry(category)?.grayscale ?? [];
	}

	getDefaultPaletteState(): PaletteState {
		return DEFAULT_PALETTE_STATE;
	}

	updatePaletteState(state: PaletteState, category: Category, row: number): PaletteState {
		const newState = {...state};
		const alias = toPaletteAlias(category);
		const isSpecialAlias = this.specialPalettes.includes(alias as SpecialPalette);

		if (isSpecialAlias) {
			const colors = this.getSpecialPalette(alias as SpecialPalette, row);
			if (colors.length > 0) {
				newState[alias] = {
					category: alias,
					colors,
					row,
				};
			}
		} else {
			const colors = this.getPalette(row);
			const grayscaleColors = this.getGrayscaleColors(category);
			if (colors.length > 0) {
				newState[alias] = {
					category: 'Built-in',
					colors: colors.slice(0, grayscaleColors.length),
					row,
				};
			}
		}

		return newState;
	}

	private readonly paletteConfigCache = new Map<string, PaletteConfig>();

	getPaletteConfig(category: Category | string, state: PaletteState): PaletteConfig | undefined {
		const alias = toPaletteAlias(category);
		const grayscaleColors = this.getGrayscaleColors(category as Category);
		const paletteEntry = this.getPaletteEntry(category as Category);
		if (!this.paletteData || !paletteEntry) {
			return undefined;
		}

		const effectiveState = state[alias] ?? this.synthesizeDefaultState(alias, grayscaleColors, paletteEntry);
		if (!effectiveState) {
			return undefined;
		}

		// Persist synthesized defaults so subsequent calls reuse them
		state[alias] ||= effectiveState;

		const colors = effectiveState.colors ?? [];
		const activeCount = Math.min(grayscaleColors.length, colors.length);

		// Cache to avoid repeated slice allocations (hot path in batch rendering)
		let cacheKey = alias;
		for (let i = 0; i < activeCount; i++) {
			cacheKey += `|${colors[i]}`;
		}

		const cached = this.paletteConfigCache.get(cacheKey);
		if (cached) {
			return cached;
		}

		const config: PaletteConfig = {
			grayscaleColors,
			colorCount: activeCount,
			colors: colors.slice(0, activeCount),
		};
		this.paletteConfigCache.set(cacheKey, config);
		return config;
	}

	getPalettes(paletteName: Category | string): string[][] {
		if (!this.paletteData) {
			return [];
		}

		const alias = toPaletteAlias(paletteName);
		if (this.specialPalettes.includes(alias as SpecialPalette)) {
			return this.paletteData[alias]?.palettes ?? [];
		}

		return this.paletteData.palettes;
	}

	private synthesizeDefaultState(
		alias: Category,
		grayscaleColors: string[],
		paletteEntry: PaletteCategoryData | undefined,
	): PaletteState[string] | undefined {
		if (this.specialPalettes.includes(alias as SpecialPalette)) {
			const special = this.getSpecialPalette(alias as SpecialPalette, 0);
			if (special.length > 0) {
				return {category: alias, colors: special, row: 0};
			}
		}

		const raw = paletteEntry?.palettes?.[0] ?? this.getPalette(0);
		if (raw.length > 0) {
			const length = grayscaleColors.length > 0 ? grayscaleColors.length : raw.length;
			return {category: 'Built-in', colors: raw.slice(0, length), row: 0};
		}

		return undefined;
	}

	// Vite JSON imports are always pre-parsed objects
	private normalizePaletteData(raw: unknown): PaletteData | undefined {
		if (!raw || typeof raw !== 'object') {
			return undefined;
		}

		return ((raw as {default?: unknown}).default ?? raw) as PaletteData;
	}

	private getPaletteEntry(category: Category): PaletteCategoryData | undefined {
		if (!this.paletteData) {
			return undefined;
		}

		const alias = toPaletteAlias(category);
		return this.paletteData[category]
			?? this.paletteData[alias]
			?? (REVERSE_PALETTE_DICTIONARY[category] ? this.paletteData[REVERSE_PALETTE_DICTIONARY[category]] : undefined);
	}
}

export const paletteManager = new PaletteManager();
