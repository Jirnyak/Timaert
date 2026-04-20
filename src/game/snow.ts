// === Snow — procedural macroworld ground texture ===
// Cold + wet. White with subtle blue shadows and wind drift lines.
// Returns vec3 color modulation (~1.0) for multiplicative blending.

export const SNOW_BIOME_GLSL = /* glsl */ `
vec3 bt_snow(vec2 wp, float sd) {
	wp += sd * 0.31;
	float drift = bt_noise(vec2(wp.x * 0.14 + wp.y * 0.04, wp.y * 0.08) + 20.0);
	float detail = bt_noise(wp * 0.3 + 70.0);
	float sparkle = step(0.965, bt_hash(wp));
	vec3 m = vec3(0.97 + drift * 0.04, 0.97 + drift * 0.03, 0.99 + drift * 0.02);
	m -= detail * 0.03;
	m += sparkle * 0.06;
	m.b += 0.01;
	return m;
}
`;
