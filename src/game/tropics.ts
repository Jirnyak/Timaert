// === Tropics — procedural macroworld ground texture ===
// Hot + wet. Dense dark green vegetation with canopy gaps.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const TROPICS_BIOME_GLSL = /* glsl */ `
vec3 bt_tropics(vec2 wp, float sd) {
	wp += sd * 0.41;
	float canopy = bt_fbm(wp * 0.11, 3);
	float gap = smoothstep(0.58, 0.68, bt_noise(wp * 0.20 + 55.0));
	float leaf = bt_noise(wp * 0.35 + 10.0);
	float grain = bt_hash(wp) * 0.02;
	vec3 m = vec3(0.88 + canopy * 0.08, 0.94 + leaf * 0.06, 0.86 + canopy * 0.05);
	m += gap * vec3(0.08, 0.10, 0.04);
	m += grain;
	return m;
}
`;
