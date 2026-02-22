import type {PaletteState} from './types';

// Default sprite indices for a new character (most start at 0)
export const DEFAULT_SPRITE_INDICES: Record<string, number> = {
	Body: 0,
	Head: 0,
	Arms: 0,
	Eyes: 0,
	HairA: 0,
	HairB: 0,
	HairC: 0,
	HairD: 0,
	TopA: 0,
	TopB: 0,
	BottomA: 0,
	BottomB: 0,
	JacketA: 0,
	JacketB: 0,
	Shoes: 0,
	Socks: 0,
	AccessoryA: 0,
	AccessoryB: 0,
	AccessoryC: 0,
	AccessoryD: 0,
	Ears: 0,
	Eyebrows: 0,
	Face: 0,
	Gloves: 0,
	Horns: 0,
	Mid: 0,
	BackA: 0,
	BackB: 0,
	ShoulderA: 0,
	ShoulderB: 0,
	ChopA: 0,
	ChopB: 0,
	StrikeA: 0,
	StrikeB: 0,
	Reap: 0,
	Water: 0,
	Seed: 0,
};

const COMMON_COLORS = ['411310', '5b1610', '8b1b16', 'b22c2e'];
const TOOL_COLORS = ['4b4644', '6e6a69', '9a9290', 'cfc5c3'];

function builtIn(colors: string[], row = 0) {
	return {category: 'Built-in', colors, row};
}

export const DEFAULT_PALETTE_STATE: PaletteState = {
	AccessoryA: builtIn(COMMON_COLORS),
	AccessoryB: builtIn(COMMON_COLORS),
	AccessoryC: builtIn(COMMON_COLORS),
	AccessoryD: builtIn(COMMON_COLORS),
	Bottom: builtIn(COMMON_COLORS),
	Eye: builtIn(['411310', '5b1610', '8b1b16', 'fff0f7']),
	Hair: builtIn(COMMON_COLORS),
	Jacket: builtIn(COMMON_COLORS),
	Shoe: builtIn(COMMON_COLORS),
	Skintone: builtIn(['834545', 'ec9e9e', 'fbc3c3', 'f6ddd6']),
	Sock: builtIn(COMMON_COLORS),
	Top: builtIn(COMMON_COLORS),
	Shoulder: builtIn(COMMON_COLORS),
	Back: builtIn(COMMON_COLORS),
	Eyebrows: builtIn(COMMON_COLORS),
	Face: builtIn(COMMON_COLORS),
	Horns: builtIn(COMMON_COLORS),
	Mid: builtIn(COMMON_COLORS),
	Gloves: builtIn(COMMON_COLORS),
	Chop: builtIn(TOOL_COLORS),
	Strike: builtIn(TOOL_COLORS),
	Reap: builtIn(TOOL_COLORS),
	Water: builtIn(TOOL_COLORS),
	Seed: builtIn(TOOL_COLORS),
};
