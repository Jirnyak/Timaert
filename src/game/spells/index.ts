// === Spell Registry — single import point ===
// Add new spells here.  Nothing else needs to change.

import type {Spell} from './spell-types';
import {magicBolt} from './magic-bolt';
import {fireball} from './fireball';
import {iceShard} from './ice-shard';
import {energyBeam} from './energy-beam';
import {lightningChain} from './lightning-chain';
import {haste} from './haste';
import {flight} from './flight';
import {armageddon} from './armageddon';

/** All spells in registration order. */
const allSpells: Spell[] = [
	magicBolt,
	fireball,
	iceShard,
	energyBeam,
	lightningChain,
	haste,
	flight,
	armageddon,
];

/** All spell definitions, keyed by id. */
const catalog = new Map<string, Spell>();
for (const spell of allSpells) {
	catalog.set(spell.id, spell);
}

export const SPELL_CATALOG: ReadonlyMap<string, Spell> = catalog;

/** Ordered list for UI display (sorted by tier then name). */
export const SPELL_LIST: Spell[] = [...catalog.values()]
	.sort((a, b) => a.tier - b.tier || a.name.localeCompare(b.name));

/** Look up a spell by id. Returns undefined for unknown ids. */
export function getSpell(id: string): Spell | undefined {
	return catalog.get(id);
}

// Re-export types and engine for convenience
export type {
	Spell, SpellBook, SpellTag, MicroEffect, MacroEffect,
} from './spell-types';
export {
	createSpellBook, learnSpell, setActiveSpell,
	canCast, startCast, toggleSustained, tickSpellBook,
	spellDamage, spellHeal, spellRadius, spellDuration, spellStrength,
} from './spell-casting';
