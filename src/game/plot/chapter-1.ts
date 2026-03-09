// === Plot Module: Chapter 1 (placeholder) ===
//
// Dormant until activated by the intro completion handler.
// Replace the condition with a real plot trigger when content is ready.
// If this file is deleted the game runs without chapter 1 content.

import {type LogicNode, createNode} from '../logic-nodes';

export const chapter1Nodes: LogicNode[] = [
	createNode({
		id: 'plot_chapter_1',
		label: 'Chapter 1 (placeholder)',
		conditions: [{check: () => false}],
		mask: [1],
		next: ['plot_chapter_1'],
		tags: ['plot'],
	}),
];
