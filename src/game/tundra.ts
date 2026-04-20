// === Tundra — procedural macroworld ground texture ===
// Cold + dry biome. Sparse grey-green lichen on exposed rocky ground.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const TUNDRA_BIOME_GLSL = /* glsl */ `
vec3 bt_tundra(vec2 wp, float sd) {
	wp += sd * 0.17;
	float lichen = bt_fbm(wp * 0.06, 3);
	float rock = bt_noise(wp * 0.18 + 30.0);
	float grain = bt_hash(wp) * 0.04;
	float patches = smoothstep(0.35, 0.65, lichen);
	vec3 lichenMod = vec3(0.94, 0.96, 0.88);
	vec3 rockMod = vec3(0.88, 0.86, 0.84);
	vec3 m = mix(rockMod, lichenMod, patches);
	m += grain;
	m += rock * 0.06;
	return m;
}
`;
