// === Torus geometry helpers ===
// Shared wraparound utilities for the macroworld (toroidal map).

export function wrapCoord(v: number, size: number): number {
	return ((v % size) + size) % size;
}

export function torusDist(
	ax: number, ay: number, bx: number, by: number,
	w: number, h: number,
): number {
	let dx = Math.abs(ax - bx);
	let dy = Math.abs(ay - by);
	if (dx > w / 2) {
		dx = w - dx;
	}

	if (dy > h / 2) {
		dy = h - dy;
	}

	return Math.hypot(dx, dy);
}

export function torusStepToward(
	fromX: number, fromY: number,
	toX: number, toY: number,
	w: number, h: number,
): {nx: number; ny: number} {
	let dx = toX - fromX;
	let dy = toY - fromY;
	if (dx > w / 2) {
		dx -= w;
	} else if (dx < -w / 2) {
		dx += w;
	}

	if (dy > h / 2) {
		dy -= h;
	} else if (dy < -h / 2) {
		dy += h;
	}

	let nx = fromX;
	let ny = fromY;
	if (dx !== 0) {
		nx += dx > 0 ? 1 : -1;
	}

	if (dy !== 0) {
		ny += dy > 0 ? 1 : -1;
	}

	return {nx: wrapCoord(nx, w), ny: wrapCoord(ny, h)};
}
