// === Plot Registry ===
//
// Single import point for all plot content modules.
// Each module exports a LogicNode[] array, optionally with active-on-start IDs.
//
// To add a new quest / chapter:
//   1. Create  plot/my-quest.ts  exporting  myQuestNodes: LogicNode[]
//   2. Import it here and spread into PLOT_NODES / PLOT_ACTIVE_NODES.
//   3. If the file is removed, the game still runs — nodes just won't exist.

import type {LogicNode} from '../logic-nodes';
import {introNodes, introActiveNodes} from './intro';
import {chapter1Nodes} from './chapter-1';

/** All plot logic nodes — registered once at game start. */
export const PLOT_NODES: LogicNode[] = [
	...introNodes,
	...chapter1Nodes,
];

/** Node IDs activated at game start (subset of PLOT_NODES). */
export const PLOT_ACTIVE_NODES: string[] = [
	...introActiveNodes,
];

/** Result of a story-sequence overlay — choices keyed by phase id. */
export type StoryResult = Record<string, string>;
