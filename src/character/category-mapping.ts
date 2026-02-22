import type {Category} from './types';
import {ZERO_INDEXED_CATEGORIES, resolveFileName} from './sprite-data';

// Maps any category to its base palette key. Falls back to identity for base keys.
export const REVERSE_PALETTE_DICTIONARY: Partial<Record<Category, Category>> = {
	Arms: 'Skintone',
	Body: 'Skintone',
	Ears: 'Skintone',
	Head: 'Skintone',
	BottomA: 'Bottom',
	BottomB: 'Bottom',
	Eyes: 'Eye',
	HairA: 'Hair',
	HairB: 'Hair',
	HairC: 'Hair',
	HairD: 'Hair',
	JacketA: 'Jacket',
	JacketB: 'Jacket',
	Shoes: 'Shoe',
	Socks: 'Sock',
	TopA: 'Top',
	TopB: 'Top',
	ShoulderA: 'Shoulder',
	ShoulderB: 'Shoulder',
	BackA: 'Back',
	BackB: 'Back',
	ChopA: 'Chop',
	ChopB: 'Chop',
	StrikeA: 'Strike',
	StrikeB: 'Strike',
};

// Maps abstract palette-only categories to their concrete primary sprite layer.
// Also used for resolving asset directory names.
const ABSTRACT_TO_PRIMARY: Partial<Record<Category, Category>> = {
	Eye: 'Eyes',
	Skintone: 'Body',
	Hair: 'HairA',
	Top: 'TopA',
	Bottom: 'BottomA',
	Sock: 'Socks',
	Shoe: 'Shoes',
	Jacket: 'JacketA',
	Shoulder: 'ShoulderA',
	Back: 'BackA',
	Strike: 'StrikeA',
	Chop: 'ChopA',
};

// Maps any category to its base palette key.
export function toPaletteAlias(category: Category | string): Category {
	return (REVERSE_PALETTE_DICTIONARY[category as Category] ?? category) as Category;
}

// Maps any category to the primary sprite layer it resolves to.
export function toPrimarySpriteCategory(category: Category | string): Category {
	return (ABSTRACT_TO_PRIMARY[category as Category] ?? category) as Category;
}

// Maps a category to its asset directory name.
export function mapCategoryToDirectory(category: Category): string {
	return ABSTRACT_TO_PRIMARY[category] ?? category;
}

export function formatSpriteIndex(category: Category, index: number): string {
	const zeroIndexed = ZERO_INDEXED_CATEGORIES.has(category);
	const spriteNumber = zeroIndexed ? index : Math.max(1, index + 1);
	return spriteNumber.toString().padStart(3, '0');
}

export function buildSpritePath(category: Category, spriteIndex: number | string): string {
	const directory = mapCategoryToDirectory(category);
	const fileName = resolveFileName(category);
	const normalizedIndex = typeof spriteIndex === 'number'
		? formatSpriteIndex(category, spriteIndex)
		: spriteIndex;
	return `/assets/character/${directory}/${fileName}_${normalizedIndex}.png`;
}
