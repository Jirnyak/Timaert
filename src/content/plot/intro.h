#pragma once

#include <cstddef>
#include <cstdint>

namespace sm
{

    class LogicNodeEngine;

    namespace content
    {

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

        // A story IS a run of slides since 2026-09-03: the asking phases
        // (Choice/Input) moved to the pre-world creation screen and no story
        // ever used them again, so the phase layer was one indirection deep
        // for exactly one caller. Future chapter breaks and scene interludes
        // are runs of slides too — a new story is a new table, not a new kind.
        struct StoryDef
        {
            const char *id = "";
            const char *sourceNodeId = "";
            const StorySlide *slides = nullptr;
            std::size_t slideCount = 0;
        };

        // The pre-world slideshow (9 slides): played by the IntroSlides
        // screen between the title menu and character creation, on no engine
        // at all — the world does not exist yet.
        const StoryDef &intro_story();

        // The one in-world opening slide (owner verdict 2026-09-04): the
        // intro_main node emits THIS on the first world tick, through the
        // same story-overlay channel future chapter breaks will use. Its
        // text is a placeholder until the owner authors the real one.
        const StoryDef &arrival_story();

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
