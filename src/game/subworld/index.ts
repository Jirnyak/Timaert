/**
 * Subworld module – public API.
 *
 * Architecture:
 *  map-data.ts           — tile constants, shared types, tiny helpers
 *  base-generator.ts     — abstract base class (grid ops, walls, branching)
 *  city-generator.ts     — city settlement generation
 *  village.ts            — village settlement generation
 *  forest.ts             — dense woodland wilderness
 *  grassland.ts          — open plains wilderness
 *  ruin.ts               — ruined landmark generation
 *  road-generator.ts     — road-feature terrain generation
 *  mountain.ts           — mountain-feature terrain generation
 *  water.ts              — water biome generation
 *  swamp.ts              — swamp biome generation
 *  map-renderer.ts       — Canvas2D tile-map renderer (MapData → Canvas)
 *  map-factory.ts        — generator registry + factory (seed → SubworldMapData)
 *  seamless-manager.ts   — 9-cell seamless grid manager
 *  types.ts              — entity, config, zone action, battle types
 *  engine.ts             — unified engine (exploration + combat) + helpers
 *  renderer.ts           — Canvas2D entity renderer
 *  renderer-3d.ts        — WebGL2 first-person 3D renderer (Might & Magic style)
 *
 * To add a new subworld type:
 *  1. Add the type to SubworldMode union in map-data.ts
 *  2. Create <type>.ts with a generate function
 *  3. Register it in map-factory.ts
 */

export {
	SubworldEngine, tileWalkable, findWalkable, makeEntity,
} from './engine';
export {tickWander, tickCombatMove, tickFlee} from './ai';
export {SubworldRenderer} from './renderer';
export {SubworldRenderer3D} from './renderer-3d';
export type {BillboardEntity} from './renderer-3d';
export {
	createMicroNpc, spawnArmy, spawnCityNpcs, spawnWildernessNpcs,
	spawnMacroNpcs, spawnFauna, populateCell,
} from './spawn';
export type {PopulateCellContext} from './spawn';
export {getFaunaTable, rollFauna} from './fauna';
export type {FaunaEntry, FaunaTable} from './fauna';
export type {CitizenSpriteSheet} from './citizen-sprites';
export {createCitizenSpriteSheet, renderPlayerSprite} from './citizen-sprites';
export type {
	SubworldConfig,
	SubworldEntity,
	TraversabilityGrid,
	Vec2,
	ZoneAction,
	SubworldResult,
	FightContext,
} from './types';

// Map generation — registry-based
export {generateSubworldMap, getSubworldTraversabilityData, registerGenerator} from './map-factory';
export type {GeneratorFn} from './map-factory';
export {renderMap} from './map-renderer';
export {CityMapGenerator} from './city-generator';
export {ForestGenerator} from './forest';
export {GrasslandGenerator} from './grassland';
export {VillageGenerator} from './village';
export {RuinGenerator} from './ruin';
export {RoadGenerator} from './road-generator';
export {MountainGenerator} from './mountain';
export {WaterGenerator} from './water';
export {SwampGenerator} from './swamp';
export type {
	MapData, SubworldMapData, SubworldMode, CellTerrain, CellLandmark,
	NeighborGrid, CellContext, CellFeature,
} from './map-data';
export {
	TILE_EMPTY, TILE_ROAD, TILE_HOUSE, TILE_WALL,
	TILE_FIELD, TILE_GRASS, TILE_SQUARE, TILE_TREE_DECOR,
	Dir, DIR_OFFSETS, buildNeighborGrid,
	findTileNear, collectRoadNearHouses,
} from './map-data';

// Seamless 9-cell manager
export {
	SeamlessSubworldManager, CELL_SIZE,
} from './seamless-manager';
export type {
	LoadedCell, CellResolver, ModeResolver, ShiftResult,
} from './seamless-manager';

// 3D camera + save/load
export {
	createCamera, sampleHeight, updateCameraHeight, rotateCamera, moveVector, moveVector3d, HEIGHT_SCALE,
} from './camera';
export type {CameraState} from './camera';
export {saveSubworldData, createSubworldSnapshot} from './map-factory';
