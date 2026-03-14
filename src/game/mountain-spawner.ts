// === Mountain Decoration — procedural pixel-art mountains on the map ===
//
// Layer 1 (Macroworld). Feature type: Mountain.
// Purely visual: draws mountain icons in the map pass on cells marked
// as Mountain in the feature map (u_featureMap, FeatureType = 3).
// Height classification happens once during generation in features.ts.
//
// Each mountain sits at the bottom of its cell with a 2×2 lookup footprint
// centred on the cell. Sides overlap 0.5 into each neighbour; the tallest
// peaks extend 0.5 above the cell top. 16×16 pixel grid over the footprint.
//
// Exports MOUNTAIN_MAP_GLSL — a GLSL snippet for the map fragment shader.
// Expects uniforms: u_masterTexture, u_worldSeed, u_mapSize, u_mtnThreshold.
// Call mountainOverlay(mapUV, baseColor) after terrain blending.

export const MOUNTAIN_MAP_GLSL = /* glsl */ `
// 2D integer-style cell hash — processes x,y independently to avoid
// diagonal stripe artefacts from linear combination hashes.
float mtnHash2D(vec2 cell, float offset) {
	vec2 p = cell + offset;
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

// 1D pixel-level hash (small inputs only — used after pre-hashing seed)
float mtnHash(float n) {
	n = fract(n * 0.1031);
	n *= n + 33.33;
	n *= n + n;
	return fract(n);
}

// Draw mountain for a single cell. Returns vec4(color, drawn).
vec4 mtnDraw(vec2 cell, vec2 localUV, vec3 baseColor) {
	vec2 cellUV = (cell + 0.5) / u_mapSize;

	// Feature map: Mountain = 3 → 3/255 ≈ 0.01176
	float featureId = texture(u_featureMap, cellUV).r * 255.0;
	if (featureId < 2.5 || featureId > 3.5) return vec4(baseColor, 0.0);

	float height = texture(u_masterTexture, cellUV).r;
	float hParam = clamp(
		(height - u_mtnThreshold) / (1.0 - u_mtnThreshold), 0.0, 1.0);

	// Per-cell random values via 2D hash (no linear combination)
	float v1 = mtnHash2D(cell, u_worldSeed + 1.0);
	float v2 = mtnHash2D(cell, u_worldSeed + 2.0);
	float v3 = mtnHash2D(cell, u_worldSeed + 3.0);

	int mtype = int(mtnHash2D(cell, u_worldSeed + 7.0) * 4.0);

	// Flip Y so peak is at top; 16×16 pixel grid over the 2×2 cell footprint
	vec2 p = floor(vec2(localUV.x, 1.0 - localUV.y) * 16.0);
	// Per-pixel hash: use pre-hashed cell seed in small range
	float cs = mtnHash2D(cell, u_worldSeed) * 1e3;
	float ph = mtnHash(cs + p.x * 17.1 + p.y * 31.7);

	float cx = 8.0 + floor((v1 - 0.5) * 2.0);

	vec3 rock1, rock2, rock3, snow1, snow2, shadow;

	if (mtype == 0) {
		rock1 = vec3(128, 118, 105) / 255.0;
		rock2 = vec3(150, 140, 128) / 255.0;
		rock3 = vec3(105, 95, 85) / 255.0;
		snow1 = vec3(240, 245, 250) / 255.0;
		snow2 = vec3(210, 220, 235) / 255.0;
		shadow = vec3(75, 70, 65) / 255.0;
	} else if (mtype == 1) {
		rock1 = vec3(155, 130, 100) / 255.0;
		rock2 = vec3(175, 150, 115) / 255.0;
		rock3 = vec3(130, 108, 82) / 255.0;
		snow1 = vec3(238, 242, 248) / 255.0;
		snow2 = vec3(215, 225, 235) / 255.0;
		shadow = vec3(95, 78, 58) / 255.0;
	} else if (mtype == 2) {
		rock1 = vec3(72, 72, 78) / 255.0;
		rock2 = vec3(95, 92, 98) / 255.0;
		rock3 = vec3(55, 55, 62) / 255.0;
		snow1 = vec3(235, 240, 248) / 255.0;
		snow2 = vec3(200, 210, 225) / 255.0;
		shadow = vec3(38, 38, 45) / 255.0;
	} else {
		rock1 = vec3(145, 88, 68) / 255.0;
		rock2 = vec3(168, 105, 78) / 255.0;
		rock3 = vec3(120, 72, 55) / 255.0;
		snow1 = vec3(242, 240, 238) / 255.0;
		snow2 = vec3(220, 215, 210) / 255.0;
		shadow = vec3(85, 52, 38) / 255.0;
	}

	float peakH = (1.0 - hParam) * 9.0;
	float baseY = 12.0;
	float snowLine = peakH + (baseY - peakH) * (0.15 + 0.15 * (1.0 - hParam));

	vec3 col = baseColor;
	float drawn = 0.0;

	if (p.y >= peakH && p.y <= baseY) {
		float frac = (p.y - peakH) / (baseY - peakH);
		float halfW = 0.5 + frac * (4.0 + v2 * 2.0);
		float edgeNoise = (mtnHash(cs + p.y * 13.1 + 47.0) - 0.5) * 1.2;

		if (abs(p.x - cx) <= halfW + edgeNoise) {
			vec3 rc = ph < 0.33 ? rock1 : ph < 0.66 ? rock2 : rock3;
			float side = (p.x - cx) / max(halfW, 0.01);
			if (side < -0.3) rc *= 1.12;
			else if (side > 0.3) rc *= 0.78;
			rc *= 1.0 - frac * 0.2;

			float ridgeN = mtnHash(cs + p.x * 23.7 + p.y * 11.3);
			if (ridgeN > 0.88) rc *= 1.25;
			if (abs(p.x - cx) > halfW + edgeNoise - 1.0) rc = shadow;

			col = rc;
			drawn = 1.0;

			if (hParam > 0.4 && p.y < snowLine) {
				float snowN
					= (mtnHash(cs + p.x * 31.3 + p.y * 7.7) - 0.5) * 1.5;
				if (p.y < snowLine + snowN) {
					col = ph < 0.5 ? snow1 : snow2;
					if (side > 0.3) col *= 0.88;
				}
			}
		}
	}

	if (p.y == 13.0 && drawn < 0.5) {
		float shadowW = 1.5 + hParam * 2.5;
		if (abs(p.x - cx) <= shadowW) {
			col = mix(baseColor, vec3(0.0), 0.25);
			drawn = 1.0;
		}
	}

	if (hParam > 0.55) {
		float cx2 = cx + 3.0 * (v3 > 0.5 ? 1.0 : -1.0);
		float peakH2 = peakH + 2.0 + v3 * 2.0;
		float baseY2 = baseY - 1.0;
		if (p.y >= peakH2 && p.y <= baseY2) {
			float frac2 = (p.y - peakH2) / (baseY2 - peakH2);
			float halfW2 = 0.3 + frac2 * (2.0 + v3);
			float edgeN2 = (mtnHash(cs + p.y * 19.3 + 91.0) - 0.5) * 0.8;
			if (abs(p.x - cx2) <= halfW2 + edgeN2) {
				vec3 rc = ph < 0.5 ? rock1 : rock2;
				float side2 = (p.x - cx2) / max(halfW2, 0.01);
				if (side2 < -0.3) rc *= 1.10;
				else if (side2 > 0.3) rc *= 0.80;
				rc *= 1.0 - frac2 * 0.18;
				if (abs(p.x - cx2) > halfW2 + edgeN2 - 1.0) rc = shadow;
				col = rc;
				drawn = 1.0;

				float snowLine2 = peakH2 + (baseY2 - peakH2) * 0.25;
				if (hParam > 0.65 && p.y < snowLine2) {
					col = ph < 0.5 ? snow1 : snow2;
					if (side2 > 0.3) col *= 0.88;
				}
			}
		}
	}

	return vec4(col, drawn);
}

vec3 mountainOverlay(vec2 mapUV, vec3 baseColor) {
	vec2 worldPos = mapUV * u_mapSize;
	// Each mountain spans 2×2 cells centred on its cell.
	// Check which 2×2 grid of anchor cells could overlap this pixel.
	vec2 baseCell = floor(worldPos - 0.5);
	vec3 col = baseColor;

	for (int dy = 0; dy <= 1; dy++) {
		for (int dx = 0; dx <= 1; dx++) {
			vec2 cell = mod(
				baseCell + vec2(float(dx), float(dy)), u_mapSize);

			// Torus-safe offset from cell centre
			vec2 diff = worldPos - (cell + 0.5);
			if (diff.x > u_mapSize.x * 0.5) diff.x -= u_mapSize.x;
			if (diff.x < -u_mapSize.x * 0.5) diff.x += u_mapSize.x;
			if (diff.y > u_mapSize.y * 0.5) diff.y -= u_mapSize.y;
			if (diff.y < -u_mapSize.y * 0.5) diff.y += u_mapSize.y;

			// Map [-1,1] → [0,1] within the 2×2 footprint
			vec2 localUV = (diff + 1.0) / 2.0;
			if (localUV.x < 0.0 || localUV.x >= 1.0
				|| localUV.y < 0.0 || localUV.y >= 1.0) continue;

			vec4 m = mtnDraw(cell, localUV, col);
			if (m.a > 0.5) col = m.rgb;
		}
	}

	return col;
}
`;
