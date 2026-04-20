// === Road generator — road-feature subworld ===
//
// Generates a subworld for macroworld cells that have a Road feature.
// The road connects edges where neighbouring cells also have roads,
// with organic curves, ditches, and scattered vegetation.
// When the cell has Water biome + Road feature, bridge structures
// are generated over the water portion of the road.

import {
	type NeighborGrid, type Dir,
	DIR_OFFSETS, roadDirections, Biome,
} from './map-data';
import {BaseMapGenerator, WATER_LEVEL} from './base-generator';
import {HEIGHT_SCALE} from './camera';

/** Thickness of the bridge deck in world-space units. */
const BRIDGE_DECK_H = 3;

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
		if (neighbors) {
			this.setNeighbors(neighbors);
		}

		this.layGrassBase();

		const dirs = neighbors
			? roadDirections(neighbors)
			: [0 as Dir, 4 as Dir]; // Fallback: N–S road

		this.carveRoads(dirs);
		this.scatterUniversalTrees();

		// Water + road → generate bridge structures over the river
		if (this.biome === Biome.Water && this.neighborGrid) {
			this.buildStructuresFromTiles();
			this.generateBridges(dirs);
		}
	}

	/**
	 * Carve roads connecting edges through the cell.
	 * When ≤2 directions: a simple through-road (no center crossroads).
	 * When 3+ directions: hub through center.
	 */
	private carveRoads(dirs: Dir[]): void {
		if (dirs.length <= 2) {
			this.carveSimpleRoad(dirs);
		} else {
			this.carveCrossroads(dirs);
		}
	}

	/** For ≤2 directions: draw a road straight between the two endpoints. */
	private carveSimpleRoad(dirs: Dir[]): void {
		const endpoints = dirs.map(d => this.edgeTarget(d));
		if (endpoints.length === 0) {
			return;
		}

		if (endpoints.length === 1) {
			// Single direction: road from that edge to the center
			const ep = endpoints[0];
			this.streetNodes.push(
				{x: ep.x, y: ep.y, isMain: true},
				{x: this.centerX, y: this.centerY, isMain: true},
			);
			this.streetEdges.push({p1: 0, p2: 1});
			this.markOrganicMainRoad(ep.x, ep.y, this.centerX, this.centerY, ep.angle);
			return;
		}

		// Two directions: direct road between the two edges (through-road)
		const [a, b] = endpoints;
		this.streetNodes.push(
			{x: a.x, y: a.y, isMain: true},
			{x: b.x, y: b.y, isMain: true},
		);
		this.streetEdges.push({p1: 0, p2: 1});
		const angle = Math.atan2(b.y - a.y, b.x - a.x);
		this.markOrganicMainRoad(a.x, a.y, b.x, b.y, angle);
	}

	/** For 3+ directions: hub in center, roads radiate outward. */
	private carveCrossroads(dirs: Dir[]): void {
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		for (const d of dirs) {
			const ep = this.edgeTarget(d);
			const nodeId = this.streetNodes.length;
			this.streetNodes.push({x: ep.x, y: ep.y, isMain: true});
			this.streetEdges.push({p1: 0, p2: nodeId});
			this.markOrganicMainRoad(this.centerX, this.centerY, ep.x, ep.y, ep.angle);
		}
	}

	/** Get edge target for a direction, using anchor if available. */
	private edgeTarget(d: Dir): {x: number; y: number; angle: number} {
		const anchor = this.anchorFor(d);
		const [dx, dy] = DIR_OFFSETS[d];
		const angle = Math.atan2(dy, dx);
		return {
			x: anchor
				? anchor.x
				: Math.max(1, Math.min(this.width - 2, Math.round(this.centerX + dx * (this.width * 0.49)))),
			y: anchor
				? anchor.y
				: Math.max(1, Math.min(this.height - 2, Math.round(this.centerY + dy * (this.height * 0.49)))),
			angle,
		};
	}

	// ── Bridge generation ────────────────────────────────────────

	/**
	 * Create bridge structures for a water cell with road feature.
	 * Bridge deck sits just above the water plane. Supports extend
	 * down to the riverbed (handled by the structure shader).
	 */
	private generateBridges(dirs: Dir[]): void {
		if (dirs.length <= 2) {
			const endpoints = dirs.map(d => this.edgeTarget(d));
			if (endpoints.length === 2) {
				this.addBridgeSegment(endpoints[0], endpoints[1]);
			} else if (endpoints.length === 1) {
				this.addBridgeSegment(
					endpoints[0],
					{x: this.centerX, y: this.centerY},
				);
			}
		} else {
			// Crossroads: one bridge segment from center to each edge
			for (const d of dirs) {
				const ep = this.edgeTarget(d);
				this.addBridgeSegment(
					{x: this.centerX, y: this.centerY},
					ep,
				);
			}
		}
	}

	/** Place a single bridge rect along a road segment. */
	private addBridgeSegment(
		a: {x: number; y: number},
		b: {x: number; y: number},
	): void {
		const dx = b.x - a.x;
		const dy = b.y - a.y;
		const length = Math.sqrt(dx * dx + dy * dy);
		const angle = Math.atan2(dy, dx);

		this.structures.push(this.makeStructure({
			tag: 'bridge',
			shape: 'rect',
			x: (a.x + b.x) / 2,
			y: (a.y + b.y) / 2,
			w: length,
			l: 8,
			height: BRIDGE_DECK_H, // Patched in toMapData from actual terrain
			rotation: angle,
			roofTexture: 'bridge_plank',
			wallTexture: 'bridge_side',
			solid: true,
			sprite: false,
		}));
	}

	/**
	 * Patch bridge heights so decks protrude above water.
	 * height = (waterLevel − terrain) × heightScale + deckThickness
	 */
	override toMapData() {
		const data = super.toMapData();
		if (data.heightmap) {
			for (const s of data.structures) {
				if (s.tag !== 'bridge') {
					continue;
				}

				const gx = Math.floor(s.x);
				const gy = Math.floor(s.y);
				const terrain = data.heightmap[gy * data.width + gx] ?? 0;
				s.height = Math.max(WATER_LEVEL - terrain, 0) * HEIGHT_SCALE + BRIDGE_DECK_H;
			}
		}

		return data;
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
