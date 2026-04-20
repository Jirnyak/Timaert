// === First-person camera for subworld raycaster ===
//
// Tracks position (x, y) on the 2D plane and height (z) above terrain.
// Orientation is yaw (horizontal angle) + pitch (vertical tilt).
// Samples heightmap for ground tracking and provides ray generation.

/** First-person camera state. */
export type CameraState = {
	/** X position on the 1024×1024 plane. */
	x: number;
	/** Y position on the 1024×1024 plane. */
	y: number;
	/** Height above sea level. */
	z: number;
	/** Horizontal look angle in radians (0 = +X, π/2 = +Y). */
	yaw: number;
	/** Vertical look angle in radians (positive = up). Clamped to ±π/3. */
	pitch: number;
};

/** Eye height above ground level. */
export const EYE_HEIGHT = 2;

/** Horizontal field of view in radians (~75°). */
export const FOV = 1.309;

/** Max pitch angle (60°). */
const MAX_PITCH = Math.PI / 3;

/** Create a default camera at a given spawn point. */
export function createCamera(x: number, y: number): CameraState {
	return {
		x, y, z: EYE_HEIGHT, yaw: 0, pitch: 0,
	};
}

/**
 * Sample heightmap at fractional coordinates using bilinear interpolation.
 * Returns 0 for out-of-bounds positions.
 */
export function sampleHeight(
	heightmap: Float32Array, width: number, height: number,
	x: number, y: number,
): number {
	const fx = Math.max(0, Math.min(width - 1.001, x));
	const fy = Math.max(0, Math.min(height - 1.001, y));
	const ix = Math.floor(fx);
	const iy = Math.floor(fy);
	const dx = fx - ix;
	const dy = fy - iy;
	const ix1 = Math.min(ix + 1, width - 1);
	const iy1 = Math.min(iy + 1, height - 1);
	const h00 = heightmap[iy * width + ix];
	const h10 = heightmap[iy * width + ix1];
	const h01 = heightmap[iy1 * width + ix];
	const h11 = heightmap[iy1 * width + ix1];
	return h00 * (1 - dx) * (1 - dy)
		+ h10 * dx * (1 - dy)
		+ h01 * (1 - dx) * dy
		+ h11 * dx * dy;
}

/**
 * Height scale: heightmap values (0–1) mapped to world units.
 *
 * Derivation: 1 tile ≈ 1m (player walks ~5 tiles/s ≈ 5 m/s).
 * Cell = 1024 tiles ≈ 1 km.  EYE_HEIGHT = 1.8 (person-scale).
 * At 400: mountains peak ≈ 400 m, coast drops ≈ 40 m,
 * flat terrain rolls ±3 m — proportional to real-world 1 km terrain.
 */
export const HEIGHT_SCALE = 500;

/**
 * Update camera z to track terrain below the player.
 * Applies gravity-like snapping with optional flying override.
 * If structureFloorH is provided (world units), the camera stands
 * on whichever is higher: terrain or structure roof.
 * When flying, flyDeltaZ (pre-scaled by speed×dt) adjusts altitude.
 */
export function updateCameraHeight(
	cam: CameraState,
	heightmap: Float32Array, mapW: number, mapH: number,
	flying: boolean,
	structureFloorH = 0,
	flyDeltaZ = 0,
): void {
	const groundH = sampleHeight(heightmap, mapW, mapH, cam.x, cam.y) * HEIGHT_SCALE;
	const floorH = Math.max(groundH, structureFloorH);
	const targetZ = floorH + EYE_HEIGHT;
	cam.z = flying
		? Math.max(cam.z + flyDeltaZ, targetZ)
		: targetZ;
}

/** Rotate camera by mouse delta (sensitivity-scaled). */
export function rotateCamera(cam: CameraState, dx: number, dy: number, sensitivity = 0.002): void {
	cam.yaw += dx * sensitivity;
	cam.pitch = Math.max(-MAX_PITCH, Math.min(MAX_PITCH, cam.pitch - dy * sensitivity));
}

/**
 * Compute movement vector from yaw and input direction.
 * Returns [moveX, moveY] in world space (forward/strafe).
 */
export function moveVector(yaw: number, forward: number, strafe: number): [number, number] {
	const cosY = Math.cos(yaw);
	const sinY = Math.sin(yaw);
	// Forward is along yaw direction; strafe is perpendicular
	const mx = forward * cosY + strafe * sinY;
	const my = forward * sinY + strafe * cosY;
	return [mx, my];
}

/**
 * 3D movement vector — forward follows full look direction (yaw+pitch).
 * Strafe remains horizontal. Returns [moveX, moveY, moveZ].
 */
export function moveVector3d(yaw: number, pitch: number, forward: number, strafe: number): [number, number, number] {
	const cosY = Math.cos(yaw);
	const sinY = Math.sin(yaw);
	const cosP = Math.cos(pitch);
	const sinP = Math.sin(pitch);
	const mx = forward * cosY * cosP + strafe * sinY;
	const my = forward * sinY * cosP + strafe * cosY;
	const mz = forward * sinP;
	return [mx, my, mz];
}
