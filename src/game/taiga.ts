// === Taiga — procedural macroworld ground texture ===
// Cold + medium moisture. Dark coniferous forest floor, needle litter.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const TAIGA_BIOME_GLSL = /* glsl */ `
vec3 bt_taiga(vec2 wp, float sd) {
	wp += sd * 0.23;
	float needles = bt_fbm(wp * 0.12, 2);
	float undergrowth = bt_noise(wp * 0.05 + 50.0);
	float bark = bt_hash(wp) * 0.03;
	vec3 m = vec3(0.90 + needles * 0.08, 0.94 + undergrowth * 0.10, 0.88 + needles * 0.06);
	m += bark;
	float darkPatches = smoothstep(0.55, 0.40, undergrowth);
	m *= 1.0 - darkPatches * 0.08;
	return m;
}
`;
