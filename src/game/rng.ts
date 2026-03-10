// === Seeded RNG ===
// Single source of truth for deterministic pseudo-random number generation.
// xorshift32: 3 shifts + 3 XORs per call, period 2^32-1, no multiply.

export function xorshift32(seed: number): () => number {
	// eslint-disable-next-line unicorn/prefer-math-trunc -- 32-bit coercion, not truncation; || 1 guards zero fixed-point
	let s = (seed | 0) || 1;

	return () => {
		s ^= s << 13;
		s ^= s >>> 17;
		s ^= s << 5;
		return (s >>> 0) / 4_294_967_296;
	};
}
