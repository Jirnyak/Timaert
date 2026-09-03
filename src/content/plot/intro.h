#pragma once

#include <cstddef>
#include <cstdint>

namespace sm
{

    class LogicNodeEngine;

    namespace content
    {

        enum class StoryPhaseKind : std::uint8_t
        {
            Slides,
            Choice,
            Input,
        };

        struct StorySlide
        {
            const char *image = "";
            const char *narration = "";
        };

        struct StoryChoice
        {
            const char *label = "";
            const char *description = "";
            const char *value = "";
            const char *image = "";
        };

        struct StoryPhaseDef
        {
            StoryPhaseKind kind = StoryPhaseKind::Slides;
            const char *id = "";
            const char *title = "";
            const char *description = "";
            const StorySlide *slides = nullptr;
            std::size_t slideCount = 0;
            const StoryChoice *choices = nullptr;
            std::size_t choiceCount = 0;
            const char *placeholder = "";
            const char *defaultValue = "";
            int maxLength = 0;
        };

        struct StoryDef
        {
            const char *id = "";
            const char *sourceNodeId = "";
            const StoryPhaseDef *phases = nullptr;
            std::size_t phaseCount = 0;
        };

        const StoryDef &intro_story();
        void register_intro_story_nodes(LogicNodeEngine &logic);

        // The authored creation choices (sex, homeland) — the character
        // creation screen renders THESE rows, verbatim. They lived as intro
        // story phases until 2026-09-03; the owner's creation-screen verdict
        // moved the asking to a pre-world screen, and the intro became pure
        // slides. One authored table, one renderer at a time.
        const StoryChoice *creation_sex_choices(std::size_t &count);
        const StoryChoice *creation_realm_choices(std::size_t &count);

        // Resolve a homeland CHOICE to a real row of the faction registry.
        //
        // A choice may name a GROUP: "Barbarian Kingdoms" is not a country, it
        // is four of them (barbarian_north/south/west/east), and the owner's
        // ruling is that the player is simply born in one of them — they are
        // procedural, so which one is the world's business, not a fifth button
        // on the intro screen. `worldSeed` decides, so the answer is fixed for
        // a given world and a reload cannot re-roll your homeland.
        //
        // A choice that already names one realm returns it unchanged. An
        // UNKNOWN id returns nullptr — the caller must not silently award
        // reputation to a country that does not exist, which is exactly what
        // "barbarians" did for as long as that button has been on screen.
        const char *resolve_homeland_faction(const char *choiceValue,
                                             std::uint32_t worldSeed);

    } // namespace content
} // namespace sm
