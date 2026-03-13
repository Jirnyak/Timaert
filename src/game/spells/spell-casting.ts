// === Spell Scaling & Casting Engine ===
// L1 — pure functions, no event bus, no UI.

import {
	type Attributes, type CombatStats, type Skills, calculateDerived,
} from '../attributes';
import type {Spell, SpellBook} from './spell-types';

// ── Universal spell strength ─────────────────────────────────────
//
//   S = basePower × M_stat × M_tier
//
//   M_stat  = rawSpellDamage (flat bonus from INT + spellcraft skill)
//   M_tier  = 1 + 0.08 × (tier − 1)
//
// Damage = baseDamage + rawSpellDamage × tierMul × scaling

export function spellStrength(spell: Spell, attributes: Attributes, skills: Skills): number {
	const derived = calculateDerived(attributes, skills);
	const tierMul = 1 + 0.08 * (spell.tier - 1);
	return derived.rawSpellDamage * tierMul;
}

/** Final damage for a micro-effect hit. */
export function spellDamage(spell: Spell, attributes: Attributes, skills: Skills): number {
	if (!spell.micro || spell.micro.baseDamage <= 0) {
		return 0;
	}

	const s = spellStrength(spell, attributes, skills);
	return Math.floor((spell.micro.baseDamage + s) * spell.scaling.power);
}

/** Final heal amount. */
export function spellHeal(spell: Spell, attributes: Attributes, skills: Skills): number {
	if (!spell.micro || spell.micro.baseHeal <= 0) {
		return 0;
	}

	const s = spellStrength(spell, attributes, skills);
	return Math.floor((spell.micro.baseHeal + s) * spell.scaling.power);
}

/** Final AoE radius (pixels). */
export function spellRadius(spell: Spell, attributes: Attributes, skills: Skills): number {
	if (!spell.micro) {
		return 0;
	}

	const s = spellStrength(spell, attributes, skills);
	const scaleFactor = s / (s + 50); // Asymptotic radius growth
	return Math.floor(spell.micro.baseRadius * (1 + scaleFactor * spell.scaling.radius));
}

/** Final buff/aura duration (seconds). */
export function spellDuration(spell: Spell, attributes: Attributes, skills: Skills): number {
	if (!spell.micro) {
		return 0;
	}

	const s = spellStrength(spell, attributes, skills);
	const scaleFactor = s / (s + 50); // Asymptotic duration growth
	return spell.micro.duration * (1 + scaleFactor * spell.scaling.duration);
}

// ── Casting validation ───────────────────────────────────────────

export type CastResult =
	| {ok: true}
	| {ok: false; reason: string};

export function canCast(
	spell: Spell,
	combat: CombatStats,
	book: SpellBook,
	inMicro: boolean,
): CastResult {
	if (!book.learned.includes(spell.id)) {
		return {ok: false, reason: 'Spell not learned'};
	}

	// Sustained spells toggle — if already active, always allow (to deactivate)
	if (spell.sustained && book.sustainedActive.includes(spell.id)) {
		return {ok: true};
	}

	if (combat.currentMp < spell.manaCost) {
		return {ok: false, reason: 'Not enough mana'};
	}

	const cd = book.cooldowns[spell.id] ?? 0;
	if (cd > 0) {
		return {ok: false, reason: `Cooldown ${cd.toFixed(1)}s`};
	}

	if (inMicro && !spell.micro) {
		return {ok: false, reason: 'Cannot use here'};
	}

	if (!inMicro && !spell.macro) {
		return {ok: false, reason: 'Cannot use on world map'};
	}

	return {ok: true};
}

/** Deduct mana and start cooldown.  Returns mana spent. */
export function startCast(
	spell: Spell,
	combat: CombatStats,
	book: SpellBook,
): number {
	// Sustained spells toggle on/off
	if (spell.sustained) {
		return toggleSustained(spell, book);
	}

	combat.currentMp -= spell.manaCost;
	if (spell.cooldown > 0) {
		book.cooldowns[spell.id] = spell.cooldown;
	}

	return spell.manaCost;
}

/** Toggle a sustained spell on or off. Returns 0 (drain handled per-tick). */
export function toggleSustained(spell: Spell, book: SpellBook): number {
	const idx = book.sustainedActive.indexOf(spell.id);
	if (idx === -1) {
		book.sustainedActive.push(spell.id);
	} else {
		book.sustainedActive.splice(idx, 1);
	}

	return 0;
}

/** Tick all cooldowns and drain mana for sustained spells. */
export function tickSpellBook(
	book: SpellBook,
	combat: CombatStats,
	dt: number,
	getSpell: (id: string) => Spell | undefined,
): void {
	// Cooldowns
	for (const id of Object.keys(book.cooldowns)) {
		book.cooldowns[id] -= dt;
		if (book.cooldowns[id] <= 0) {
			// eslint-disable-next-line @typescript-eslint/no-dynamic-delete
			delete book.cooldowns[id];
		}
	}

	// Sustained mana drain
	for (let i = book.sustainedActive.length - 1; i >= 0; i--) {
		const spell = getSpell(book.sustainedActive[i]);
		if (!spell) {
			book.sustainedActive.splice(i, 1);
			continue;
		}

		const drain = spell.manaDrain * dt;
		if (combat.currentMp >= drain) {
			combat.currentMp -= drain;
		} else {
			// Out of mana — cancel sustained spell
			combat.currentMp = 0;
			book.sustainedActive.splice(i, 1);
		}
	}
}

// ── Spell book helpers ───────────────────────────────────────────

export function createSpellBook(): SpellBook {
	return {
		learned: [], activeSpellId: '', cooldowns: {}, sustainedActive: [],
	};
}

export function learnSpell(book: SpellBook, spellId: string): boolean {
	if (book.learned.includes(spellId)) {
		return false;
	}

	book.learned.push(spellId);
	if (book.activeSpellId === '') {
		book.activeSpellId = spellId;
	}

	return true;
}

export function setActiveSpell(book: SpellBook, spellId: string): void {
	if (book.learned.includes(spellId)) {
		book.activeSpellId = spellId;
	}
}
