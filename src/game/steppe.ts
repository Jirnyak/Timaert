// === Steppe — procedural macroworld ground texture ===
// Hot + medium moisture. Dry golden grass with wind-swept patterns.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const STEPPE_BIOME_GLSL = /* glsl */ `
vec3 bt_steppe(vec2 wp, float sd) {
	wp += sd * 0.37;
	float wind = bt_noise(vec2(wp.x * 0.10, wp.y * 0.03) + 45.0);
	float tufts = bt_fbm(wp * 0.14, 2);
	float grain = bt_hash(wp) * 0.025;
	vec3 m = vec3(0.98 + wind * 0.05, 0.96 + tufts * 0.06, 0.90 + wind * 0.03);
	m += grain;
	float bare = smoothstep(0.62, 0.68, tufts);
	m += bare * vec3(0.04, 0.02, 0.0);
	return m;
}
`;
