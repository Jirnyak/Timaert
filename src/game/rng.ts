// === Seeded RNG utilities ===
// Single source of truth for deterministic pseudo-random number generators.

/**
 * Lehmer / Park-Miller PRNG.
 * Returns a closure that yields values in (0, 1).
 */
export function lehmerRng(seed: number): () => number {
	let s = seed;
	return () => {
		s = (s * 16_807 + 0) % 2_147_483_647;
		return s / 2_147_483_647;
	};
}

/**
 * Linear Congruential Generator (glibc constants).
 * Returns a closure that yields values in [0, 1).
 */
export function lcgRng(seed: number): () => number {
	let s = seed;
	return () => {
		s = (s * 1_103_515_245 + 12_345) & 0x7F_FF_FF_FF;
		return s / 0x7F_FF_FF_FF;
	};
}
