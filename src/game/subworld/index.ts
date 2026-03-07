/**
 * Subworld module – public API.
 *
 * Architecture:
 *  types.ts      — entity, config, zone action, battle types
 *  engine.ts     — unified engine (exploration + combat) + helpers
 *  renderer.ts   — Canvas2D renderer
 *
 * No factories — callers build SubworldConfig using generators directly.
 */

export {
	SubworldEngine, seededRng, tileWalkable, findWalkable, makeEntity,
} from './engine';
export {SubworldRenderer} from './renderer';
export type {CitizenSpriteSheet} from './citizen-sprites';
export {createCitizenSpriteSheet, renderPlayerSprite} from './citizen-sprites';
export type {
	SubworldConfig,
	SubworldEntity,
	TraversabilityGrid,
	Vec2,
	ZoneAction,
	BattleSubworldOptions,
	BattleResult,
} from './types';
