// === Road generator — road-feature subworld ===
//
// Generates a subworld for macroworld cells that have a Road feature.
// The road connects edges where neighbouring cells also have roads,
// with organic curves, ditches, and scattered vegetation.

import {
	TILE_ROAD, TILE_GRASS, TILE_TREE_DECOR,
	type StreetNode, type StreetEdge,
	type NeighborGrid, type Dir,
	DIR_OFFSETS, roadDirections, landmarkDirections,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class RoadGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'grassland', 1);
	}

	/**
	 * Generate tiles for a road cell.
	 * @param _density — unused, kept for GeneratorFn signature.
	 * @param neighbors — optional NeighborGrid for road connectivity.
	 */
	generateTiles(_density: number, neighbors?: NeighborGrid): void {
		this.layGrassBase();

		const dirs = neighbors
			? [...new Set([...roadDirections(neighbors), ...landmarkDirections(neighbors)])]
			: [0 as Dir, 4 as Dir]; // Fallback: N–S road

		this.carveRoads(dirs);
		this.scatterVegetation();
	}

	private layGrassBase(): void {
		for (let i = 0; i < this.grid.length; i++) {
			this.grid[i] = TILE_GRASS;
		}
	}

	/**
	 * Carve roads from center toward each connected edge.
	 * All roads meet at the center, forming a natural crossroads.
	 */
	private carveRoads(dirs: Dir[]): void {
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});

		for (const d of dirs) {
			const [dx, dy] = DIR_OFFSETS[d];
			// Edge target: walk to the boundary in that direction
			const tx = this.centerX + dx * (this.width * 0.49);
			const ty = this.centerY + dy * (this.height * 0.49);
			const clampedX = Math.max(1, Math.min(this.width - 2, Math.round(tx)));
			const clampedY = Math.max(1, Math.min(this.height - 2, Math.round(ty)));
			const angle = Math.atan2(dy, dx);

			const nodeId = this.streetNodes.length;
			this.streetNodes.push({x: clampedX, y: clampedY, isMain: true});
			this.streetEdges.push({p1: 0, p2: nodeId});
			this.markOrganicMainRoad(this.centerX, this.centerY, clampedX, clampedY, angle);
		}

		// A few side trails branching off the main roads
		const trailCount = this.rng.randInt(2, 6);
		for (let i = 0; i < trailCount; i++) {
			this.growStreetBranch();
		}
	}

	/** Scatter trees and grass alongside the road. */
	private scatterVegetation(): void {
		const treeChance = 0.06;
		for (let y = 3; y < this.height - 3; y += 3) {
			for (let x = 3; x < this.width - 3; x += 3) {
				const idx = y * this.width + x;
				if (this.grid[idx] !== TILE_GRASS) {
					continue;
				}

				// Skip tiles too close to roads
				if (this.hasNearbyTile(x, y, TILE_ROAD, 2)) {
					continue;
				}

				if (this.rng.random() < treeChance) {
					const w = this.rng.randInt(2, 3);
					const h = this.rng.randInt(2, 3);
					this.houses.push({
						x, y, w, h, rotation: this.rng.randFloat(-0.2, 0.2),
					});
					for (let dy = 0; dy < h; dy++) {
						for (let dx = 0; dx < w; dx++) {
							const px = x + dx;
							const py = y + dy;
							if (px < this.width && py < this.height) {
								this.grid[py * this.width + px] = TILE_TREE_DECOR;
							}
						}
					}
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
}

export function generateRoad(
	seed: number, w: number, h: number,
	_parameter?: number, neighbors?: NeighborGrid,
): ReturnType<BaseMapGenerator['toMapData']> {
	const gen = new RoadGenerator(seed, w, h);
	gen.generateTiles(0, neighbors);
	return gen.toMapData();
}
