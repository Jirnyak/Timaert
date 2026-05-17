# Timaert Design Document

## 0. Document Role

This file is the current design target for the native Timaert game in
`timaert_c/`.

The legacy documents under `../docs/design/` are historical references. They
may contain useful ideas, but they are not the active design authority for the
native build.

The active code authorities are:

- `../src/` for TypeScript/Svelte gameplay behaviour, formulas, content
  tables, and UI expectations.
- `timaert_c/` for the shipping native architecture, performance constraints,
  layer discipline, and corrected final design decisions.

The native game is already playable. All design work must respect the playable
baseline and deepen it through content, systemic depth, and measured native
performance. Do not replace working systems with speculative rewrites.

## 1. One-Sentence Vision

Timaert is a procedural open-world fantasy RPG where a living Mount and
Blade-like campaign world zooms into Might and Magic-style local action RPG
zones, while Dwarf Fortress-like simulation and Daggerfall-like scale keep the
world moving for years of in-game time.

## 2. Main References

### 2.1 Mount and Blade

The macro world must feel alive.

Cities, villages, caravans, lords, guards, merchants, bandits, pilgrims,
witches, soldiers, cultists, and monster squads move through the world for
their own reasons. The player is one actor among many, not the only source of
motion.

Borrowed pillars:

- Campaign-map travel.
- Local settlement economies.
- Caravans and trade routes.
- Kingdoms, wars, raids, patrols, and territory.
- Hiring soldiers and growing from adventurer to warlord.
- Player reputation and faction relations.
- Conquest as a systemic outcome.

Timaert must not copy Mount and Blade's separate tactical battle layer. Combat
in the final native design happens in the same subworld action RPG engine used
for exploration.

### 2.2 Dwarf Fortress

The world should have simulation pressure even when the player ignores it.

Borrowed pillars:

- World state matters over long time spans.
- Population, resources, production, mood, and ownership form feedback loops.
- Strange events can emerge from geography, factions, and history.
- The world is not just a stage; it is an active machine.

Timaert does not need Dwarf Fortress-level microscopic simulation everywhere.
The goal is high-value simulation: enough persistent state to make travel,
trade, quests, war, and exploration feel grounded.

### 2.3 Might and Magic 6, 7, 8

The local world should feel like a first-person action RPG with big readable
sprite enemies, direct movement, dangerous mobs, corpse loot, and powerful
spells.

Borrowed pillars:

- First-person exploration.
- Hordes of sprite monsters.
- Melee attackers, missile attackers, and spellcasters.
- Corpse interaction and loot transfer.
- Experience from kills and quests.
- Towns with citizens and guards.
- Dangerous dungeons, ruins, outdoor zones, towers, and strange places.
- Flight, Armageddon-like destructive magic, town portal/recall style magic,
  buffs, direct damage, and utility spells.

Timaert should preserve the feeling of danger: entering the wrong ruin or
high-zone wilderness early can kill the player.

### 2.4 Daggerfall

The world should feel larger than the main quest.

Borrowed pillars:

- Large seamless travel scale.
- Many settlements and points of interest.
- Procedural local places.
- Sandbox progression and side activity depth.
- The possibility to ignore the main quest for a long time.

Timaert's toroidal 1024 x 1024 macro grid and 1024-cell subworld zoom are the
technical basis for this scale.

## 3. Current Playable Baseline

The current native game already has these pillars implemented or partially
implemented:

- SDL2 + OpenGL 3.2 Core + ImGui app shell.
- C++23 with no exceptions and no RTTI.
- EnTT world for entity storage and component-driven systems.
- 1024 x 1024 toroidal macro world.
- GPU-generated master terrain fields: height, moisture, temperature, mask,
  and river data.
- Ten biomes: tundra, taiga, snow, valley, meadow, swamp, desert, steppe,
  tropics, water.
- Feature layer: roads, dirt roads, trees, mountains, and feature upload to
  the macro renderer.
- Politik generator: kingdoms, capitals, cities, city connections, land
  snapping, Voronoi ownership, languages, flags.
- Settlement and village lists populated from politics.
- Economy state on settlements and villages.
- Daily world tick: settlement production, village gathering, prices,
  population, mood, garrison growth, active trade routes, upkeep, time.
- Macro NPC spawning around settlements, villages, and global pools.
- Macro NPC AI behaviours: home wanderer, woodcutter, trader, nomad,
  aggressive, patrol, teleporter, wanderer.
- Macro map hover/click pathing and path-cost generation.
- Difficulty zones generated after civilization and features.
- Subworld entry from macro cells.
- Seamless 3 x 3 subworld manager.
- First-person 3D subworld renderer plus top-down 2D view.
- Subworld generators for natural cells, settlements, roads, spires, ruins,
  water, forest, mountain, swamp, and other modes.
- Universal C++ combat model based on NPC-kind `CombatTemplate` data.
- Soldier squads as persistent NPC-kind records.
- Corpse loot and XP attribution in subworld combat.
- Danger-zone exit blocking for high danger cells.
- Spell registry, spell book, cooldowns, sustained state, projectiles, beams,
  auras, Haste, Flight, and Armageddon.
- Event bus, effect applicator, logic nodes, quest engine.
- ShowDialog and ShowStory presentation path.
- Settlement, diplomacy, quest, codex, map, spell, trade, and NPC action UI
  surfaces in ImGui.
- Save/load and save roundtrip tests.

This is the playable baseline. Every future feature must preserve it.

## 4. Final Game Pillars

### 4.1 Living World First

The player should believe that the world is moving with or without them.

Minimum expectations:

- Caravans travel because price gradients exist.
- Villages gather because terrain resources exist.
- Cities produce and consume because population needs exist.
- Guards patrol because settlements need security.
- Bandits attack because trade and weak targets exist.
- Lords move because kingdom goals exist.
- Cults, monsters, and strange powers spread because danger zones and plot
  state exist.

### 4.2 One World, Two Scales

Macro and micro are not two separate games.

The macro world answers:

- Where am I?
- Who owns this land?
- What is the terrain?
- What is nearby?
- How dangerous is this area?
- What economy and faction pressures exist?

The micro world answers:

- What does this cell feel like under the player's feet?
- Who is physically here?
- What can be fought, looted, explored, talked to, or activated?
- What local consequences happen right now?

Subworld cells must inherit macro context. A city cell becomes a city; a road
cell has road structure; a mountain cell rises; a dangerous ruin produces
dangerous local content.

### 4.3 Combat Is Unified

There is no separate final battle mode.

Combat is normal subworld play. The same engine handles:

- A bandit ambush.
- Fighting a guard in a city.
- A war party clash.
- Clearing a ruin.
- Fighting summoned monsters.
- A caravan raid.
- Defending a village.
- A late-game demon incursion.

Every NPC kind owns its combat data. Hireable NPC kinds can become soldiers.
Soldiers are not abstract unit-type counts.

### 4.4 Data-Driven Expansion

The final game must be expandable.

Adding content should usually mean adding data:

- A new NPC kind.
- A new spell registry row and effect module.
- A new quest objective checker.
- A new reward applier.
- A new landmark generator.
- A new faction definition.
- A new item or loot table.
- A new perk or skill integration point.

The engine should not become a pile of `if this specific content id` branches.

### 4.5 Long Campaign Pressure

The intended main quest length is about ten in-game years or more.

The player can spend years:

- Trading.
- Exploring.
- Building reputation.
- Hiring soldiers.
- Serving kingdoms.
- Joining wars.
- Seeking spells.
- Clearing ruins.
- Founding a dynasty.
- Becoming immortal or dying old.

The main plot should be designed for a level 100-ish endgame. Midgame is
around level 20-30. There is no hard level cap.

## 5. World Structure

### 5.1 Macro Grid

The macro world is a 1024 x 1024 toroidal grid.

Important properties:

- Coordinates wrap around.
- Terrain generation must be seamless at world edges.
- Pathfinding must understand torus distance.
- Roads and political ownership must respect terrain and water rejection.
- Cell context must be stable enough for save/load and subworld generation.

Each cell can expose:

- Macro height.
- Moisture.
- Temperature.
- Biome.
- Feature.
- River strength.
- Road/dirt road flags.
- Zone difficulty.
- Political owner.
- Landmark presence.
- Nearby active NPCs.

### 5.2 Biomes

Current biome set:

- Tundra.
- Taiga.
- Snow.
- Valley.
- Meadow.
- Swamp.
- Desert.
- Steppe.
- Tropics.
- Water.

Biomes determine:

- Visual macro texture.
- Subworld terrain style.
- Resource tendencies.
- Movement costs.
- Fauna and monster tables.
- Tree density and structure scatter.
- Difficulty modifiers.
- Quest and event flavour.

Future biome expansion must add one data row/config plus renderer/generator
support, not broad engine branching.

### 5.3 Features

Current feature types:

- None.
- Road.
- Tree.
- Mountain.
- Dirt road.

Feature ordering matters:

1. Mountains.
2. Trees.
3. Dirt roads.
4. Roads.

Roads and dirt roads must not stamp water. Trees should avoid rivers and
shorelines according to current masks. Mountains derive from height thresholds
and must remain compatible with subworld height amplification and zone
generation.

### 5.4 Rivers

Rivers are part of the terrain identity.

Design requirements:

- Visible on the macro renderer.
- Relevant to tree exclusion.
- Relevant to road/path planning.
- Relevant to subworld water/shoreline context.
- Potentially relevant to trade, fertility, settlement placement, and quests.

Rivers should not become arbitrary decorative lines. They should be exposed as
data and consumed by systems that need water context.

### 5.5 Difficulty Zones

Zone levels are a parallel progression system.

Zone meaning:

- 0: safe city core.
- 1: settled land.
- 2: patrolled roads and villages.
- 3: frontier.
- 4: wild.
- 5: untamed.
- 6: perilous.
- 7: forsaken.
- 8: cursed.
- 9: hellgate.

Zone consumers:

- Subworld monster level scaling.
- Ambush probability.
- Loot quality.
- Quest difficulty.
- Spell and spire placement.
- Ruin and portal spawn logic.
- Exit blocking in subworld danger.

Zone design goal:

- Cities and roads make safety pockets.
- Wild areas remain risky.
- Mountains, forests, and remote land push danger upward.
- Plot and corruption can create local spikes.
- Deep danger must reward exploration with rare loot, spells, and story.

## 6. Macro Gameplay

### 6.1 Starting Situation

The player begins near a city.

Starting conditions:

- Low level.
- Little money.
- Basic inventory.
- Weak combat stats.
- No large army.
- Local rumours or visible nearby objectives.
- Nearby safe settlement access.
- Nearby danger if the player chooses to risk it.

The first decision should be meaningful:

- Enter the city and look for quests.
- Trade on a small route.
- Follow the road to another settlement.
- Investigate a marker or landmark.
- Talk to passing NPCs.
- Enter the nearby wilderness.
- Attack or rob someone and accept the consequences.

### 6.2 Travel

Macro travel should cost time and stamina.

Factors:

- Terrain movement cost.
- Roads and dirt roads.
- Travel skill.
- Speed attribute.
- Flight and Haste-style macro effects.
- Army burden and inventory weight.
- Danger zone and ambush risk.

Travel must be strategic, not just cursor movement.

### 6.3 Encounters

Macro encounters should be generated from local context.

Examples:

- Bandits near profitable trade routes.
- Guards near cities and roads.
- Woodcutters near forests.
- Caravans between price gradients.
- Witches in forests and strange zones.
- Cultists near corrupted ruins.
- Demons in high danger zones.
- Lords near borders, war targets, or capitals.

Encounter result options:

- Talk.
- Trade.
- Bribe.
- Threaten.
- Join.
- Attack.
- Flee.
- Enter subworld action combat.
- Trigger a quest or story node.

### 6.4 Time

Time advancement is central.

Current code tracks day, hour, and minute. Daily ticks update settlements,
villages, active trade routes, garrisons, player upkeep, and player age.

Final design target:

- One year is 100 days.
- The main plot can take around ten years.
- NPCs and player age.
- Long wars and economy shifts are possible.
- Some quests expire.
- Some plot states worsen if ignored.
- Aging and legacy become real systems.

Older TS comments and legacy docs may contain conflicting year assumptions.
The final design target is 100 days.

## 7. Economy

### 7.1 Economy Goal

The economy is not a minigame isolated from the world. It is the reason many
NPCs move and the reason trade, raids, protection, politics, and quests exist.

The player should be able to play as a trader, but profit must carry risk.

### 7.2 Resources

Current resources:

- Grain.
- Wood.
- Iron.
- Clay.
- Silver.
- Gems.

Resource design:

- Terrain and biome influence local availability.
- Villages gather resources.
- Cities consume resources to produce goods.
- Scarcity and distance create price gradients.
- Rare resources support high-value goods and quests.

### 7.3 Goods

Goods are generated from resource pairs.

Current goods include:

- Bread.
- Tools.
- Bricks.
- Coins.
- Jewelry.
- Weapons.
- Furniture.
- Silverware.
- Ornaments.
- Tiles.
- Armor.
- Crowns.
- Pottery.
- Sculptures.
- Regalia.

Goods influence:

- Population growth.
- Wealth.
- Happiness.
- Trade value.
- Quest needs.
- War supply.
- Settlement mood.

### 7.4 Villages

Villages gather.

Village gameplay roles:

- Early quests.
- Local information.
- Resource origin.
- Vulnerable raid targets.
- Peasant and worker NPCs.
- Small market access.
- Future recruitment and taxation nodes.

Final village depth:

- Named families and elders.
- Seasonal production.
- Bandit pressure.
- Famine and resource shortages.
- Local reputation.
- Protection contracts.
- Village upgrades if player owns nearby settlement.

### 7.5 Cities

Cities produce, consume, hire, govern, and create conflict.

City gameplay roles:

- Market.
- Quest hub.
- Political owner seat.
- Garrison source.
- Guards and citizens.
- Caravan dispatch.
- Trade history.
- Future lord and court system.

Final city depth:

- Districts or local subworld areas.
- Crime and guard response.
- Shops and guilds.
- Noble or council NPCs.
- City ownership transfer.
- Taxes and projects.
- Siege consequences.
- Population mood affecting revolt.

### 7.6 Caravans

Caravans are moving economy.

Caravan behaviour should account for:

- Origin surplus.
- Destination demand.
- Travel time.
- Road safety.
- Faction relations.
- Active war.
- Bandit threat.
- Player reputation.

Player interactions:

- Escort.
- Trade.
- Rob.
- Scout route.
- Sabotage enemy economy.
- Protect own kingdom trade.

### 7.7 Local Markets

No global market.

Prices should come from:

- Local supply.
- Local demand.
- Population.
- Wealth.
- Happiness.
- Distance and risk.
- War and blockade.
- Player charisma and reputation.

Trade should become a readable but uncertain game:

- Buy where supply is high.
- Sell where demand is high.
- Protect cargo.
- Watch roads.
- Exploit wars.
- Avoid being destroyed by stronger parties.

## 8. Politics

### 8.1 Current Kingdoms

Current major lineages:

- Magika.
- Empire.
- Timaert.
- Barbarians.

Current kingdom examples:

- Old Magica.
- Northern Magica.
- Lower Magica.
- Lake Duchy.
- Empire of Light.
- Republic of Timaert.
- North Barbarians.
- South Barbarians.
- West Barbarians.
- East Barbarians.

Each kingdom has data:

- Id.
- Name.
- Lineage.
- Preferred seed region.
- City count range.
- Colour.
- Priority.
- Capital placement constraints.

### 8.2 Faction Relations

Relations should drive:

- War.
- Trade.
- Patrol hostility.
- Quest availability.
- NPC dialogue.
- Market access.
- Caravan routes.
- Player reputation consequences.
- Settlement ownership and rebellion.

### 8.3 Future Factions

Future factions should include:

- Cults.
- Bandits.
- Monsters.
- Wildlife.
- Pilgrims.
- Mage hunters.
- Merchant guilds.
- Witches.
- Deserters.
- Rebels.
- Local city factions.
- Player kingdom.

Faction additions should be data-driven. A faction needs relationship defaults,
spawn context, event hooks, and content tables, not hardcoded engine logic.

### 8.4 War

War should emerge from faction state.

War systems should eventually include:

- Lord parties.
- Patrols.
- Raids.
- Sieges.
- Border skirmishes.
- Caravan attacks.
- Village devastation.
- City ownership transfer.
- Truces.
- Tribute.
- Player mercenary service.
- Player vassalage.
- Player conquest.

War must use the same macro and subworld systems. A siege is not a separate
abstract resolver; it is a set of parties, garrisons, city state, events,
and local combat opportunities.

## 9. NPCs

### 9.1 Current NPC Kinds

Current native NPC kinds:

- Peasant.
- Woodcutter.
- Merchant.
- Caravan.
- Bandit.
- Guard.
- Witch.
- Sorceress.

Each kind carries:

- Label.
- Portrait.
- Base HP.
- Base level.
- AI behaviour selector.
- Combat template.
- Upkeep if hireable.
- Hireable flag.
- XP reward.
- Name pool.
- Talk lines.

### 9.2 NPC AI

Current behaviours:

- HomeWanderer.
- Woodcutter.
- Trader.
- Nomad.
- Aggressive.
- Patrol.
- Teleporter.
- Wanderer.

Future behaviours:

- Lord campaign AI.
- War target selection.
- Caravan profit routing.
- Guard crime response.
- Citizen daily schedule.
- Cult ritual movement.
- Monster lair patrol.
- Companion follow and autonomy.
- Family and heir behaviour.

### 9.3 NPC Context

NPCs must carry macro context into subworld.

A guard in a city should know:

- Which settlement they serve.
- Which kingdom owns it.
- Whether the player is hostile.
- Whether a crime happened.
- Whether war is ongoing.
- Whether the player is famous, wanted, allied, or noble.

A caravan should know:

- Origin.
- Destination.
- Cargo.
- Faction.
- Escort strength.
- Route risk.
- Trade value.

A lord should know:

- Kingdom.
- Army.
- Orders.
- Personal relation.
- Estate/city ties.
- War goals.

### 9.4 NPC Expansion Contract

To add an NPC kind:

1. Add enum value.
2. Add registry row.
3. Add AI selector or reuse existing one.
4. Add combat template.
5. Add loot table if needed.
6. Add spawn table/context if needed.
7. Add UI portrait/sprite/paperdoll if needed.

No broad engine changes should be necessary.

## 10. Army and Recruitment

### 10.1 Final Army Model

The final native design uses NPC-kind soldiers.

An army is a list of persistent soldier records:

- Stable entity id.
- NPC kind.
- Level.

Stats come from the NPC kind's combat template and level scaling.

This allows:

- Peasants as cheap recruits.
- Guards as trained troops.
- Special NPC kinds as rare soldiers.
- Future mercenaries, cultists, summons, monsters, companions.

### 10.2 No Legacy RPS Resolver

The TypeScript reference still contains old `UnitType` and rock-paper-scissors
army structures. The native final design removed those for good reasons.

Do not reintroduce:

- `UnitType` as final native combat schema.
- Swordsman/Archer/Spearman/Horseman RPS tables.
- Modal battle resolver.
- Per-unit-type histogram as the player's final army representation.

If TS compatibility still needs those fields temporarily, isolate them and
remove readers as the native model becomes authoritative.

### 10.3 Hiring

Hiring should be atomic:

- Validate kind is hireable.
- Find matching garrison soldier.
- Check gold.
- Charge price.
- Move soldier record to player squad.

Upkeep:

- Base upkeep per NPC kind.
- Level factor.
- Charisma discount.
- Future leadership/perk modifiers.

### 10.4 Army Gameplay

Player army should support:

- Protection during travel.
- Subworld combat companions.
- Caravan attacks.
- Village defense.
- City conquest.
- Faction service.
- Desertion if unpaid.
- Morale and loyalty later.

## 11. Subworld

### 11.1 Role

The subworld is the detailed local view of macro cells.

It supports:

- City streets.
- Villages.
- Forests.
- Mountains.
- Swamps.
- Water and coast.
- Roads.
- Ruins.
- Spires.
- Future caves, labyrinths, castles, hell worlds, and strange portals.

### 11.2 Scale

Each macro cell becomes a 1024 x 1024 local tile map.

The active subworld is a 3 x 3 grid:

- Center cell.
- Eight neighbours.
- 3072 x 3072 total local area.
- Boundary recentering when crossing cell edges.

This gives local continuity without generating the entire macro world at local
detail.

### 11.3 Context Inheritance

Subworld generation reads macro context:

- Cell coordinate.
- Biome.
- Feature.
- Zone level.
- Macro height.
- Neighbour biomes.
- Neighbour features.
- Landmark presence.
- Seed.

The subworld should not invent contradictory terrain.

Examples:

- A road macro cell contains a road local structure.
- A mountain macro cell has amplified local height.
- A water macro cell has water plane and shoreline behaviour.
- A city macro cell produces city layout and citizens.
- A spire macro cell produces a tower and spell interaction.
- A high danger cell produces stronger fauna/monsters.

### 11.4 Local Exploration

Local exploration should include:

- Moving in first person.
- Attacking and casting.
- Looting corpses.
- Talking to NPCs.
- Trading inside settlements.
- Finding quest targets.
- Entering structures.
- Activating spires.
- Discovering portals.
- Fighting through hordes.

### 11.5 View Modes

Native default target:

- First-person 3D for final gameplay.
- Top-down 2D as debug/practical alternate mode.

First-person rendering needs clarity more than realism:

- Readable terrain.
- Readable enemies.
- Readable projectiles.
- Clear water and roads.
- Clear interaction prompts.
- Stable performance.

## 12. Combat

### 12.1 Combat Role

Combat is Might and Magic-style action RPG combat.

The player should fight:

- Lone enemies.
- Hordes.
- Missile attackers.
- Spellcasters.
- Guards.
- Bandits.
- Monsters.
- War parties.
- Summoned creatures.
- Bosses.

### 12.2 Combat Template

Every combat-capable NPC kind has a combat template:

- HP.
- Damage.
- Speed.
- Attack range.
- Cooldown.
- Label.
- Attack kind.
- Missile speed.
- Missile blast.
- Missile colour.

The same data supports ordinary NPC combat and soldier projection.

### 12.3 Attack Kinds

Current attack kinds:

- Melee.
- Missile.

Future attack kinds or spell behaviours can be added, but should still fit
the same subworld action model.

Possible future behaviours:

- Area burst.
- Cone.
- Beam.
- Summon.
- Aura.
- Teleport strike.
- Fear/confusion.
- Healing/support.

### 12.4 Hostility

Hostility is faction-driven.

Principles:

- Friendly attacks reduce reputation.
- Reputation below hostile threshold makes faction auto-aggro.
- Guards respond to crimes.
- War factions are hostile by default.
- Allied factions tolerate or forgive more, but not infinite murder.

### 12.5 Loot and XP

Might and Magic-style rules:

- Killing blows matter.
- XP goes to the killer or killer's party as appropriate.
- Corpses contain loot.
- Corpses can be interacted with.
- Loot tables belong to NPC/item data.
- Empty loot is still a valid corpse state.

Future improvements:

- Corpse decay timers.
- Search skill modifiers.
- Luck-based quality.
- Crime consequences for looting citizens.
- Rare artifacts from bosses and ruins.

### 12.6 Danger Exit Gate

High-danger subworld cells can block exit.

Purpose:

- Prevent trivial escape from serious danger.
- Make high-zone exploration meaningful.
- Support ambushes, demon zones, and cursed ruins.

Exit blocking must be clear to the player through status text and UI feedback.

## 13. Spells

### 13.1 Current Spells

Current spell list:

- Magic Bolt.
- Fireball.
- Ice Shard.
- Lightning Chain.
- Energy Beam.
- Haste.
- Flight.
- Armageddon.

### 13.2 Spell Data

Spell definitions should include:

- Id.
- Name.
- Icon and source icon.
- Tags.
- Rarity.
- Delivery shape.
- Tier.
- Mana cost.
- Cooldown.
- Cast time.
- Sustained state.
- Mana drain.
- Micro effect.
- Macro effect.
- Base damage/heal/radius.
- Scaling fields.
- Status effect.
- Description.
- Pros/cons.

### 13.3 Micro Spells

Micro spells affect subworld combat.

Examples:

- Magic Bolt: cheap basic projectile.
- Fireball: explosive projectile with friendly fire.
- Ice Shard: high single-target burst and chill.
- Lightning Chain: bouncing damage.
- Energy Beam: line beam.
- Armageddon: large destructive meteor/nova effect.
- Haste: sustained movement/attack buff.
- Flight: sustained vertical/terrain movement benefit.

### 13.4 Macro Spells

Macro effects should eventually include:

- Travel speed.
- Terrain bypass.
- Mark and Recall.
- Town portal.
- Region reveal.
- Weather or terrain influence.
- Army buff.
- Region damage.
- Corruption or purification.
- Settlement panic or protection.

Macro spells must have consequences if they are destructive. Armageddon should
not be free power; it should affect reputation, settlements, NPC deaths, and
possibly world corruption.

### 13.5 Spell Learning

Spell acquisition should come from:

- Spires.
- Ruins.
- Artifacts.
- Rare teachers.
- Plot events.
- Faction rewards.
- Dangerous bargains.

High-tier spells should not be ordinary shop items.

### 13.6 Future Spell List

Target expansions:

- Mark.
- Recall.
- Town Portal.
- Heal.
- Cure poison/disease/curse.
- Shield.
- Stone Skin.
- Bless.
- Invisibility.
- Water Walk.
- Telekinesis.
- Summon.
- Charm/Fear.
- Meteor Shower.
- Resurrection.
- Lich transformation.
- Portal to strange worlds.

## 14. RPG Progression

### 14.1 Levels

There is no hard level cap.

Milestones:

- Early game: level 1-10.
- Mid game: level 20-30.
- Late main plot: around level 100.
- Sandbox can continue beyond.

Current formula:

`EXP_next(level) = floor(1000 * level * (0.1 * level + 1))`

Enemy XP:

`EXP_fight(enemyLevel, modifier) = floor(10 * enemyLevel * modifier)`

Quest XP:

`EXP_quest(questLevel, modifier) = floor(100 * questLevel * modifier)`

### 14.2 Attributes

Current attributes:

- STR: physical damage and carry capacity.
- VIT: max HP and HP regeneration.
- END: max SP and SP regeneration.
- WIL: max MP and MP regeneration.
- INT: spell damage.
- WIS: XP multiplier.
- LCK: critical scaling and better loot.
- CHA: relation bonus, trade discount, army upkeep discount.
- SPD: movement speed.

### 14.3 Skills

Current skills:

- Bodybuilding.
- Meditation.
- Travel.
- Fighter.
- Endurance.
- Spellcraft.
- Weightlifting.

Future skills should support builds:

- Trading.
- Leadership.
- Scouting.
- Diplomacy.
- Necromancy.
- Elemental schools.
- Repair/crafting.
- Medicine.
- Stealth/crime.
- Navigation.

### 14.4 Perks

Perks are permanent build-defining choices.

Existing examples:

- Immortal.
- Short-Lived.
- Mechanical.
- Talented.
- Gifted.
- Natural.
- Educated.
- Leader.
- Saint.
- Revenant.
- Demiurg.

Perk design rule:

- Strong upside.
- Real downside or opportunity cost.
- Meaningful identity.
- Works in both macro and micro where possible.

### 14.5 Build Archetypes

Mage:

- INT, WIL, Spellcraft.
- Learns spire spells.
- Uses Flight, Haste, projectiles, beams, and destructive magic.
- Weak early, terrifying later.

Warrior:

- STR, VIT, END, Fighter, Bodybuilding.
- Survives hordes.
- Uses melee and heavy equipment.
- Can clear dangerous areas without large army.

Leader:

- CHA, Leadership future skill, reputation.
- Hires and sustains army.
- Gains faction power.
- Conquers and governs.

Trader:

- CHA, Travel, Weightlifting, market knowledge.
- Turns geography into money.
- Uses escorts, bribes, and safe routes.

Explorer:

- SPD, Travel, LCK, scouting.
- Finds ruins, spires, portals, and rare loot.
- Avoids or escapes impossible fights.

Hybrid:

- Expected and supported.
- The game should not force one class path.

## 15. Aging, Family, and Legacy

### 15.1 Aging Goal

Time should matter.

The player can spend ten or more in-game years in the world. Aging should
eventually become a strategic and narrative pressure.

### 15.2 Year Length

Final design target:

- 100 days per year.

Older docs and some TS comments may contradict this. Align future code and
saves toward 100-day years when the aging system is expanded.

### 15.3 Mortality

Human characters can eventually die of old age.

Ways to resist aging:

- Immortality perk.
- Lichdom.
- Divine blessing.
- Artifact.
- Rejuvenation magic.
- Long-lived ancestry.
- Mechanical or undead form.

Tradeoffs:

- Long-lived ancestry such as elf can have slower XP gain.
- Lichdom can damage reputation or lock faction paths.
- Mechanical bodies can block normal leveling.
- Divine immortality can impose quest obligations.

### 15.4 Family and Legacy

Future legacy systems:

- Marriage or partnership.
- Children.
- Heirs.
- Family reputation.
- Inheritance.
- Successor play after death.
- Dynastic enemies and allies.

This supports open-ended sandbox play without requiring every death to be a
hard reset.

## 16. Items, Loot, and Inventory

### 16.1 Item Role

Items support:

- Combat power.
- Economy.
- Quest objectives.
- Magic progression.
- Character identity.
- Trade routes.
- Artifact hunting.

### 16.2 Loot Sources

Loot can come from:

- NPC corpses.
- Monsters.
- Ruins.
- Spires.
- Quest rewards.
- Shops.
- Caravans.
- Faction gifts.
- World events.

### 16.3 Corpse Loot

Corpse loot is a key Might and Magic reference.

Rules:

- Killed NPC can leave corpse.
- Corpse has inventory.
- Player interacts to transfer loot.
- Corpse can be empty.
- Loot table remains data-driven.

### 16.4 Artifacts

Artifacts should be rare and storyful.

Sources:

- High-danger ruins.
- Bosses.
- Ancient spires.
- Other worlds.
- Main quest.
- Faction secrets.

Artifacts can affect:

- Aging.
- Spell access.
- Faction relations.
- Combat stats.
- World corruption.
- Kingdom legitimacy.

## 17. Quests

### 17.1 Quest Types

Current categories:

- Main.
- Side.
- Procedural.

### 17.2 Objective Verbs

Current objective verbs:

- Visit cell.
- Find location.
- Deliver items.
- Destroy NPC.
- Wait at location.
- Interact with cell.

This is a good base. Add new objective verbs only when a new gameplay action
cannot be represented by existing verbs.

### 17.3 Rewards

Current reward types:

- Gold.
- XP.
- Item.
- Reputation.
- Event.

Future rewards:

- Spell learn.
- Perk unlock.
- Faction rank.
- Settlement ownership.
- Companion join.
- Map reveal.
- Artifact.
- Family/legacy state.

### 17.4 Procedural Quest Context

Procedural quests should use:

- Settlement economy.
- Village resources.
- Nearby roads.
- Nearby danger zones.
- Faction ownership.
- Player reputation.
- Active wars.
- Nearby NPCs.
- Nearby landmarks.
- Plot phase.

Examples:

- A city lacks tools because bandits cut the trade road.
- A village asks for protection after repeated raids.
- A lord requests scouting before a siege.
- A witch asks for a rare component from a swamp ruin.
- A merchant wants escort through a high-zone pass.
- A cult event creates a timed corruption objective.

### 17.5 Main Quest

The main quest should be authored but world-bound.

Design rule:

- Main plot beats are authored.
- Locations, factions, supporting NPCs, and local details are selected from
  generated world context.

This gives structure without killing replayability.

### 17.6 Plot Duration

Expected main plot duration:

- Around ten in-game years or more.
- Late plot around level 100.
- Player can delay or ignore it, with consequences.

## 18. Events and Logic Nodes

### 18.1 Event Role

Events are the connective tissue.

They describe:

- NPC death.
- NPC spawn.
- Settlement visit.
- Quest start/update/complete/fail.
- Spell cast.
- Trade.
- World cell change.
- Time advance.
- Reputation change.
- Dialog/story presentation.
- Battle start/end compatibility events.
- Magic surge.
- Camera movement.

### 18.2 Logic Nodes

Logic nodes allow condition-to-effect gameplay.

Use them for:

- Story progression.
- Quest branching.
- Random events.
- Dialog outcomes.
- World reactions.
- Tutorial or intro steps.

### 18.3 Effect Applicator

Effects should mutate state through explicit verbs.

Examples:

- Heal HP.
- Damage HP.
- Restore MP/SP.
- Drain SP.
- Grant XP.
- Unlock codex.
- Complete/fail quest.
- Emit dialog/story.
- Spawn entity.
- Change reputation.

Adding a new effect should mean adding one effect verb implementation, not
sprinkling special cases across UI and game loops.

## 19. Landmarks

### 19.1 Current Landmarks

Current active landmark classes:

- Cities.
- Villages.
- Spires.
- Markers/POIs.

Subworld generation already supports more local modes such as ruins and roads.

### 19.2 Future Landmark Types

Add:

- Castles.
- Ruins.
- Monster dens.
- Cult temples.
- Mage towers.
- Labyrinths.
- Caves.
- Ancient machines.
- Demon gates.
- Other-world portals.
- Player camps.
- Player-founded settlements.

### 19.3 Landmark Contract

A landmark should define:

- Macro marker/registry data.
- Placement rules.
- Political owner or wilderness state.
- Zone/danger influence.
- Subworld generator mode.
- Quest/event hooks.
- Loot/spell/content tables if needed.
- Save data if persistent mutable state exists.

## 20. Other Worlds and Portals

Portals should extend late-game exploration.

Other worlds can include:

- Hellish demon zones.
- Strange dream worlds.
- Dead cities.
- Elemental planes.
- Ancient machine spaces.
- Witch realms.

Design principles:

- Access through rare landmarks, spells, artifacts, or plot.
- Higher danger and stranger rules.
- Unique resources, monsters, spells, and artifacts.
- Consequences on return.

Other worlds can reuse subworld architecture with alternate macro context or
dedicated local generators.

## 21. UI and Player Information

### 21.1 UI Goal

The UI should make systemic decisions readable.

The player needs to understand:

- Current HP/MP/SP.
- Gold.
- Inventory and weight.
- Level/XP.
- Time.
- Position.
- Danger zone.
- Current path/travel cost.
- Market prices.
- Quest objectives.
- Faction relation.
- Army size and upkeep.
- Current spell and cooldown.
- Nearby interactables.

### 21.2 Macro UI

Macro UI surfaces:

- Top status bar.
- Bottom command toolbar.
- Map overlay.
- Diplomacy overlay.
- Settlement overlay.
- Quest overlay.
- Codex.
- Inventory/stat/equipment future surfaces.
- NPC proximity/action panel.

### 21.3 Subworld UI

Subworld UI surfaces:

- First-person view.
- HP/MP/SP.
- Active spell.
- Combat log/status line.
- Interaction prompt.
- Loot prompt.
- Exit/blocked-exit feedback.
- Minimap or local map later.

### 21.4 UI Tone

The game is a dark fantasy RPG, but the UI should remain readable and useful.

Avoid decorative interfaces that hide important numbers. The player should be
able to repeatedly trade, travel, fight, and inspect systems quickly.

## 22. Visual Direction

### 22.1 Macro Visuals

Macro world should show:

- Procedural terrain texture.
- Shorelines.
- Rivers.
- Roads.
- Dirt roads.
- Forests.
- Mountains.
- Cities.
- Villages.
- Spires.
- NPC parties.
- Player marker.
- Political and zone overlays.

Visual priority:

1. Readable map.
2. Clear strategic information.
3. Distinct biomes.
4. Attractive procedural texture.
5. No visual lies: if a road appears, it should be traversable and data-backed.

### 22.2 Subworld Visuals

Subworld visual goals:

- Might and Magic-like first-person scale.
- Sprite/paperdoll billboards for NPCs and monsters.
- Readable projectile and spell effects.
- Terrain height, roads, water, trees, structures.
- Clear corpses/loot.
- City citizens and guards.
- Dungeons/ruins with strong silhouettes.

### 22.3 Art Expansion

Art systems should remain data-driven:

- Sprite atlas manifests.
- Paperdoll descriptors.
- Palette data.
- Animation metadata.
- NPC visual presets.

Do not create per-frame textures in hot render paths.

## 23. Audio

Current native audio uses SDL_mixer.

Audio roles:

- Title/menu music.
- Macro exploration music.
- Subworld music.
- One-shot spell/NPC/event SFX.
- Future combat tension and biome ambience.

Audio failures must not kill normal gameplay unless initialization contract
explicitly requires it. Missing asset spam must not happen per frame.

## 24. Save and Persistence

### 24.1 Save Philosophy

The game is pre-release.

No backward or forward save compatibility is required. Breaking save changes
should bump `kSaveVersion` and invalidate old saves.

### 24.2 Persistent State

Persistent state should include:

- World seed and map parameters.
- Player state.
- Time.
- Settlements and villages.
- Economy state.
- Factions and relations.
- Politics.
- Markers.
- Spires.
- Army/soldiers.
- Active trade routes.
- Quests.
- Codex.
- Event history as needed.
- Future family/legacy state.
- Future ownership/conquest state.
- Future subworld-local persistent changes for important cells.

### 24.3 Deterministic Regeneration

Do not save data that can be deterministically regenerated unless it is needed
for player-visible persistence or performance.

Good regenerated data:

- Base terrain.
- Base zones from seed/civilization.
- Base procedural subworld layouts if untouched.

Must persist:

- Player actions.
- Loot taken.
- NPC deaths if persistent.
- Ownership changes.
- Quest state.
- Spire depletion.
- Settlement state.
- Family/legacy state.

## 25. Performance Rules

### 25.1 Native Performance Goal

The C++ port exists because the full TS/Vite/WebGL build becomes too slow for
the intended scale.

Performance principles:

- Use contiguous data.
- Avoid per-frame allocation.
- Avoid per-frame string formatting.
- Avoid per-frame map/set insertion in hot loops.
- Use EnTT views for entity iteration.
- Use bounded pathfinding.
- Use deterministic fixed-size or reserved buffers where practical.
- Keep rendering GPU-driven where possible.

### 25.2 Hot Paths

Hot paths include:

- Macro renderer.
- Subworld renderer.
- NPC AI tick.
- Subworld combat tick.
- Pathfinding.
- World generation.
- Seamless subworld recentering.
- Spell/projectile ticking.

Do not add convenience abstractions that allocate or hide costly work inside
these paths.

### 25.3 Worker/Async Systems

Recent worker/seam commits are experimental unless proven by build, tests, and
runtime smokes.

Acceptance for async generation:

- Mac Ninja build passes.
- MSVC build passes where relevant.
- `subworld_async_seam_test` passes.
- Real subworld seam smoke passes.
- No deadlocks on quit.
- No runaway worker threads.
- No blocking seam recentering on full generation.
- Timing is measured and not worse than accepted baseline.

If a worker change breaks portable native build, correctness, or frame timing,
revert the worker change rather than papering over it.

## 26. Build and Verification

### 26.1 Portable Native Build

Expected macOS/Linux build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Native dependencies:

- CMake.
- Ninja.
- SDL2.
- SDL2_mixer with MP3 support.
- OpenGL 3.2 Core on macOS.

### 26.2 Windows Build

Windows/MSVC is a verification target, not gameplay authority.

Known command:

```cmd
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"
```

### 26.3 Tests

Important focused tests:

- `quest_lifecycle_test`.
- `save_roundtrip_test`.
- `spell_casting_effects_test`.
- `combat_squad_test`.
- `audio_contract_test`.
- `audio_runtime_test`.
- `pathfinding_parity_test`.
- `feature_layer_parity_test`.
- `character_paperdoll_test`.
- `character_paperdoll_gl_smoke_test`.
- `road_river_generation_test`.
- `subworld_generator_parity_test`.
- `subworld_async_seam_test`.
- `subworld_spawn_parity_test`.

### 26.4 Smokes

Runtime smokes should prove player-visible flows:

- Title launch.
- New game boot.
- Macro walking.
- Save/load.
- Settlement trade.
- Quest accept.
- NPC talk/trade/attack.
- Spell overlay/casting.
- Subworld time.
- Subworld seam crossing.
- Subworld audio transition.
- Story/dialog overlays.

Build success alone is not gameplay parity.

## 27. TS to C++ Translation Plan

### 27.1 Translation Goal

The goal is not line-by-line syntax copying.

The goal is native gameplay parity or an explicitly documented native
improvement.

### 27.2 Authority Order

For gameplay:

1. Read matching TS module in `../src/`.
2. Read current native implementation in `timaert_c/src/`.
3. Check `ARCHITECTURE.md` for layer and design rules.
4. Check focused tests and smoke evidence.
5. Implement the smallest native change that preserves or improves behaviour.

For UX shell:

- TS/Svelte is current UI behaviour reference.
- `proto_c/` is old feel reference only.

For final combat model:

- Native universal NPC-soldier combat is the final authority.
- Do not revive legacy TS battle mode as final design.

### 27.3 Module Walk

Every TS module should be walked by exports:

- Types.
- Constants.
- Functions.
- Registries.
- Content tables.
- Side effects.
- UI contracts.

Each export should be classified:

- Ported.
- Native-replaced.
- Temporary compatibility.
- Not started.
- Intentionally skipped.

This status belongs in `translation.md`, not in vague commit messages.

### 27.4 Porting Slices

Preferred slice shape:

1. Read TS module and direct callers.
2. Read native target and direct callers.
3. Identify data contract.
4. Implement C++ data structures or registry row.
5. Wire consumers.
6. Add/adjust focused test.
7. Run build and focused tests.
8. Update `translation.md` only with evidence.

### 27.5 Known Divergences

Important divergences:

- TS `army.ts` contains `UnitType` and RPS tables; native final design uses
  NPC-kind soldiers.
- TS `GameSubState` contains `battle`; native final design has no separate
  battle mode.
- Road generation intentionally diverges from TS corridor snapping; native A*
  baseline is protected by rejected-water invariant tests.
- Some TS UI shells are not duplicated exactly in ImGui if the gameplay path is
  already accessible.
- Save versions differ; no compatibility required.
- Aging year-length assumptions need final alignment to 100 days.

### 27.6 Acceptance Criteria

A translation task is complete only if:

- Relevant TS was read.
- Native code matches behaviour or documents intentional divergence.
- It respects layers.
- It is data-driven.
- It preserves playable baseline.
- Build passes.
- Focused tests pass.
- Remaining gaps are written honestly.

## 28. Near-Term Roadmap

### 28.1 Stabilize Native Build

Immediate priority:

- Mac Ninja build must keep working.
- Windows build must keep working when available.
- Worker/seam changes must be reverted if they break portable native build or
  runtime stability.
- Zero-warning target should be restored where practical.

### 28.2 Finish Translation Ledger

Finish export walk for:

- State.
- Army/NPC.
- Economy.
- Politics.
- Events.
- Quests.
- Spells.
- Subworld.
- UI overlays.

### 28.3 Living Macro Expansion

Add:

- Lords.
- War parties.
- Patrols.
- Siege goals.
- Caravan risk calculation.
- Crime and guard response.
- Ownership transfer.
- Player kingdom state.

### 28.4 Content Depth

Add:

- More monster families.
- More NPC kinds.
- More spells including Mark/Recall.
- More ruins.
- Labyrinths.
- Portals.
- Artifacts.
- Procedural quest templates.
- Authored main quest beats.

### 28.5 Long Campaign Systems

Add:

- Aging effects.
- Old-age death.
- Family.
- Heirs.
- Legacy succession.
- Immortality paths.
- Lich/artifact/perk lifespan modifications.

## 29. Non-Goals

Do not add:

- Separate native battle screen.
- RPS army resolver as final design.
- Global market replacing local economies.
- Save compatibility scaffolding.
- Content-specific engine branches.
- Per-frame heap churn in hot systems.
- Unbounded pathfinding/generation.
- Worker code without proof.
- Renderer rewrites without screenshots/smokes.

## 30. Final Experience Target

A successful Timaert campaign should feel like this:

The player starts weak near a city. They hear rumours, see roads, watch
caravans, and decide whether to trade safely or risk the wild. Early choices
are small: buy goods, deliver a quest, fight a bandit, loot a corpse, flee a
monster too strong for them.

Over time, the map becomes personal. The player remembers which roads are
unsafe, which city pays well for silver, which kingdom hates them, which ruin
almost killed them, which spire taught Flight, which lord betrayed them, which
village they saved, and which caravan they robbed.

Midgame, the player has a build. A mage flies over terrain and burns hordes. A
warrior clears ruins alone. A trader runs guarded routes. A leader hires troops
and joins wars. An explorer enters places nobody sane would visit.

Late game, the world has changed. Wars have moved borders. Settlements have
grown or suffered. The player may command an army, own a city, found a kingdom,
seek immortality, enter other worlds, finish the main quest, or ignore it and
keep shaping the sandbox.

The game should feel procedural, dangerous, systemic, and personal.
