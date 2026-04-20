// === Desert — procedural macroworld ground texture ===
// Hot + dry. Sandy dunes with directional wind ripple patterns.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const DESERT_BIOME_GLSL = /* glsl */ `
vec3 bt_desert(vec2 wp, float sd) {
	wp += sd * 0.21;
	float ripple = bt_noise(vec2(wp.x * 0.12 + wp.y * 0.03, wp.y * 0.06) + 35.0);
	float dune = bt_noise(wp * 0.04 + 90.0);
	float grain = bt_hash(wp) * 0.025;
	vec3 m = vec3(1.00 + ripple * 0.06, 0.97 + ripple * 0.04, 0.92 + dune * 0.04);
	m += dune * vec3(0.04, 0.02, 0.0);
	m += grain;
	float shadow = smoothstep(0.55, 0.45, ripple);
	m *= 1.0 - shadow * 0.04;
	return m;
}
`;
