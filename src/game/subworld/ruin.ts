// === Ruin generator — abandoned structure subworld ===
//
// Produces a tile map with crumbling walls, overgrown vegetation,
// and broken roads. Difficulty level controls how many wall rings
// and how dense the rubble/traps are.

import {
	TILE_ROAD, TILE_GRASS, TILE_TREE_DECOR, TILE_SQUARE,
	type Point, type StreetNode, type StreetEdge, type MapData,
} from './map-data';
import {segmentIntersection, BaseMapGenerator} from './base-generator';

export class RuinGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'ruin', 1);
	}

	generateTiles(difficulty: number): void {
		this.layOvergrowth();
		this.carveRuinedPaths();
		this.buildRuinedWalls(difficulty);
		this.placeRuinedSquare();
		this.scatterRubble();
	}

	/** Base the map on overgrown grass. */
	private layOvergrowth(): void {
		for (let i = 0; i < this.grid.length; i++) {
			this.grid[i] = this.rng.random() < 0.55 ? TILE_GRASS : 0;
		}
	}

	/** Carve broken, meandering paths that used to be streets. */
	private carveRuinedPaths(): void {
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		const pathCount = this.rng.randInt(2, 4);
		for (let i = 0; i < pathCount; i++) {
			const angle = (i * (Math.PI * 2) / pathCount)
				+ this.rng.randFloat(-0.6, 0.6);
			const tx = this.centerX + (Math.cos(angle) * this.width * 0.5);
			const ty = this.centerY + (Math.sin(angle) * this.height * 0.5);
			this.markOrganicMainRoad(this.centerX, this.centerY, tx, ty, angle);
		}

		// Add broken side paths
		const branches = this.rng.randInt(4, 10);
		for (let i = 0; i < branches; i++) {
			this.growStreetBranch();
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

	/** Scatter trees and debris among the ruins. */
	private scatterRubble(): void {
		for (let y = 3; y < this.height - 3; y += 3) {
			for (let x = 3; x < this.width - 3; x += 3) {
				const idx = y * this.width + x;
				if (this.grid[idx] === TILE_ROAD || this.grid[idx] === TILE_SQUARE) {
					continue;
				}

				if (this.rng.random() < 0.15) {
					const w = this.rng.randInt(1, 3);
					const h = this.rng.randInt(1, 3);
					this.houses.push({
						x, y, w, h, rotation: this.rng.randFloat(-0.4, 0.4),
					});
					this.grid[idx] = TILE_TREE_DECOR;
				}
			}
		}
	}

	// ── Street branching ────────────────────────────────────────

	private growStreetBranch(): StreetEdge | undefined {
		if (this.streetNodes.length < 2) {
			return undefined;
		}

		const available = this.streetNodes.length > 5
			? Array.from({length: this.streetNodes.length - 5}, (_, i) => i + 5)
			: [0];
		const parentId = this.selectWeightedParent(available);
		const parent = this.streetNodes[parentId];
		const radialAngle = Math.atan2(parent.y - this.centerY, parent.x - this.centerX);

		for (let i = 0; i < 12; i++) {
			let angle: number;
			if (this.rng.random() < 0.55) {
				const perpBase = radialAngle + (this.rng.random() > 0.5 ? Math.PI / 2 : -(Math.PI / 2));
				angle = perpBase + this.rng.randFloat(-0.4, 0.4);
			} else {
				angle = radialAngle + this.rng.randFloat(-0.6, 0.6);
			}

			const dist = this.rng.randFloat(10, 20) * this.streetWidth;
			const nx = parent.x + (Math.cos(angle) * dist);
			const ny = parent.y + (Math.sin(angle) * dist);

			const margin = 15;
			if (nx < margin || nx >= this.width - margin || ny < margin || ny >= this.height - margin) {
				continue;
			}

			let tooClose = false;
			for (const node of this.streetNodes) {
				const snDx = node.x - nx;
				const snDy = node.y - ny;
				if (Math.sqrt(snDx * snDx + snDy * snDy) < 8 * this.streetWidth) {
					tooClose = true;
					break;
				}
			}

			if (tooClose) {
				continue;
			}

			const newNode: StreetNode = {x: nx, y: ny, isMain: false};
			const newId = this.streetNodes.length;
			this.streetNodes.push(newNode);
			const edge: StreetEdge = {p1: parentId, p2: newId};
			this.streetEdges.push(edge);
			this.markStreetAndRemoveHouses(parent.x, parent.y, nx, ny);
			return edge;
		}

		return undefined;
	}

	private selectWeightedParent(available: number[]): number {
		if (available.length <= 1) {
			return available[0];
		}

		const maxDist = Math.min(this.width, this.height) * 0.4;
		let totalWeight = 0;
		const weights: number[] = [];
		for (const id of available) {
			const node = this.streetNodes[id];
			const wdx = node.x - this.centerX;
			const wdy = node.y - this.centerY;
			const d = Math.sqrt(wdx * wdx + wdy * wdy);
			const w = Math.max(0.1, 1 - ((d / maxDist) * 0.7));
			weights.push(w);
			totalWeight += w;
		}

		let r = this.rng.random() * totalWeight;
		for (const [i, weight] of weights.entries()) {
			r -= weight;
			if (r <= 0) {
				return available[i];
			}
		}

		return available.at(-1)!;
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
export function generateRuin(seed: number, width: number, height: number, difficulty = 1): MapData {
	const gen = new RuinGenerator(seed, width, height);
	gen.generateTiles(difficulty);
	return gen.toMapData();
}
