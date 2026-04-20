// === Meadow — procedural macroworld ground texture ===
// Temperate + medium moisture. Lush green grass with wildflower dots.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const MEADOW_BIOME_GLSL = /* glsl */ `
vec3 bt_meadow(vec2 wp, float sd) {
	wp += sd * 0.13;
	float grass = bt_fbm(wp * 0.1, 3);
	float sway = bt_noise(wp * 0.04 + 60.0);
	float grain = bt_hash(wp) * 0.025;
	vec3 m = vec3(0.92 + sway * 0.06, 0.97 + grass * 0.06, 0.90 + sway * 0.04);
	m += grain;
	float flower = bt_hash(wp + 99.0);
	if (flower > 0.96) {
		float kind = bt_hash(wp + 200.0);
		if (kind < 0.33) m += vec3(0.12, 0.02, 0.0);
		else if (kind < 0.66) m += vec3(0.06, 0.06, 0.12);
		else m += vec3(0.12, 0.10, 0.0);
	}
	return m;
}
`;
