// Codex — the player's knowledge catalogue, as a REGISTRY (CANON S16/S20.1).
// ----------------------------------------------------------------
// One row per article, ordered by CodexArticleId; the unlock state is a bit
// per ordinal in PlayerState.codexUnlockedBits, and the CodexUnlock event
// carries the ordinal in ev.a. The articles lived as constexpr tables inside
// ui/overlays.cpp and the unlock state as a vector of id STRINGS — the UI
// layer owned game content, and a string was doing an ordinal's job (the
// same defect class the quest id hash had).
//
// This is macro/ (not content/) because every consumer must reach it from
// its own layer: macro/state.cpp seeds the starting bits, events/
// effect_applicator.cpp applies unlocks, content/plot emits them, ui draws
// them — macro is the lowest common floor, and the catalogue of what a
// PLAYER can know is player state's vocabulary.
//
// Adding an article = one enum value + one row (rows_in_enum_order refuses a
// drifted table). Ordinals ride the save, so reordering existing entries is
// a kSaveVersion bump like any other breaking change.
#pragma once
#include <cstddef>
#include <cstdint>
#include "core/table_guard.h"

namespace sm
{

    enum class CodexCategoryId : std::uint8_t
    {
        Lore = 0,
        Mechanics,
        Economy,
        Count,
    };

    struct CodexCategoryRow
    {
        CodexCategoryId id; // MUST equal the row's index (guard below)
        const char *title;
    };

    inline constexpr CodexCategoryRow kCodexCategories[std::size_t(
        CodexCategoryId::Count)] = {
        {CodexCategoryId::Lore, "Lore & World"},
        {CodexCategoryId::Mechanics, "RPG Mechanics"},
        {CodexCategoryId::Economy, "Economics"},
    };
    static_assert(rows_in_enum_order(kCodexCategories, &CodexCategoryRow::id),
                  "kCodexCategories row order must mirror CodexCategoryId");

    enum class CodexArticleId : std::uint8_t
    {
        Cosmology = 0,
        MageRulers,
        EmpireOfLight,
        Witches,
        Attributes,
        PerksSkills,
        Market,
        Settlements,
        Count,
    };
    inline constexpr std::size_t kCodexArticleCount =
        std::size_t(CodexArticleId::Count);

    struct CodexArticleRow
    {
        CodexArticleId id; // MUST equal the row's index (guard below)
        CodexCategoryId category;
        const char *title;
        const char *body;
    };

    inline constexpr CodexArticleRow kCodexArticles[kCodexArticleCount] = {
        {
            CodexArticleId::Cosmology,
            CodexCategoryId::Lore,
            "Cosmology",
            "Torus world created by dead gods.\n\n"
            "Two opposing forces:\n"
            "- Pure Magic: natural, impersonal, knowable energy.\n"
            "- Black Force: void/negation of people's desires and dead gods' "
            "whispers.\n\n"
            "When they meet, they annihilate each other. Black artifacts of "
            "dead gods exist and destabilize magic.\n\n"
            "Central Prophecy: A \"Black Child\" will be born, marking the "
            "end of the Pure Magic era.",
        },
        {
            CodexArticleId::MageRulers,
            CodexCategoryId::Lore,
            "Mage-Rulers",
            "The Magocracy of the Remnants of Magika.\n\n"
            "Arrogant and powerful mages - dukes, lords, archmages - rule "
            "the remnants of the Magika kingdoms. They exploit common "
            "people, considering them unworthy of true Pure Magic. In the "
            "kingdoms of Magika, magic is widespread and the primary measure "
            "of power. Even simple peasants possess basic spells. For elite "
            "mages, a peasant is a tool, a resource, expendable material.",
        },
        {
            CodexArticleId::EmpireOfLight,
            CodexCategoryId::Lore,
            "Empire of Light",
            "Theocratic empire. Magic is strictly forbidden under death "
            "penalty. Public religion; private corruption. "
            "Uses elite anti-mage warriors.\n\n"
            "Great Eunuchs (Shadow Rulers): 13 secret rulers. Publicly "
            "religious leaders; secretly serve black cults. They manipulate "
            "prophecy to prepare the world's transition toward the Black "
            "Child.",
        },
        {
            CodexArticleId::Witches,
            CodexCategoryId::Lore,
            "The Witches",
            "Immortal System Entities. Cannot be permanently killed; they "
            "reincarnate. Represent metaphysical principles:\n\n"
            "- Nefesh (Life): birth, transformation, cycle. Assigns "
            "arbitrary tasks.\n"
            "- Ain (Void): entropy and dissolution. Appears near ruined "
            "areas.\n"
            "- Tiferet (Present): \"now\". Focused on immediate action.\n"
            "- Hokma (Memory): recorded past. Knows everything that was "
            "written or marked.",
        },
        {
            CodexArticleId::Attributes,
            CodexCategoryId::Mechanics,
            "Attributes",
            "Eight primary attributes shape your character:\n\n"
            "- STR (Strength): +1 physical damage per point.\n"
            "- VIT (Vitality): +10 max HP per point.\n"
            "- END (Endurance): +10 max SP per point.\n"
            "- WIL (Willpower): +10 max MP per point.\n"
            "- INT (Intelligence): +1 spell damage per point.\n"
            "- WIS (Wisdom): +1% EXP bonus per point.\n"
            "- LCK (Luck): crit scaling and better loot.\n"
            "- CHA (Charisma): trade discount and relation bonus.\n"
            "- SPD (Speed): movement speed with asymptotic scaling.",
        },
        {
            CodexArticleId::PerksSkills,
            CodexCategoryId::Mechanics,
            "Skills & Perks",
            "Skills provide flat base stat increases applied before "
            "attribute-based multipliers. They do not modify attributes "
            "directly. Examples include Bodybuilding and martial "
            "disciplines.\n\n"
            "Perks are powerful, build-defining choices that provide both "
            "significant advantages and disadvantages. They are gained at "
            "level 1 and every 10 levels. Example: \"Immortal\" prevents "
            "death from old age, but requires much more experience to level "
            "up.",
        },
        {
            CodexArticleId::Market,
            CodexCategoryId::Economy,
            "Market System",
            "No global market. All trade is local and emergent. Prices "
            "fluctuate based on local supply and demand.\n\n"
            "Demand factor rises when demand outpaces supply, raising the "
            "target price. Charisma reduces the commission when buying from "
            "NPCs.",
        },
        {
            CodexArticleId::Settlements,
            CodexCategoryId::Economy,
            "Settlements & Caravans",
            "Villages gather resources via peasant squads. They store "
            "inventory locally and sell to caravans and cities.\n\n"
            "Cities buy resources, produce goods through production chains, "
            "spawn caravans for trade, and collect taxes based on population "
            "and trade volume.\n\n"
            "Caravans spawn at cities, load surplus goods, and travel using "
            "pathfinding toward destinations with strong profit estimates.",
        },
    };
    static_assert(rows_in_enum_order(kCodexArticles, &CodexArticleRow::id),
                  "kCodexArticles row order must mirror CodexArticleId");

    // The unlock state is one bit per article in a u64 — flat, POD, saved
    // whole. The assert is the loud cap: the 65th article widens the store
    // (an array of u64), it does not silently truncate.
    static_assert(kCodexArticleCount <= 64,
                  "codexUnlockedBits is one u64 — widen it before adding "
                  "a 65th article");

    inline constexpr std::uint64_t codex_bit(CodexArticleId id)
    {
        return 1ull << std::uint64_t(id);
    }

    // What a fresh player already knows: the world he was born into (lore of
    // his cosmos) and the rulebook basics the character sheet assumes.
    inline constexpr std::uint64_t kCodexInitialUnlockBits =
        codex_bit(CodexArticleId::Cosmology)
        | codex_bit(CodexArticleId::Attributes)
        | codex_bit(CodexArticleId::PerksSkills)
        | codex_bit(CodexArticleId::Market)
        | codex_bit(CodexArticleId::Settlements);

} // namespace sm
