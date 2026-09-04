#include "content/plot/intro.h"
#include "content/plot/chapter_1.h"
#include "events/event_types.h"
#include "events/logic_nodes.h"

#include <cstdint>
#include <string_view>

#include "core/rng.h"
#include "macro/faction.h"

namespace sm::content {
namespace {

constexpr StorySlide kIntroSlides[] = {
    {"assets/backgrounds/intro0.png", "In the time before memory, the gods shaped a world upon the surface of a torus - infinite yet bounded."},
    {"assets/backgrounds/intro1.png", "Pure Magic flowed through every stone and river, the breath of creation itself."},
    {"assets/backgrounds/intro2.png", "But the gods grew jealous of their own work... and destroyed one another."},
    {"assets/backgrounds/intro3.png", "Their corpses became the Black Force - void and negation, whispering from beyond."},
    {"assets/backgrounds/intro4.png", "Where Pure Magic and Black Force meet, both are annihilated. The world trembles."},
    {"assets/backgrounds/intro5.png", "Kingdoms rose. Mage-lords built towers of arrogance. Empires banned magic under pain of death."},
    {"assets/backgrounds/intro6.png", "Barbarian kings seized castles from slain wizards, promising freedom they could not deliver."},
    {"assets/backgrounds/intro7.png", "A prophecy speaks of a Black Child - herald of the end of Pure Magic."},
    {"assets/backgrounds/intro8.png", "And now, traveller, you arrive. The world does not yet know your name."},
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

// The intro is PURE SLIDES since 2026-09-03: the asking (sex, name, homeland)
// moved to the pre-world character creation screen, which renders the same
// authored choice tables through creation_*_choices below. Since 2026-09-04
// these nine play BEFORE the world exists (the IntroSlides screen); the world
// itself opens with the single arrival slide below.
constexpr StoryDef kIntroStory = {
    "intro",
    "intro_main",
    kIntroSlides,
    sizeof(kIntroSlides) / sizeof(kIntroSlides[0]),
};

// PLACEHOLDER text and a borrowed frame (owner authors both later): the slot
// matters — the first thing the world says, through the same overlay channel
// chapter breaks and scene interludes will use.
constexpr StorySlide kArrivalSlides[] = {
    {"assets/backgrounds/intro8.png", "The road has carried you here. Whatever you were before, Timaert will ask again."},
};

constexpr StoryDef kArrivalStory = {
    "arrival",
    "intro_main",
    kArrivalSlides,
    sizeof(kArrivalSlides) / sizeof(kArrivalSlides[0]),
};

LogicNode intro_main_node() {
    LogicNode n;
    n.id = kArrivalStory.sourceNodeId;
    n.label = "Intro Sequence";
    n.tags.push_back("intro");
    n.tags.push_back("plot");
    n.effect = [](NodeContext& ctx) {
        const StoryDef& story = arrival_story();
        GameEvent ev{EventTag::ShowStory};
        ev.s1 = story.sourceNodeId;
        ev.s2 = story.id;
        ev.ix = static_cast<int>(story.slideCount);
        ctx.bus->emit(ev);
    };
    return n;
}

} // namespace

const StoryDef& intro_story() {
    return kIntroStory;
}

const StoryDef& arrival_story() {
    return kArrivalStory;
}

const StoryChoice* creation_sex_choices(std::size_t& count) {
    count = sizeof(kSexChoices) / sizeof(kSexChoices[0]);
    return kSexChoices;
}

const StoryChoice* creation_realm_choices(std::size_t& count) {
    count = sizeof(kRealmChoices) / sizeof(kRealmChoices[0]);
    return kRealmChoices;
}

const char* resolve_homeland_faction(const char* choiceValue,
                                     std::uint32_t worldSeed) {
    if (!choiceValue || !choiceValue[0]) return nullptr;
    const std::string_view value{choiceValue};

    // A homeland choice that names several realms. One row per group, beside
    // the choices it belongs to, so adding "the Free Cities" is a row here and
    // a row in kRealmChoices — never a branch in the code that awards
    // reputation.
    struct HomelandGroup {
        std::string_view value;              // the choice's stored value
        const char* const* realms;           // faction registry ids
        std::size_t count;
    };
    static constexpr const char* kBarbarianRealms[] = {
        "barbarian_north", "barbarian_south", "barbarian_west", "barbarian_east",
    };
    static constexpr HomelandGroup kHomelandGroups[] = {
        {"barbarians", kBarbarianRealms,
         sizeof(kBarbarianRealms) / sizeof(kBarbarianRealms[0])},
    };

    for (const HomelandGroup& g : kHomelandGroups) {
        if (g.value != value) continue;
        // The world decides which of them raised you, and it decides ONCE:
        // seeded from the world, so the same world always answers the same way
        // and a reload cannot move your birthplace.
        const std::uint32_t pick =
            hash3(worldSeed, 0x484F4D45u /*'HOME'*/, 0u)
            % std::uint32_t(g.count);
        return g.realms[pick];
    }

    // Not a group — then it must be a realm the registry actually knows. This
    // is the guard that was missing: "barbarians" fell through to
    // add_player_reputation and quietly went nowhere, so one of the three
    // starting buttons did nothing at all.
    return faction_index(choiceValue) >= 0 ? choiceValue : nullptr;
}

void register_intro_story_nodes(LogicNodeEngine& logic) {
    logic.add(intro_main_node());
    register_chapter_1_nodes(logic);
    logic.activate(kIntroStory.sourceNodeId);
}

} // namespace sm::content
