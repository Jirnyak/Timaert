// Encounter table — built once, cached.
//
// Mirrors plot/encounters.ts. We use plain payload helpers to keep the
// table dense and readable.
#include "content/plot/encounters.h"

namespace sm::content {

namespace {

GameEvent gold(int delta) {
    GameEvent e{EventTag::PlayerGoldChange};
    e.ix = delta;
    return e;
}

GameEvent effect(const char* type, int value) {
    GameEvent e{EventTag::ApplyEffect};
    e.s1 = type;
    e.ix = value;
    return e;
}

GameEvent battle(const char* name, const char* type, int level) {
    GameEvent e{EventTag::BattleStart};
    e.s1 = name;
    e.s2 = type;
    e.ix = level;
    return e;
}

GameEvent codex(const char* id) {
    GameEvent e{EventTag::CodexUnlock};
    e.s1 = id;
    return e;
}

GameEvent rep(const char* faction, int delta) {
    GameEvent e{EventTag::ReputationChange};
    e.s1 = faction;
    e.ix = delta;
    return e;
}

std::vector<EncounterDef> build_table() {
    std::vector<EncounterDef> t;
    t.reserve(16);

    t.push_back({"Hidden Cache",
        "You stumble upon a hollow tree with a leather pouch inside.",
        {
            {"Take it",  {gold(25)}},
            {"Leave it", {}},
        }});

    t.push_back({"Herb Patch",
        "Fragrant medicinal herbs grow by the roadside.",
        {
            {"Gather herbs (+HP)", {effect("heal_hp", 30)}},
            {"Ignore",             {}},
        }});

    t.push_back({"Abandoned Campfire",
        "A still-warm campfire with leftover rations.",
        {
            {"Rest and eat", {effect("restore_sp", 9999), effect("heal_hp", 15)}},
            {"Search the area", {gold(25)}},
        }});

    t.push_back({"Traveling Merchant",
        "A merchant rests by the road. \"Care to trade or share a meal?\"",
        {
            {"Buy rations (50g)", {gold(-50), effect("heal_hp", 50)}},
            {"Rob him",           {battle("Angry Merchant", "merchant", 3)}},
            {"Leave",             {}},
        }});

    t.push_back({"Beggar",
        "A ragged man asks for a coin. \"Bless you, traveler.\"",
        {
            {"Give 10g", {gold(-10)}},
            {"Ignore",   {}},
        }});

    t.push_back({"Lost Child",
        "A crying child has lost their parents.",
        {
            {"Help (+XP)", {effect("grant_xp", 25)}},
            {"Ignore",     {}},
        }});

    t.push_back({"Bard",
        "A bard offers to sing a song of your deeds.",
        {
            {"Listen",   {effect("restore_mp", 9999)}},
            {"Tip 20g",  {gold(-20), effect("grant_xp", 15)}},
        }});

    t.push_back({"Bandit Ambush",
        "You hear a twig snap. \"Your money or your life!\"",
        {
            {"Fight!",   {battle("Highwayman", "bandit", 2)}},
            {"Pay 100g", {gold(-100)}},
        }});

    t.push_back({"Wolf Pack",
        "Growling shadows emerge from the bushes. Starving wolves.",
        {
            {"Defend yourself", {battle("Alpha Wolf", "bandit", 2)}},
            {"Run (-SP)",       {effect("drain_sp", 30)}},
        }});

    t.push_back({"Trap!",
        "You step into a snare trap!",
        {
            {"Break free (-15 HP)", {effect("damage_hp", 15)}},
            {"Wait for help",       {battle("Trapper", "bandit", 3)}},
        }});

    t.push_back({"Duel Challenge",
        "A wandering knight challenges you to a duel for honor.",
        {
            {"Accept",  {battle("Knight Errant", "peasant", 5)}},
            {"Decline", {}},
        }});

    t.push_back({"Mysterious Shrine",
        "An ancient stone shrine pulses with faint light.",
        {
            {"Pray",               {effect("restore_hp", 9999), effect("restore_mp", 9999)}},
            {"Take the offering",  {gold(50)}},
        }});

    t.push_back({"Black Monolith",
        "A jagged shard of obsidian rises from the earth, consuming the light around it.",
        {
            {"Study the runes", {codex("cosmology"), effect("grant_xp", 50)}},
            {"Smash the shard", {rep("empire", 5), rep("cults", -10)}},
        }});

    t.push_back({"Magical Flux",
        "The air shimmers like oil on water. Pure Magic erupts from a leyline!",
        {
            {"Absorb the energy",   {effect("restore_mp", 40), rep("magika", 3)}},
            {"Channel into a spell", {effect("grant_xp", 30)}},
        }});

    t.push_back({"Witch's Hut",
        "Smoke curls from a crooked chimney. A witch peers at you.",
        {
            {"Ask for a potion (30g)", {gold(-30), effect("restore_hp", 9999)}},
            {"Attack the witch",       {battle("Forest Witch", "witch", 7)}},
            {"Leave",                  {}},
        }});

    return t;
}

} // namespace

const std::vector<EncounterDef>& encounters() {
    static const std::vector<EncounterDef> kTable = build_table();
    return kTable;
}

} // namespace sm::content
