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

    } // namespace content
} // namespace sm
