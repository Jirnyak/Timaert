// Procedural language — faithful port of `src/game/language.ts`.

#include "macro/language.h"
#include "core/rng.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace sm {

namespace {

// ── Default inventories ──────────────────────────────────────
const std::vector<std::string> kDefaultVowels = {
    "a", "e", "i", "o", "u", "y"
};

const std::vector<std::string> kDefaultConsonants = {
    "b", "c", "d", "f", "g", "h", "k", "l", "m",
    "n", "p", "r", "s", "t", "v", "w", "z"
};

// Pool of syllable templates. Duplicates of "CV" / "CVC" bias the random
// pick toward those shapes (the Set in TS dedupes the *chosen* set, but
// the pool sampling itself is biased — this is mirrored here).
const std::vector<std::string> kSyllablePool = {
    "V", "CV", "VC", "CVC", "CV", "CV", "CVC", "CVV", "CCV", "CVCC", "CCVC", "VCC"
};

// ── Internal helpers ─────────────────────────────────────────

std::vector<float> build_cdf(const std::vector<float>& weights) {
    std::vector<float> cdf(weights.size(), 0.0f);
    float total = 0.0f;
    for (float w : weights) total += w;
    if (total <= 0.0f) total = 1.0f;
    float acc = 0.0f;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        acc += weights[i] / total;
        cdf[i] = acc;
    }
    if (!cdf.empty()) cdf.back() = 1.0f;
    return cdf;
}

template <typename T>
const T& pick_from_cdf(const std::vector<T>& items,
                       const std::vector<float>& cdf,
                       Rng& rng) {
    const float r = rng.next_f01();
    for (std::size_t i = 0; i < cdf.size(); ++i) {
        if (r <= cdf[i]) return items[i];
    }
    return items.back();
}

// Generate "Zipf-ish" frequency weights for `n` items: a few favourites,
// long tail. `bias` controls steepness.
std::vector<float> natural_weights(int n, Rng& rng, float bias) {
    std::vector<float> weights(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        const float r = rng.next_f01();
        weights[static_cast<std::size_t>(i)] = std::pow(r, bias) + 0.05f;
    }
    // Fisher-Yates shuffle so favourites aren't always the same positions.
    for (int i = n - 1; i > 0; --i) {
        const int j = static_cast<int>(rng.next_u32() % static_cast<std::uint32_t>(i + 1));
        std::swap(weights[static_cast<std::size_t>(i)],
                  weights[static_cast<std::size_t>(j)]);
    }
    return weights;
}

const std::string& pick_vowel(const Language& lang, Rng& rng) {
    return pick_from_cdf(lang.vowels, lang.vowelCdf, rng);
}

const std::string& pick_consonant(const Language& lang, Rng& rng) {
    return pick_from_cdf(lang.consonants, lang.consonantCdf, rng);
}

} // namespace

const std::vector<std::string>& default_vowels()     { return kDefaultVowels; }
const std::vector<std::string>& default_consonants() { return kDefaultConsonants; }

Language create_language(std::uint32_t seed) {
    return create_language(seed, kDefaultVowels, kDefaultConsonants);
}

Language create_language(std::uint32_t seed,
                         const std::vector<std::string>& vowels,
                         const std::vector<std::string>& consonants) {
    Rng rng(seed ? seed : 1u);

    // Vowels: gentler bias so >1 vowel is "common".
    const float vowelBias     = 1.5f + rng.next_f01() * 1.5f;
    // Consonants: steeper bias → strong "favourite" consonants.
    const float consonantBias = 2.5f + rng.next_f01() * 2.5f;

    const auto vowelWeights     = natural_weights(static_cast<int>(vowels.size()),     rng, vowelBias);
    const auto consonantWeights = natural_weights(static_cast<int>(consonants.size()), rng, consonantBias);

    // Pick 3..5 distinct syllable templates with random weights.
    const int templateCount = 3 + static_cast<int>(rng.next_u32() % 3u);
    std::vector<std::string> chosen;
    std::unordered_set<std::string> seen;
    while (static_cast<int>(chosen.size()) < templateCount) {
        const std::string& cand =
            kSyllablePool[rng.next_u32() % kSyllablePool.size()];
        if (seen.insert(cand).second) chosen.push_back(cand);
    }
    std::vector<float> syllableWeights(chosen.size(), 0.0f);
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        syllableWeights[i] = 0.2f + rng.next_f01();
    }

    const int   minSyllables   = 1 + static_cast<int>(rng.next_u32() % 2u); // 1..2
    const int   maxSyllables   = minSyllables + 1
                               + static_cast<int>(rng.next_u32() % 3u);     // +1..+3
    const float doublingChance = rng.next_f01() * 0.15f;

    Language lang;
    lang.vowels         = vowels;
    lang.consonants     = consonants;
    lang.vowelCdf       = build_cdf(vowelWeights);
    lang.consonantCdf   = build_cdf(consonantWeights);
    lang.syllables      = std::move(chosen);
    lang.syllableCdf    = build_cdf(syllableWeights);
    lang.minSyllables   = minSyllables;
    lang.maxSyllables   = maxSyllables;
    lang.doublingChance = doublingChance;
    lang.seed           = seed;
    return lang;
}

std::string generate_word(const Language& lang, Rng& rng) {
    const int span = lang.maxSyllables - lang.minSyllables + 1;
    const int nSyl = lang.minSyllables
                   + static_cast<int>(rng.next_u32() % static_cast<std::uint32_t>(span));

    std::string out;
    out.reserve(static_cast<std::size_t>(nSyl) * 3);
    std::string previous;

    for (int i = 0; i < nSyl; ++i) {
        const std::string& tpl = pick_from_cdf(lang.syllables, lang.syllableCdf, rng);
        for (char ch : tpl) {
            std::string letter = (ch == 'V')
                ? pick_vowel(lang, rng)
                : pick_consonant(lang, rng);
            // Avoid awkward immediate repeats unless the language doubles.
            if (letter == previous && rng.next_f01() > lang.doublingChance) {
                letter = (ch == 'V')
                    ? pick_vowel(lang, rng)
                    : pick_consonant(lang, rng);
            }
            out      += letter;
            previous  = letter;
        }
    }
    return out;
}

std::string generate_name(const Language& lang, Rng& rng) {
    std::string w = generate_word(lang, rng);
    if (!w.empty()) {
        w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
    }
    return w;
}

std::string generate_name(const Language& lang, std::uint32_t entitySeed) {
    // Per-call deterministic RNG mixed with the language's own seed so
    // different languages with the same `entitySeed` yield distinct names.
    Rng rng((entitySeed ^ (lang.seed * 16777619u)) | 1u);
    return generate_name(lang, rng);
}

std::string generate_unique_name(const Language& lang,
                                 Rng& rng,
                                 std::vector<std::string>& used,
                                 int maxAttempts) {
    auto contains = [&](const std::string& s) {
        return std::find(used.begin(), used.end(), s) != used.end();
    };

    for (int i = 0; i < maxAttempts; ++i) {
        std::string n = generate_name(lang, rng);
        if (!contains(n)) {
            used.push_back(n);
            return n;
        }
    }

    int suffix = 2;
    const std::string base = generate_name(lang, rng);
    std::string finalName = base + " " + std::to_string(suffix);
    while (contains(finalName)) {
        ++suffix;
        finalName = base + " " + std::to_string(suffix);
    }
    used.push_back(finalName);
    return finalName;
}

} // namespace sm
