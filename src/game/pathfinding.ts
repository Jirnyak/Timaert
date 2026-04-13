/** Pre-computed cost grid for A* pathfinding. */
export type PathCostData = {
	width: number;
	height: number;
	/** SP weight per cell (1.0–10.0). */
	costGrid: Float32Array;
};

type Node = {
	x: number;
	y: number;
	g: number;
	h: number;
	f: number;
	parent: Node | undefined;
};

// Binary min-heap for efficient priority queue
class MinHeap {
	private readonly items: Node[] = [];
	private readonly index = new Map<number, number>();

	constructor(private readonly width: number) {}

	private key(node: Node): number {
		return node.y * this.width + node.x;
	}

	get size(): number {
		return this.items.length;
	}

	push(node: Node): void {
		const k = this.key(node);
		const existing = this.index.get(k);
		if (existing !== undefined) {
			if (node.f < this.items[existing].f) {
				this.items[existing] = node;
				this.bubbleUp(existing);
			}

			return;
		}

		this.items.push(node);
		this.index.set(k, this.items.length - 1);
		this.bubbleUp(this.items.length - 1);
	}

	pop(): Node | undefined {
		if (this.items.length === 0) {
			return undefined;
		}

		const top = this.items[0];
		this.index.delete(this.key(top));
		const last = this.items.pop()!;
		if (this.items.length > 0) {
			this.items[0] = last;
			this.index.set(this.key(last), 0);
			this.sinkDown(0);
		}

		return top;
	}

	private bubbleUp(index: number): void {
		while (index > 0) {
			const parent = (index - 1) >> 1;
			if (this.items[index].f >= this.items[parent].f) {
				break;
			}

			this.swap(index, parent);
			index = parent;
		}
	}

	private sinkDown(index: number): void {
		const {length} = this.items;
		while (true) {
			let smallest = index;
			const left = 2 * index + 1;
			const right = 2 * index + 2;
			if (left < length && this.items[left].f < this.items[smallest].f) {
				smallest = left;
			}

			if (right < length && this.items[right].f < this.items[smallest].f) {
				smallest = right;
			}

			if (smallest === index) {
				break;
			}

			this.swap(index, smallest);
			index = smallest;
		}
	}

	private swap(a: number, b: number): void {
		const temporary = this.items[a];
		this.items[a] = this.items[b];
		this.items[b] = temporary;
		this.index.set(this.key(this.items[a]), a);
		this.index.set(this.key(this.items[b]), b);
	}
}

// 8-directional neighbors (cardinal + diagonal)
const DX = [0, 1, 1, 1, 0, -1, -1, -1];
const DY = [-1, -1, 0, 1, 1, 1, 0, -1];
// Cost multiplier: 1.0 for cardinal, √2 for diagonal
const STEP_COST = [1, 1.414_213_6, 1, 1.414_213_6, 1, 1.414_213_6, 1, 1.414_213_6];

// Wrap coordinate for torus topology
function wrap(value: number, max: number): number {
	return ((value % max) + max) % max;
}

// Octile distance with torus wrapping (consistent heuristic for 8-dir)
function heuristic(x1: number, y1: number, x2: number, y2: number, w: number, h: number): number {
	let dx = Math.abs(x2 - x1);
	let dy = Math.abs(y2 - y1);
	if (dx > w / 2) {
		dx = w - dx;
	}

	if (dy > h / 2) {
		dy = h - dy;
	}

	// Octile: max(dx,dy) + (√2−1)*min(dx,dy)
	return dx > dy
		? dx + 0.414_213_6 * dy
		: dy + 0.414_213_6 * dx;
}

export type PathResult = {
	path: Array<{x: number; y: number}>;
	found: boolean;
};

// A* pathfinding with SP cost weights on torus topology.
// All cells are passable — cost determines preference.
// maxSteps limits search to prevent freezing on large maps.
export function findPath(
	data: PathCostData,
	startX: number,
	startY: number,
	endX: number,
	endY: number,
	maxSteps = 50_000,
): PathResult {
	const {width, height, costGrid} = data;

	const sx = wrap(startX, width);
	const sy = wrap(startY, height);
	const ex = wrap(endX, width);
	const ey = wrap(endY, height);

	if (sx === ex && sy === ey) {
		return {path: [{x: sx, y: sy}], found: true};
	}

	const open = new MinHeap(width);
	const closed = new Uint8Array(width * height);

	const gScores = new Float32Array(width * height);
	gScores.fill(Infinity);

	const startNode: Node = {
		x: sx,
		y: sy,
		g: 0,
		h: heuristic(sx, sy, ex, ey, width, height),
		f: 0,
		parent: undefined,
	};
	startNode.f = startNode.h;
	gScores[sy * width + sx] = 0;

	open.push(startNode);

	let steps = 0;
	while (open.size > 0 && steps < maxSteps) {
		steps++;
		const current = open.pop()!;
		const idx = current.y * width + current.x;

		if (current.x === ex && current.y === ey) {
			// Reconstruct path
			const path: Array<{x: number; y: number}> = [];
			let node: Node | undefined = current;
			while (node) {
				path.push({x: node.x, y: node.y});
				node = node.parent;
			}

			path.reverse();
			return {path, found: true};
		}

		if (closed[idx]) {
			continue;
		}

		closed[idx] = 1;

		for (let d = 0; d < 8; d++) {
			const nx = wrap(current.x + DX[d], width);
			const ny = wrap(current.y + DY[d], height);
			const nidx = ny * width + nx;

			if (closed[nidx]) {
				continue;
			}

			// Edge weight = SP cost weight of destination × step distance
			const cost = costGrid[nidx] * STEP_COST[d];
			const tentativeG = current.g + cost;

			if (tentativeG < gScores[nidx]) {
				gScores[nidx] = tentativeG;
				const h = heuristic(nx, ny, ex, ey, width, height);
				open.push({
					x: nx,
					y: ny,
					g: tentativeG,
					h,
					f: tentativeG + h,
					parent: current,
				});
			}
		}
	}

	return {path: [], found: false};
}
