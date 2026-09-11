// Procedural language — faithful port of `src/game/language.ts`.
//
// A `Language` is a deterministic phonotactic config: vowel + consonant
// inventories with Zipf-ish frequency CDFs, plus a small set of weighted
// syllable templates ('CV', 'CVC', 'V', etc.). Same Language + same RNG →
// same word.
//
// The TS module takes an `rng: () => number` callable and threads it
// through every random call. The C++ port preserves that pattern: pass
// an `Rng&` to whichever helper performs the randomness, or use the
// `entitySeed` overloads for one-shot naming.

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sm {

struct Rng;

struct Language {
    std::vector<std::string> vowels;
    std::vector<std::string> consonants;
    std::vector<float>       vowelCdf;      // cumulative, last == 1.0
    std::vector<float>       consonantCdf;
    std::vector<std::string> syllables;     // e.g. {"CV","CVC","V"}
    std::vector<float>       syllableCdf;
    int   minSyllables    = 1;
    int   maxSyllables    = 3;
    float doublingChance  = 0.0f;
    std::uint32_t seed    = 0;              // for entitySeed-derived RNG
};

// Construct a Language deterministically from `seed`.
Language create_language(std::uint32_t seed);

// A FACTION's naming language, derived — never stored (owner 2026-09-11:
// kingdoms are gone, so the realm language that lived on struct Kingdom
// derives from the world seed and the faction registry index instead;
// genesis and later namers get the same tongue by construction).
inline Language faction_language(std::uint32_t worldSeed,
                                 std::uint16_t factionIdx) {
    return create_language(worldSeed
                           ^ (std::uint32_t(factionIdx) * 0x9E3779B1u));
}
Language create_language(std::uint32_t seed,
                         const std::vector<std::string>& vowels,
                         const std::vector<std::string>& consonants);

// Lower-case word — randomness driven by caller-provided rng.
std::string generate_word(const Language& lang, Rng& rng);

// Capitalised name. RNG-form: caller controls state. EntitySeed-form:
// derives a fresh per-call Rng (used by politik when naming cities).
std::string generate_name(const Language& lang, Rng& rng);
std::string generate_name(const Language& lang, std::uint32_t entitySeed);

} // namespace sm
