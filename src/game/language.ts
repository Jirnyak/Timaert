// === Universal procedural language generator ===
//
// A `Language` is a self-contained, deterministic phonotactics: an alphabet
// (vowels + consonants), per-letter frequencies, and a set of weighted
// syllable templates expressed as strings of `C` (consonant) and `V` (vowel).
//
// Same Language + same RNG → same word, so naming is reproducible from a
// world seed. Each Language is generated with its own RNG (`createLanguage`)
// so the same vowel/consonant inventory yields wildly different "languages"
// — different favourite letters, different syllable shapes, different
// rhythm.
//
// Usage:
//   const lang = createLanguage(rng, DEFAULT_VOWELS, DEFAULT_CONSONANTS);
//   const word = generateWord(lang, rng);            // 'krathok'
//   const name = generateName(lang, rng);            // 'Krathok'
//
// The module is alphabet-agnostic: pass any latin letters (or digraphs) as
// vowels/consonants and the same machinery applies.

export type Language = {
	vowels: string[];
	consonants: string[];
	/** Cumulative distribution over `vowels` (last entry == 1). */
	vowelCdf: Float32Array;
	/** Cumulative distribution over `consonants` (last entry == 1). */
	consonantCdf: Float32Array;
	/** Syllable templates, e.g. ['CV', 'CVC', 'V']. */
	syllables: string[];
	/** Cumulative distribution over `syllables`. */
	syllableCdf: Float32Array;
	minSyllables: number;
	maxSyllables: number;
	/** Probability of placing a doubled-letter inside a word (0..1). */
	doublingChance: number;
};

export const DEFAULT_VOWELS = ['a', 'e', 'i', 'o', 'u', 'y'];
export const DEFAULT_CONSONANTS = [
	'b',
	'c',
	'd',
	'f',
	'g',
	'h',
	'k',
	'l',
	'm',
	'n',
	'p',
	'r',
	's',
	't',
	'v',
	'w',
	'z',
];

/**
 * Pool of syllable templates. Each language picks a small subset of these
 * with random weights so different languages feel structurally distinct
 * (open-vowel vs. cluster-heavy vs. closed-syllable etc.).
 */
const SYLLABLE_POOL = [
	'V',
	'CV',
	'VC',
	'CVC',
	'CV',
	'CV',
	'CVC',
	'CVV',
	'CCV',
	'CVCC',
	'CCVC',
	'VCC',
];

function buildCdf(weights: number[]): Float32Array {
	const cdf = new Float32Array(weights.length);
	let total = 0;
	for (const w of weights) {
		total += w;
	}

	let acc = 0;
	for (const [i, weight] of weights.entries()) {
		acc += weight / total;
		cdf[i] = acc;
	}

	cdf[cdf.length - 1] = 1;
	return cdf;
}

function pickFromCdf<T>(items: T[], cdf: Float32Array, rng: () => number): T {
	const r = rng();
	for (const [i, element] of cdf.entries()) {
		if (r <= element) {
			return items[i];
		}
	}

	return items.at(-1);
}

/**
 * Generate "Zipf-ish" frequency weights for `n` items: a few favourites,
 * a long tail. Bias is a per-language exponent that controls steepness.
 */
function naturalWeights(n: number, rng: () => number, bias: number): number[] {
	const weights: number[] = [];
	for (let i = 0; i < n; i++) {
		// Rng()^bias → squeezed toward 0; (1 - that)^bias → squeezed toward 1.
		// Mix gives most letters a small weight and a few a much larger one.
		const r = rng();
		weights.push(r ** bias + 0.05);
	}

	// Shuffle so favourites aren't always the same alphabet positions.
	for (let i = n - 1; i > 0; i--) {
		const j = Math.floor(rng() * (i + 1));
		[weights[i], weights[j]] = [weights[j], weights[i]];
	}

	return weights;
}

/**
 * Build a fresh `Language` instance using `rng` for all its random choices.
 * Vowels and consonants are treated separately: distinct weight bias, and
 * the syllable templates always alternate C/V via the template letters
 * themselves.
 */
export function createLanguage(
	rng: () => number,
	vowels: string[] = DEFAULT_VOWELS,
	consonants: string[] = DEFAULT_CONSONANTS,
): Language {
	// Vowels: gentler bias so more than one vowel is "common".
	const vowelBias = 1.5 + rng() * 1.5;
	// Consonants: steeper bias → strong "favourite" consonants.
	const consonantBias = 2.5 + rng() * 2.5;

	const vowelWeights = naturalWeights(vowels.length, rng, vowelBias);
	const consonantWeights = naturalWeights(consonants.length, rng, consonantBias);

	// Pick 3..5 distinct syllable templates with random weights.
	const templateCount = 3 + Math.floor(rng() * 3);
	const chosen = new Set<string>();
	while (chosen.size < templateCount) {
		chosen.add(SYLLABLE_POOL[Math.floor(rng() * SYLLABLE_POOL.length)]);
	}

	const syllables = [...chosen];
	const syllableWeights = syllables.map(() => 0.2 + rng());

	const minSyllables = 1 + Math.floor(rng() * 2); // 1..2
	const maxSyllables = minSyllables + 1 + Math.floor(rng() * 3); // +1..+3
	const doublingChance = rng() * 0.15;

	return {
		vowels,
		consonants,
		vowelCdf: buildCdf(vowelWeights),
		consonantCdf: buildCdf(consonantWeights),
		syllables,
		syllableCdf: buildCdf(syllableWeights),
		minSyllables,
		maxSyllables,
		doublingChance,
	};
}

function pickVowel(lang: Language, rng: () => number): string {
	return pickFromCdf(lang.vowels, lang.vowelCdf, rng);
}

function pickConsonant(lang: Language, rng: () => number): string {
	return pickFromCdf(lang.consonants, lang.consonantCdf, rng);
}

/**
 * Build one word from `lang`. Rejects immediate repeats unless the language
 * decided to double a letter (per `doublingChance`).
 */
export function generateWord(lang: Language, rng: () => number): string {
	const span = lang.maxSyllables - lang.minSyllables + 1;
	const nSyl = lang.minSyllables + Math.floor(rng() * span);

	let out = '';
	let previous = '';
	for (let i = 0; i < nSyl; i++) {
		const tpl = pickFromCdf(lang.syllables, lang.syllableCdf, rng);
		for (const ch of tpl) {
			let letter = ch === 'V' ? pickVowel(lang, rng) : pickConsonant(lang, rng);
			// Avoid awkward immediate repeats unless the language doubles.
			if (letter === previous && rng() > lang.doublingChance) {
				letter = ch === 'V' ? pickVowel(lang, rng) : pickConsonant(lang, rng);
			}

			out += letter;
			previous = letter;
		}
	}

	return out;
}

/** Like `generateWord` but with the first letter capitalized. */
export function generateName(lang: Language, rng: () => number): string {
	const w = generateWord(lang, rng);
	if (w.length === 0) {
		return w;
	}

	return w[0].toUpperCase() + w.slice(1);
}

/**
 * Generate up to `maxAttempts` unique names from `lang`, respecting an
 * external `used` set. Falls back to numbered names if collisions persist.
 */
export function generateUniqueName(
	lang: Language,
	rng: () => number,
	used: Set<string>,
	maxAttempts = 30,
): string {
	for (let i = 0; i < maxAttempts; i++) {
		const n = generateName(lang, rng);
		if (!used.has(n)) {
			used.add(n);
			return n;
		}
	}

	let suffix = 2;
	const base = generateName(lang, rng);
	while (used.has(`${base} ${suffix}`)) {
		suffix++;
	}

	const final = `${base} ${suffix}`;
	used.add(final);
	return final;
}
