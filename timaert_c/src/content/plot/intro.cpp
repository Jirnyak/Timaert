#include "content/plot/intro.h"
#include "events/event_types.h"
#include "events/logic_nodes.h"

#include <cstdint>

namespace sm::content {
namespace {

constexpr StorySlide kIntroSlides[] = {
    {"/assets/backgrounds/intro0.png", "In the time before memory, the gods shaped a world upon the surface of a torus - infinite yet bounded."},
    {"/assets/backgrounds/intro1.png", "Pure Magic flowed through every stone and river, the breath of creation itself."},
    {"/assets/backgrounds/intro2.png", "But the gods grew jealous of their own work... and destroyed one another."},
    {"/assets/backgrounds/intro3.png", "Their corpses became the Black Force - void and negation, whispering from beyond."},
    {"/assets/backgrounds/intro4.png", "Where Pure Magic and Black Force meet, both are annihilated. The world trembles."},
    {"/assets/backgrounds/intro5.png", "Kingdoms rose. Mage-lords built towers of arrogance. Empires banned magic under pain of death."},
    {"/assets/backgrounds/intro6.png", "Barbarian kings seized castles from slain wizards, promising freedom they could not deliver."},
    {"/assets/backgrounds/intro7.png", "A prophecy speaks of a Black Child - herald of the end of Pure Magic."},
    {"/assets/backgrounds/intro8.png", "And now, traveller, you arrive. The world does not yet know your name."},
};

constexpr StoryChoice kSexChoices[] = {
    {"Male", "Strong mind - +1 skill point", "male", "/assets/sprites/male.png"},
    {"Female", "Strong body - +1 attribute point", "female", "/assets/sprites/female.png"},
};

constexpr StoryChoice kRealmChoices[] = {
    {"Magocracy of Magika", "Land of mage-lords. Magic is everyday life.", "magika", nullptr},
    {"Empire of Light", "Holy empire. Magic is forbidden on pain of death.", "empire", nullptr},
    {"Barbarian Kingdoms", "Feudal warlords ruling by sword and steel.", "barbarians", nullptr},
};

constexpr StoryPhaseDef kIntroPhases[] = {
    {
        StoryPhaseKind::Slides,
        nullptr,
        nullptr,
        nullptr,
        kIntroSlides,
        sizeof(kIntroSlides) / sizeof(kIntroSlides[0]),
        nullptr,
        0,
        nullptr,
        nullptr,
        0,
    },
    {
        StoryPhaseKind::Choice,
        "sex",
        "Choose Your Nature",
        "The body shapes the mind, and the mind shapes destiny.",
        nullptr,
        0,
        kSexChoices,
        sizeof(kSexChoices) / sizeof(kSexChoices[0]),
        nullptr,
        nullptr,
        0,
    },
    {
        StoryPhaseKind::Input,
        "name",
        "What Is Your Name?",
        "A name is the first claim a soul makes upon the world.",
        nullptr,
        0,
        nullptr,
        0,
        "Enter your name",
        "Traveller",
        24,
    },
    {
        StoryPhaseKind::Choice,
        "realm",
        "Choose Your Homeland",
        "Where you were born determines who you must become - or defy.",
        nullptr,
        0,
        kRealmChoices,
        sizeof(kRealmChoices) / sizeof(kRealmChoices[0]),
        nullptr,
        nullptr,
        0,
    },
};

constexpr StoryDef kIntroStory = {
    "intro",
    "intro_main",
    kIntroPhases,
    sizeof(kIntroPhases) / sizeof(kIntroPhases[0]),
};

constexpr std::uint32_t total_choices(const StoryDef& story) {
    std::uint32_t total = 0;
    for (std::size_t i = 0; i < story.phaseCount; ++i) {
        total += static_cast<std::uint32_t>(story.phases[i].choiceCount);
    }
    return total;
}

constexpr int input_max_length(const StoryDef& story) {
    for (std::size_t i = 0; i < story.phaseCount; ++i) {
        if (story.phases[i].kind == StoryPhaseKind::Input) {
            return story.phases[i].maxLength;
        }
    }
    return 0;
}

LogicNode intro_main_node() {
    LogicNode n;
    n.id = kIntroStory.sourceNodeId;
    n.label = "Intro Sequence";
    n.tags.push_back("intro");
    n.tags.push_back("plot");
    n.effect = [](NodeContext& ctx) {
        const StoryDef& story = intro_story();
        GameEvent ev{EventTag::ShowStory};
        ev.s1 = story.sourceNodeId;
        ev.s2 = story.id;
        ev.ix = static_cast<int>(story.phaseCount);
        ev.iy = story.phaseCount > 0 ? static_cast<int>(story.phases[0].slideCount) : 0;
        ev.a = total_choices(story);
        ev.b = static_cast<std::uint32_t>(input_max_length(story));
        ctx.bus->emit(ev);
    };
    return n;
}

} // namespace

const StoryDef& intro_story() {
    return kIntroStory;
}

void register_intro_story_nodes(LogicNodeEngine& logic) {
    logic.add(intro_main_node());
}

} // namespace sm::content
