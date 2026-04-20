// === Swamp — procedural macroworld ground texture ===
// Temperate + wet. Dark murky ground with standing water patches.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const SWAMP_BIOME_GLSL = /* glsl */ `
vec3 bt_swamp(vec2 wp, float sd) {
	wp += sd * 0.29;
	float murk = bt_fbm(wp * 0.08, 3);
	float pool = smoothstep(0.42, 0.32, bt_noise(wp * 0.15 + 25.0));
	float moss = bt_noise(wp * 0.28 + 80.0);
	float grain = bt_hash(wp) * 0.03;
	vec3 m = vec3(0.88 + murk * 0.08, 0.92 + moss * 0.08, 0.86 + murk * 0.05);
	m -= pool * vec3(0.06, 0.04, 0.02);
	m *= 1.0 - pool * 0.10;
	m += grain;
	return m;
}
`;
