// === Subworld Sky Shaders — first-person procedural sky for 3D renderer ===
//
// Fullscreen-quad fragment shader: day/night gradient, sun disc with glow,
// procedural moons, twinkling stars, animated FBM clouds.
// Style: pixel-retro minimalist — functional beauty, ragged noise edges.
//
// Used by renderer-3d.ts which owns the GL context and quad geometry.
// Uniforms supplied by renderer-3d: u_tod, u_elapsed, u_seed, u_yaw, u_pitch,
// u_fov, u_aspect.

// ── Vertex Shader ───────────────────────────────────────────────

export const SKY_VS = /* glsl */ `#version 300 es
precision highp float;
in vec2 a_pos;
out vec2 v_uv;
void main() {
	v_uv = a_pos * 0.5 + 0.5;
	gl_Position = vec4(a_pos, 0.9999, 1.0);
}`;

// ── Fragment Shader ─────────────────────────────────────────────

export const SKY_FS = /* glsl */ `#version 300 es
precision highp float;

out vec4 fragColor;
in vec2 v_uv;

uniform float u_tod;     // 0..1 (0 = midnight, 0.5 = noon)
uniform float u_elapsed; // real seconds for animation
uniform float u_seed;    // world seed
uniform float u_yaw;     // camera yaw radians
uniform float u_pitch;   // camera pitch radians
uniform float u_fov;     // vertical FOV radians
uniform float u_aspect;  // viewport width / height
uniform vec3  u_fogColor; // renderer fog colour for seamless horizon

/* ── hash / noise ── */

float h21(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

float h11(float p) {
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(h21(i), h21(i + vec2(1, 0)), f.x),
		mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), f.x),
		f.y);
}

float fbm4(vec2 p) {
	float v = 0.0, a = 0.5;
	mat2 r = mat2(0.8, 0.6, -0.6, 0.8);
	for (int i = 0; i < 4; i++) { v += a * vnoise(p); p = r * p * 2.0; a *= 0.5; }
	return v;
}

float fbm3(vec2 p) {
	float v = 0.0, a = 0.5;
	mat2 r = mat2(0.8, 0.6, -0.6, 0.8);
	for (int i = 0; i < 3; i++) { v += a * vnoise(p); p = r * p * 2.0; a *= 0.5; }
	return v;
}

/* ── reconstruct world-space view ray from screen UV ── */

vec3 viewRay(vec2 uv) {
	float tanHF = tan(u_fov * 0.5);
	// NDC range is -1..1, UV is 0..1, so multiply offset by 2
	float x = (uv.x * 2.0 - 1.0) * u_aspect * tanHF;
	float y = (uv.y * 2.0 - 1.0) * tanHF;
	// Camera basis matching renderer-3d.ts lookAt convention
	float cy = cos(u_yaw), sy = sin(u_yaw);
	float cp = cos(u_pitch), sp = sin(u_pitch);
	vec3 forward = vec3(cy * cp, sp, sy * cp);
	vec3 right   = vec3(-sy, 0.0, cy);
	vec3 up      = cross(right, forward);
	return normalize(forward + x * right + y * up);
}

void main() {
	vec3 rd = viewRay(v_uv);
	float t = u_tod;

	// Elevation above horizon: 0 = horizon, 1 = zenith, <0 = below
	float elev = rd.y;

	// ── Time-of-day phases ──
	float dayF   = clamp(smoothstep(0.22, 0.35, t) - smoothstep(0.65, 0.78, t), 0.0, 1.0);
	float nightF = 1.0 - dayF;
	float dawn   = smoothstep(0.20, 0.26, t) * smoothstep(0.35, 0.28, t);
	float dusk   = smoothstep(0.65, 0.72, t) * smoothstep(0.80, 0.74, t);
	float twilight = dawn + dusk;

	// ── 1. Sky gradient ──
	// Deep blue zenith → pale horizon by day; dark blue → near-black at night
	vec3 zenithDay  = vec3(0.18, 0.30, 0.62);
	vec3 horizDay   = vec3(0.58, 0.68, 0.82);
	vec3 zenithNight = vec3(0.01, 0.01, 0.04);
	vec3 horizNight  = vec3(0.04, 0.04, 0.08);
	vec3 twiCol      = vec3(0.60, 0.25, 0.08);

	float he = clamp(elev, 0.0, 1.0);
	vec3 skyDay   = mix(horizDay,   zenithDay,   he);
	vec3 skyNight = mix(horizNight,  zenithNight, he);
	vec3 col = mix(skyNight, skyDay, dayF);
	// Twilight warm band near horizon
	col = mix(col, twiCol, twilight * smoothstep(0.25, 0.0, he) * 0.7);

	// Below horizon — blend to fog colour for seamless terrain match
	if (elev < 0.0) {
		col = mix(col, u_fogColor, smoothstep(0.0, -0.12, elev));
	}

	// ── 2. Sun ──
	float sunAng = (t - 0.25) * 6.28318;
	vec3  sunDir = normalize(vec3(cos(sunAng), sin(sunAng), 0.0));
	float sunVis = smoothstep(0.22, 0.30, t) * smoothstep(0.78, 0.70, t);

	float sunDot = dot(rd, sunDir);
	// Hard inner disc — small bright core
	float disc = smoothstep(0.9992, 0.9996, sunDot) * 1.0;
	// Soft glow ring
	float glow = pow(max(sunDot, 0.0), 256.0) * 0.6;
	// Wider atmospheric scatter
	float scatter = pow(max(sunDot, 0.0), 8.0) * 0.12;

	vec3 sunCol = mix(vec3(1.0, 0.45, 0.10), vec3(1.0, 0.92, 0.7), dayF);
	col += sunCol * (disc + glow + scatter) * sunVis;

	// ── 3. Moons ──
	float seedBits = fract(abs(u_seed) * 0.00013751);
	int mCount = 1 + int(floor(seedBits * 3.0));
	float moonVis = clamp(nightF * 1.4, 0.0, 1.0);

	for (int m = 0; m < 3; m++) {
		if (m >= mCount) break;
		float ms = abs(u_seed) + float(m) * 1337.7;
		// Moon angular radius (apparent size on sky)
		float mRad = 0.02 + h11(ms * 0.137) * 0.015;
		// Moon orbit — opposite side from sun, offset per moon
		float mAng = (t - 0.75 + h11(ms * 0.419) * 0.25) * 6.28318
		           + float(m) * 2.094;
		vec3 moonDir = normalize(vec3(
			cos(mAng),
			abs(sin(mAng)) * 0.7 + 0.2,
			sin(mAng) * 0.4));
		float mDot = dot(rd, moonDir);
		float mD = acos(clamp(mDot, -1.0, 1.0));

		// Disc with noise-roughened edge
		float edgeNoise = vnoise(rd.xz * 60.0 + ms) * 0.006;
		float moonDisc = smoothstep(mRad + edgeNoise, mRad * 0.8, mD);

		// Surface detail — craters
		float surf = vnoise(rd.xz / mRad * 4.0 + ms * 0.1) * 0.15;

		vec3 mCol = vec3(
			0.72 + h11(ms * 7.0) * 0.12,
			0.74 + h11(ms * 11.0) * 0.10,
			0.80 + h11(ms * 13.0) * 0.12);
		mCol -= surf;

		// Soft glow around moon
		float mGlow = exp(-mD * mD / (mRad * mRad) * 3.0) * 0.08;

		col = mix(col, mCol, moonDisc * moonVis);
		col += vec3(0.6, 0.65, 0.8) * mGlow * moonVis;
	}

	// ── 4. Stars ──
	if (nightF > 0.05 && elev > -0.02) {
		float stars = 0.0;
		// Two density layers
		for (int L = 0; L < 2; L++) {
			float sc = 80.0 + float(L) * 60.0;
			vec2 suv = rd.xz / (elev + 0.15) * sc + u_seed * 0.001 * float(L + 1);
			vec2 g = floor(suv);
			vec2 f = fract(suv) - 0.5;
			float hv = h21(g + float(L) * 100.0);
			if (hv > 0.91) {
				float b = (hv - 0.91) / 0.09;
				float sz = 0.04 + b * 0.06;
				float d = length(f);
				if (d < sz) {
					float tw = 0.5 + 0.5 * sin(u_elapsed * (1.5 + b * 3.0) + hv * 80.0);
					stars += smoothstep(sz, 0.0, d) * b * tw / float(L + 1);
				}
			}
		}

		float starI = stars * nightF * 0.9;
		// Slight colour variation — warm/cool
		vec3 starCol = vec3(0.75 + stars * 0.2, 0.80 + stars * 0.1, 1.0);
		col += starCol * starI;
	}

	// ── 5. Clouds — FBM noise drifting across the sky dome ──
	if (elev > -0.05) {
		// Project ray onto dome for cloud UV
		float domeH = max(elev + 0.05, 0.01);
		vec2 cloudUV = rd.xz / domeH;
		float wx = u_elapsed * 0.008;
		float wy = u_elapsed * 0.003;

		// Primary cloud layer — large formations
		float c1 = smoothstep(0.42, 0.72, fbm4(cloudUV * 0.6 + vec2(wx, wy)));
		// Secondary — wispy detail
		float c2 = smoothstep(0.48, 0.80, fbm3(cloudUV * 1.4 + vec2(wx * 1.6, -wy * 0.5) + 40.0)) * 0.35;

		float clouds = clamp(c1 + c2, 0.0, 1.0);

		// Cloud colour — bright day, dark night, orange twilight
		vec3 cCol = mix(vec3(0.06, 0.06, 0.10), vec3(0.92, 0.92, 0.96), dayF);
		cCol = mix(cCol, vec3(0.95, 0.55, 0.20), twilight * 0.6);

		// Sun-lit cloud edges
		float cloudSunDot = dot(normalize(vec3(cloudUV, 1.0)), sunDir);
		float cloudLight = pow(max(cloudSunDot, 0.0), 4.0) * 0.15 * sunVis;
		cCol += sunCol * cloudLight;

		// Fade clouds near horizon to avoid hard cut
		float cloudFade = smoothstep(-0.05, 0.08, elev);
		float cloudAlpha = clouds * mix(0.55, 0.35, dayF) * cloudFade;

		col = mix(col, cCol, cloudAlpha);
	}

	fragColor = vec4(col, 1.0);
}`;

// ── Uniform setter helper ───────────────────────────────────────

/** Cached uniform locations for sky shader. */
export type SkyUniforms = {
	tod: WebGLUniformLocation;
	elapsed: WebGLUniformLocation;
	seed: WebGLUniformLocation;
	yaw: WebGLUniformLocation;
	pitch: WebGLUniformLocation;
	fov: WebGLUniformLocation;
	aspect: WebGLUniformLocation;
	fogColor: WebGLUniformLocation;
};

/** Resolve all sky uniform locations from a compiled program. */
export function getSkyUniforms(gl: WebGL2RenderingContext, prog: WebGLProgram): SkyUniforms {
	const loc = (name: string) => gl.getUniformLocation(prog, name)!;
	return {
		tod: loc('u_tod'),
		elapsed: loc('u_elapsed'),
		seed: loc('u_seed'),
		yaw: loc('u_yaw'),
		pitch: loc('u_pitch'),
		fov: loc('u_fov'),
		aspect: loc('u_aspect'),
		fogColor: loc('u_fogColor'),
	};
}
