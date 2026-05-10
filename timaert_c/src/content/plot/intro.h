#pragma once

#include <cstddef>
#include <cstdint>

namespace sm {

class LogicNodeEngine;

namespace content {

enum class StoryPhaseKind : std::uint8_t {
    Slides,
    Choice,
    Input,
};

struct StorySlide {
    const char* image;
    const char* narration;
};

struct StoryChoice {
    const char* label;
    const char* description;
    const char* value;
    const char* image;
};

struct StoryPhaseDef {
    StoryPhaseKind kind;
    const char* id;
    const char* title;
    const char* description;
    const StorySlide* slides;
    std::size_t slideCount;
    const StoryChoice* choices;
    std::size_t choiceCount;
    const char* placeholder;
    const char* defaultValue;
    int maxLength;
};

struct StoryDef {
    const char* id;
    const char* sourceNodeId;
    const StoryPhaseDef* phases;
    std::size_t phaseCount;
};

const StoryDef& intro_story();
void register_intro_story_nodes(LogicNodeEngine& logic);

} // namespace content
} // namespace sm
