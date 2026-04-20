// === Water — procedural macroworld ground texture ===
// Ocean / lake surface. Caustic-like patterns with depth variation.
// Two functions: bt_water for deep water modulation,
// bt_shore for shoreline rendering (sand + foam + waves).

export const WATER_BIOME_GLSL = /* glsl */ `
vec3 bt_water(vec2 wp, float sd) {
	wp += sd * 0.11;
	float caustic = bt_noise(wp * 0.12 + 5.0) * bt_noise(wp * 0.18 + 65.0);
	float depth = bt_noise(wp * 0.03);
	float ripple = bt_noise(wp * 0.25 + 120.0);
	vec3 m = vec3(0.94 + caustic * 0.08, 0.96 + caustic * 0.06 + depth * 0.04, 1.00 + ripple * 0.03);
	m *= 1.0 - depth * 0.04;
	return m;
}
`;
