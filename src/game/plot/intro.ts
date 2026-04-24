// === Plot Module: Intro ===
//
// Lore slideshow (intro0–intro8) → sex choice → realm choice.
// Pure content — if this file is deleted the game runs without an intro.

import {EventTag, type StoryPhase} from '../event-types';
import {type LogicNode, createNode} from '../logic-nodes';

// ── Slide narration table ──

const SLIDES: StoryPhase = {
	type: 'slides',
	slides: [
		{image: '/assets/backgrounds/intro0.png', narration: 'In the time before memory, the gods shaped a world upon the surface of a torus — infinite yet bounded.'},
		{image: '/assets/backgrounds/intro1.png', narration: 'Pure Magic flowed through every stone and river, the breath of creation itself.'},
		{image: '/assets/backgrounds/intro2.png', narration: 'But the gods grew jealous of their own work… and destroyed one another.'},
		{image: '/assets/backgrounds/intro3.png', narration: 'Their corpses became the Black Force — void and negation, whispering from beyond.'},
		{image: '/assets/backgrounds/intro4.png', narration: 'Where Pure Magic and Black Force meet, both are annihilated. The world trembles.'},
		{image: '/assets/backgrounds/intro5.png', narration: 'Kingdoms rose. Mage-lords built towers of arrogance. Empires banned magic under pain of death.'},
		{image: '/assets/backgrounds/intro6.png', narration: 'Barbarian kings seized castles from slain wizards, promising freedom they could not deliver.'},
		{image: '/assets/backgrounds/intro7.png', narration: 'A prophecy speaks of a Black Child — herald of the end of Pure Magic.'},
		{image: '/assets/backgrounds/intro8.png', narration: 'And now, traveller, you arrive. The world does not yet know your name.'},
	],
};

// ── Choice phases ──

const SEX_CHOICE: StoryPhase = {
	type: 'choice',
	id: 'sex',
	title: 'Choose Your Nature',
	description: 'The body shapes the mind, and the mind shapes destiny.',
	options: [
		{
			label: 'Male', description: 'Strong mind — +1 skill point', value: 'male', image: '/assets/sprites/male.png',
		},
		{
			label: 'Female', description: 'Strong body — +1 attribute point', value: 'female', image: '/assets/sprites/female.png',
		},
	],
};

const NAME_INPUT: StoryPhase = {
	type: 'input',
	id: 'name',
	title: 'What Is Your Name?',
	description: 'A name is the first claim a soul makes upon the world.',
	placeholder: 'Enter your name',
	defaultValue: 'Traveller',
	maxLength: 24,
};

const REALM_CHOICE: StoryPhase = {
	type: 'choice',
	id: 'realm',
	title: 'Choose Your Homeland',
	description: 'Where you were born determines who you must become — or defy.',
	options: [
		{label: 'Magocracy of Magika', description: 'Land of mage-lords. Magic is everyday life.', value: 'magika'},
		{label: 'Empire of Light', description: 'Holy empire. Magic is forbidden on pain of death.', value: 'empire'},
		{label: 'Barbarian Kingdoms', description: 'Feudal warlords ruling by sword and steel.', value: 'barbarians'},
	],
};

// ── Nodes ──

export const introNodes: LogicNode[] = [
	createNode({
		id: 'intro_main',
		label: 'Intro Sequence',
		conditions: [],
		mask: [],
		next: [],
		tags: ['intro', 'plot'],
		effect(ctx) {
			ctx.bus.emit({
				tag: EventTag.ShowStory,
				phases: [SLIDES, SEX_CHOICE, NAME_INPUT, REALM_CHOICE],
				sourceNodeId: 'intro_main',
			});
		},
	}),
];

export const introActiveNodes = ['intro_main'];
