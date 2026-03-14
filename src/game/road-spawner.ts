// === Road Decoration — procedural road overlay on the map ===
//
// Layer 1 (Macroworld). Feature type: Road.
// Purely visual: draws road surface on cells marked Road in featureMap.
//
// Neighbour-aware: samples 8 adjacent cells to determine road direction.
// Computes per-pixel distance to road centreline segments connecting
// the cell centre to each connected neighbour edge. Uses a 16×16
// virtual pixel grid for pixel-art crispness (same approach as mountains).
//
// Exports ROAD_MAP_GLSL — a GLSL snippet for the map fragment shader.
// Expects uniforms: u_masterTexture, u_featureMap, u_worldSeed, u_mapSize.
// Call roadOverlay(mapUV, baseColor) after terrain texture blending.

export const ROAD_MAP_GLSL = /* glsl */ `
float roadHash(float n) {
	n = fract(n * 0.1031);
	n *= n + 33.33;
	n *= n + n;
	return fract(n);
}

// Distance from point p to line segment a→b
float roadLineDist(vec2 p, vec2 a, vec2 b) {
	vec2 ab = b - a;
	float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
	return length(p - (a + ab * t));
}

// Check if a neighbouring cell is a road (torus-safe)
bool roadAt(vec2 cell) {
	vec2 uv = mod(cell + 0.5, u_mapSize) / u_mapSize;
	float fid = texture(u_featureMap, uv).r * 255.0;
	return fid > 0.5 && fid < 1.5;
}

vec3 roadOverlay(vec2 mapUV, vec3 baseColor) {
	vec2 pixelCoord = mapUV * u_mapSize;
	vec2 cell = floor(pixelCoord);
	vec2 cellUV = (cell + 0.5) / u_mapSize;

	float featureId = texture(u_featureMap, cellUV).r * 255.0;
	if (featureId < 0.5 || featureId > 1.5) return baseColor;

	// 16×16 virtual pixel grid for pixel-art crispness
	vec2 p = floor(fract(pixelCoord) * 16.0) + 0.5;
	vec2 ctr = vec2(8.0);

	// Min distance to road centreline across all connections
	float md = 999.0;
	bool connected = false;

	// Cardinal neighbours — edge midpoints
	if (roadAt(cell + vec2(0, -1))) {
		md = min(md, roadLineDist(p, ctr, vec2(8.0, 0.0)));
		connected = true;
	}
	if (roadAt(cell + vec2(0, 1))) {
		md = min(md, roadLineDist(p, ctr, vec2(8.0, 16.0)));
		connected = true;
	}
	if (roadAt(cell + vec2(1, 0))) {
		md = min(md, roadLineDist(p, ctr, vec2(16.0, 8.0)));
		connected = true;
	}
	if (roadAt(cell + vec2(-1, 0))) {
		md = min(md, roadLineDist(p, ctr, vec2(0.0, 8.0)));
		connected = true;
	}

	// Diagonal neighbours — corner points
	if (roadAt(cell + vec2(1, -1))) {
		md = min(md, roadLineDist(p, ctr, vec2(16.0, 0.0)));
		connected = true;
	}
	if (roadAt(cell + vec2(-1, -1))) {
		md = min(md, roadLineDist(p, ctr, vec2(0.0, 0.0)));
		connected = true;
	}
	if (roadAt(cell + vec2(1, 1))) {
		md = min(md, roadLineDist(p, ctr, vec2(16.0, 16.0)));
		connected = true;
	}
	if (roadAt(cell + vec2(-1, 1))) {
		md = min(md, roadLineDist(p, ctr, vec2(0.0, 16.0)));
		connected = true;
	}

	// Isolated cell fallback: small dot
	if (!connected) md = length(p - ctr);

	// Road width: wider for major roads
	float gpuRoad = texture(u_masterTexture, cellUV).a;
	float hw = gpuRoad > 0.45 ? 3.0 : 2.2;

	if (md > hw) return baseColor;

	// Per-pixel hash (include cell coords for per-cell variation)
	float cs = cell.x * 127.1 + cell.y * 311.7 + u_worldSeed;
	float ph = roadHash(cs + p.x * 17.31 + p.y * 43.77);

	// Edge factor [0 = centre, 1 = edge]
	float edge = smoothstep(hw - 1.2, hw, md);

	// ── Road palette ──
	vec3 roadColor;

	if (gpuRoad > 0.45) {
		// Major: cobblestone
		vec3 stone1 = vec3(0.50, 0.48, 0.44);
		vec3 stone2 = vec3(0.45, 0.43, 0.40);
		roadColor = ph < 0.5 ? stone1 : stone2;

		// Offset cobble grid (3-pixel cells, brick-lay pattern)
		vec2 sp = p;
		if (mod(floor(p.y / 3.0), 2.0) > 0.5) sp.x += 1.5;
		vec2 sc = fract(sp / 3.0);
		if (sc.x < 0.18 || sc.y < 0.18) roadColor *= 0.80;

		if (ph > 0.85) roadColor *= 0.75;
	} else {
		// Minor: dirt path
		vec3 dirt1 = vec3(0.52, 0.42, 0.30);
		vec3 dirt2 = vec3(0.48, 0.38, 0.26);
		vec3 dirt3 = vec3(0.56, 0.44, 0.33);
		roadColor = ph < 0.33 ? dirt1 : ph < 0.66 ? dirt2 : dirt3;

		// Wheel tracks — two parallel lines along road
		if (abs(md - hw * 0.45) < 0.55) roadColor *= 0.84;

		// Surface noise
		float tn = roadHash(cs + p.x * 7.13 + p.y * 11.31 + 100.0);
		if (tn > 0.82) roadColor *= 0.88;
	}

	// Edge darkening + grass bleed
	vec3 edgeCol = vec3(0.30, 0.25, 0.18);
	roadColor = mix(roadColor, edgeCol, edge * 0.5);
	float opacity = 0.85 - edge * 0.35;

	return mix(baseColor, roadColor, opacity);
}
`;
