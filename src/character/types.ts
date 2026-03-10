export type AnimationType = 'idle' | 'walk' | 'run' | 'pickup' | 'strike' | 'chop' | 'seed' | 'water' | 'reap';
export type Direction = 'front' | 'back' | 'left' | 'right';

export type Category =
	| 'AccessoryA' | 'AccessoryB' | 'AccessoryC' | 'AccessoryD'
	| 'Arms' | 'Back' | 'BackA' | 'BackB' | 'Body' | 'Bottom' | 'BottomA' | 'BottomB'
	| 'Chop' | 'ChopA' | 'ChopB'
	| 'Ears' | 'Eye' | 'Eyebrows' | 'Eyes'
	| 'Face' | 'Gloves'
	| 'Hair' | 'HairA' | 'HairB' | 'HairC' | 'HairD' | 'Head' | 'Horns'
	| 'Jacket' | 'JacketA' | 'JacketB'
	| 'Mid'
	| 'Reap'
	| 'Seed' | 'Shoe' | 'Shoes' | 'Shoulder' | 'ShoulderA' | 'ShoulderB'
	| 'Skintone' | 'Sock' | 'Socks'
	| 'Strike' | 'StrikeA' | 'StrikeB'
	| 'Top' | 'TopA' | 'TopB'
	| 'Water';

export type SpriteLayer =
	| 'AccessoryA' | 'AccessoryB' | 'AccessoryC' | 'AccessoryD'
	| 'Arms' | 'BackA' | 'BackB' | 'Body' | 'BottomA' | 'BottomB'
	| 'ChopA' | 'ChopB'
	| 'Ears' | 'Eyebrows' | 'Eyes'
	| 'Face' | 'Gloves'
	| 'HairA' | 'HairB' | 'HairC' | 'HairD' | 'Head' | 'Horns'
	| 'JacketA' | 'JacketB'
	| 'Mid'
	| 'Reap'
	| 'Seed' | 'Shoes' | 'ShoulderA' | 'ShoulderB' | 'Socks'
	| 'StrikeA' | 'StrikeB'
	| 'TopA' | 'TopB'
	| 'Water';

export type SpecialPalette = 'Skintone' | 'Eye' | 'Chop' | 'Strike' | 'Reap' | 'Water' | 'Seed';

export type PaletteCategoryData = {
	grayscale: string[];
	palettes?: string[][];
};

export type PaletteData = {
	palettes: string[][];
} & Record<string, PaletteCategoryData>;

export type PaletteState = Record<string, {
	category: string;
	colors: string[];
	row: number;
}>;

export type PaletteConfig = {
	grayscaleColors: string[];
	colorCount: number;
	colors: string[];
	_grayscaleKey?: string;
	_colorsKey?: string;
};

export type CharacterData = {
	name: string;
	desc: string;
	sprites: Record<string, number>;
	hidden: string[];
	paletteState: PaletteState;
};

export type AnimationState = {
	currentAnimation: string;
	currentDirection: Direction;
	currentFrame: number;
	frameTimer: number;
	isPlaying: boolean;
};

export type AtlasEntry = {
	u0: number;
	v0: number;
	w: number;
	h: number;
	ox: number;
	oy: number;
};

export type AtlasData = {
	image: HTMLImageElement;
	atlasWidth: number;
	atlasHeight: number;
	sheetCount: number;
	entryCount: number;
	entries: Uint16Array;
	sheetNames: string[];
	nameToOrdinal: Map<string, number>;
};
