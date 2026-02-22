export const ANIMATION_FRAME_COUNTS: Record<string, number> = {
	idle: 4,
	walk: 6,
	run: 6,
	pickup: 4,
	strike: 4,
	chop: 4,
	seed: 3,
	water: 4,
	reap: 4,
};

export const ANIMATION_START_INDICES: Record<string, number> = {
	idle: 0,
	walk: 16,
	run: 40,
	pickup: 64,
	strike: 80,
	chop: 96,
	seed: 112,
	water: 124,
	reap: 140,
};

export const ANIMATION_FRAME_DELAYS: Record<string, number[]> = {
	idle: [500, 300, 200, 200],
	walk: [125, 125, 125, 125, 125, 125],
	run: [100, 100, 100, 100, 100, 100],
	pickup: [125, 125, 125, 500],
	strike: [200, 125, 125, 200],
	chop: [200, 125, 150, 200],
	seed: [300, 125, 200],
	water: [500, 125, 125, 500, 125, 125],
	reap: [200, 125, 125, 200],
};
