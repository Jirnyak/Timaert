// === Valley — procedural macroworld ground texture ===
// Temperate + dry. Mixed grassland with exposed earth and stone patches.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const VALLEY_BIOME_GLSL = /* glsl */ `
vec3 bt_valley(vec2 wp, float sd) {
	wp += sd * 0.19;
	float grass = bt_fbm(wp * 0.09, 3);
	float earth = bt_noise(wp * 0.22 + 40.0);
	float stones = step(0.88, bt_noise(wp * 0.45 + 15.0));
	float grain = bt_hash(wp) * 0.03;
	vec3 grassMod = vec3(0.93, 0.98, 0.88);
	vec3 earthMod = vec3(0.98, 0.93, 0.86);
	vec3 m = mix(grassMod, earthMod, smoothstep(0.4, 0.6, earth));
	m += grass * 0.06;
	m += grain;
	m -= stones * 0.06;
	return m;
}
`;
