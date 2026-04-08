// === Ruin generator — abandoned structure subworld ===
//
// Produces a tile map with crumbling walls, overgrown vegetation,
// and broken roads. Difficulty level controls how many wall rings
// and how dense the rubble/traps are.

import {
	TILE_SQUARE,
	type Point, type MapData,
	type NeighborGrid,
} from './map-data';
import {segmentIntersection, BaseMapGenerator} from './base-generator';

export class RuinGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'ruin', 1);
	}

	generateTiles(difficulty: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.setNeighbors(neighbors);
		}

		this.layGrassBase();
		this.carveRuinedPaths();
		this.buildRuinedWalls(difficulty);
		this.placeRuinedSquare();
		this.scatterUniversalTrees();
	}

	/** Carve broken, meandering paths that used to be streets. */
	private carveRuinedPaths(): void {
		if (this.edgeAnchors.length === 0) {
			return;
		}

		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		for (const anchor of this.edgeAnchors) {
			const angle = Math.atan2(anchor.y - this.centerY, anchor.x - this.centerX);
			this.markOrganicMainRoad(this.centerX, this.centerY, anchor.x, anchor.y, angle);
		}
	}

	/** Build walls with intentional gaps (ruined). */
	private buildRuinedWalls(difficulty: number): void {
		const rings = Math.max(1, Math.min(3, Math.ceil(difficulty / 3)));
		for (let ring = 0; ring < rings; ring++) {
			const radius = this.width * (0.12 + ring * 0.1);
			const segments = this.rng.randInt(10, 16);
			this.buildWall(radius, segments, 0.25);
		}
	}

	/** Place a cracked stone square at center. */
	private placeRuinedSquare(): void {
		const size = this.rng.randInt(5, 9);
		const x = this.centerX - Math.floor(size / 2);
		const y = this.centerY - Math.floor(size / 2);
		for (let dy = 0; dy < size; dy++) {
			for (let dx = 0; dx < size; dx++) {
				const px = x + dx;
				const py = y + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height // Leave random holes in the square
					&& this.rng.random() < 0.8) {
					this.grid[py * this.width + px] = TILE_SQUARE;
				}
			}
		}
	}

	// ── Wall building ───────────────────────────────────────────

	private getCenterOfMass(): Point {
		if (this.streetNodes.length === 0) {
			return {x: this.width / 2, y: this.height / 2};
		}

		let sumX = 0;
		let sumY = 0;
		for (const n of this.streetNodes) {
			sumX += n.x;
			sumY += n.y;
		}

		return {x: sumX / this.streetNodes.length, y: sumY / this.streetNodes.length};
	}

	private buildWall(radius: number, segments: number, roughness = 0.15): void {
		const center = this.getCenterOfMass();
		const angleStep = (Math.PI * 2) / segments;
		const nodes: Point[] = [];
		const phase1 = this.rng.randFloat(0, Math.PI * 2);
		const phase2 = this.rng.randFloat(0, Math.PI * 2);

		for (let i = 0; i < segments; i++) {
			const angle = i * angleStep;
			const harmonic = (Math.sin((angle * 3) + phase1) * 0.32)
				+ (Math.sin((angle * 5) + phase2) * 0.18);
			const radialJitter = this.rng.randFloat(-1, 1) * radius * roughness * 0.18;
			const r = radius + (radius * roughness * harmonic) + radialJitter;
			nodes.push({
				x: center.x + (Math.cos(angle) * r),
				y: center.y + (Math.sin(angle) * r),
			});
		}

		for (let pass = 0; pass < 2; pass++) {
			for (let i = 0; i < nodes.length; i++) {
				const previous = nodes[(i - 1 + nodes.length) % nodes.length];
				const curr = nodes[i];
				const next = nodes[(i + 1) % nodes.length];
				curr.x = (previous.x + (curr.x * 2) + next.x) / 4;
				curr.y = (previous.y + (curr.y * 2) + next.y) / 4;
			}
		}

		let totalRadius = 0;
		for (const p of nodes) {
			const rx = p.x - center.x;
			const ry = p.y - center.y;
			totalRadius += Math.sqrt(rx * rx + ry * ry);
		}

		const avgRadius = totalRadius / nodes.length;
		const gateAngles = this.findRoadCrossingsOnWall(nodes, center.x, center.y);
		const gateHalfArc = Math.max(0.05, (5 + (3 * this.streetWidth)) / avgRadius);

		this.walls.push({
			nodes, avgRadius, centerX: center.x, centerY: center.y, gateAngles, gateHalfArc,
		});
	}

	private findRoadCrossingsOnWall(wallNodes: Point[], cx: number, cy: number): number[] {
		const gateAngles: number[] = [];
		for (const path of this.mainRoadPaths) {
			for (let p = 0; p < path.length - 1; p++) {
				for (let w = 0; w < wallNodes.length; w++) {
					const b1 = wallNodes[w];
					const b2 = wallNodes[(w + 1) % wallNodes.length];
					const crossing = segmentIntersection(path[p], path[p + 1], b1, b2);
					if (crossing) {
						gateAngles.push(Math.atan2(crossing.y - cy, crossing.x - cx));
					}
				}
			}
		}

		return gateAngles.length > 0
			? gateAngles
			: [0, Math.PI, Math.PI / 2, -(Math.PI / 2)];
	}
}

/** Functional entry point — used by the generator registry. */
export function generateRuin(
	seed: number, width: number, height: number,
	difficulty = 1, neighbors?: NeighborGrid,
): MapData {
	const gen = new RuinGenerator(seed, width, height);
	gen.generateTiles(difficulty, neighbors);
	return gen.toMapData();
}
