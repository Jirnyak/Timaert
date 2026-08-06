# Lore — Лор мира Тимаэрт

The canonical fiction of the world: metaphysics, history, powers, named
figures, the ten-year clock, and the endings — **and the mechanic each one is
made of**.

- **Source of record:** the owner's own account (conversation transcript,
  2026-08-05). Everything below is his canon; this file only organises it.
- **Design authority:** [design.md](design.md) (systems), this file (fiction).
- **Rule of this document:** lore and mechanics are **the same thing**. A lore
  claim that no system produces is marked `— NOT BUILT`, and a system that
  contradicts the fiction is a bug in one of the two, to be resolved by the
  owner, never silently.

## 0. Tone — the register the world is written in

Deliberately **on the edge of absurd** (owner: «я стараюсь лор держать на грани
абсурда»). Not solemn epic pathos: hypertrophied archetypes taken to their
absolute, played straight. The Peasant King. The Thirteen Great Eunuchs. The
Sacrilegist. The names are meant to lodge in memory in one hearing and to be
repeatable as a joke without losing an ounce of their weight — a relative who
has never played a game calls him "the king of the bums", and that is a
success, not a failure.

The second register is the counterweight: **the world is mechanically about
peasant labour, merchant caravans, lordly squabbles and craft output**. The
dead gods whisper *over a working economy*, and that is exactly why the horror
lands. High metaphysics on top of grounded microeconomics.

The third: **the world does not care about the player.** History's flywheels
turned for a thousand years before he was born and keep turning while he hauls
grain.

## 1. Metaphysics — the two forces

Two forces, and the law between them.

- **Magic (Магия)** — the primordial force of *nature*, the living substance of
  the world. Not sorcery, not sin, not corruption. It simply is what the world
  is made of.
- **Black energy (Чёрная энергия)** — the **traces of the dead gods**, who
  created the world an eternity ago and then vanished. Residue, not a presence.
  Chaotic, outside the physics and mathematics of this world.
- **THE LAW: magic and black energy annihilate on contact.** This is the single
  rule from which most of the world's politics falls out for free:
  - Where living magic is strong, black energy cannot take root — so **there
    are no cults in the Magikas**.
  - Where the Empire of Light burns magic out, a **vacuum** opens and there is
    nothing left to oppose black energy — so **cults grow fastest inside the
    most sterile, most policed state in the world**, as a direct consequence of
    its own crusade.
  - The wilds have no central power to clean them, so old traces linger there.

- **Causality (Причинность)** is the third, quieter force — the will of the
  dead gods expressed as *what is allowed to happen*. It is the in-fiction
  reason certain figures cannot be permanently killed (see §5), and it is a law
  of the world rather than an engine exception. When there is no mind left on
  the map to observe it, causality itself breaks (see §8, the Empty Land
  ending).

- **The whisper of the gods (шёпот богов)** is how black energy recruits: not
  by intervention but by **changing the mind of whoever believes in it**. This
  is stated as metaphysics in the fiction and implemented literally as game
  design (§9).

## 2. Timeline

| When | Event |
|---|---|
| An eternity ago | The gods create the world and **vanish**. Nothing of them remains but traces — black energy — and the black artifacts made from it. |
| −1000 years | **The Sacrilegist (Святотатец)**, the greatest mage who ever lived, founds the kingdom of **Magika**. He withdraws from ruling it almost at once and spends his reign teleporting across the world **destroying black artifacts and cults**. |
| −1000 years | War with the **Empire of Light**. He annihilates **half its army** — and then **truly disappears**. Not dies: disappears. He is the engine of the whole plot and is never once present in it. |
| −1000 → 0 | Magika **fragments** into several successor states — the "high kingdoms": few lands, deep coffers, strong mages, fading grandeur. Cults and black artifacts, nearly extinguished by the Sacrilegist, begin their slow thousand-year return. |
| Recent decades | The **Peasant King (Царь-крестьянин)** rises with a **black spear** — a black artifact whose nature *nobody knows, including him* — and frees the peasantry from the mages. The freed lands become the **Barbarian Kingdoms**, ruled by feudal warlords. |
| The east | The **Republic of Timaert**, across the sea, industrialises: the first locomotives, the first firearms, line infantry and galleons. The game is named after it. |
| **Year 0** | **The game begins.** The player is an ordinary inhabitant, attacked by bandits in a forest, robbed and **left to die**. A witch, **Nefesh**, saves him. |
| **Year +10** | Default doom. The Peasant King, **betrayed by the barbarian feudal kings**, in despair runs his spear through an infant. The **shadows of the dead gods condense**. Demons take the world. |

**The clock in engine terms.** **Year 0 is tick 0** — there is no calendar era,
no in-fiction dating system, no "Year 312 of the Third Concord". The world's
origin point *is* the start of the game, and the clock the player reads is the
one the engine keeps ([time.md](time.md)). Ten in-game years = 10 × 2²⁰ ticks =
**10 485 760 ticks** = 1280 days ≈ **45.5 hours of real time** at the shipped
ladder (64 ticks / real second, day = 8192, year = 2²⁰). The dates in the table
above are relative to that zero and exist for authors, not for the player.

The plot's duration and [design.md](design.md) §17.6 ("around ten in-game years,
late plot around level 100") agree; this is the arithmetic that ties them.

## 3. The powers

Every realm below is **already a row** in the one faction registry
(`src/macro/faction.h`) — kingdoms are ordinary rows, and a faction's relations
follow from its `Temperament` plus authored pair overrides. The lore column and
the registry column are meant to stay in sync; see §11 for the current diffs.

### 3.1 The Magikas — weakened high kingdoms

*Registry: `old_magica`, `northern_magica`, `lower_magica`, `lake_duchy` —
`Temperament::Magical`. Plus `magika`, the itinerant mage orders sworn to no
crown.*

A thousand years of decline since the Sacrilegist. **Little territory, enormous
resources, very strong mages.** Elitist, conservative, decaying grandeur — a
defensive monolith that holds its borders against much larger armies because
of what a single archmage does to an army.

- **Northern Magika vs Southern Magika** fight each other as a **proxy war of
  the Empire of Light**. **"Southern Magika" is another name for Lower Magica**
  (owner's ruling) — there is no separate row and none is wanted; `lower_magica`
  is the south. The registry already carries the political shape of it: the one
  authored alliance override in the whole file is `empire` ↔ `lower_magica`, so
  the south is the Empire's client by construction. — *the war itself is
  `NOT BUILT`; today the northern/southern pair band comes from temperament.*
- **No cults.** Living magic annihilates black energy on contact, so cultists
  physically cannot get a footing here.
- **In micro-combat, a mage squad is a boss fight.** On the macro map it is ten
  dots against your hundred soldiers; in the subworld one of them deletes half
  your army in five seconds with a single high-tier cast. You do not brawl a
  mage squad, you **hunt** it: isolate, kite, assassinate, or bring black
  energy — the one thing that annihilates them.

### 3.2 The Empire of Light — the clay giant

*Registry: `empire`, `Temperament::Lawful`. Allied to `lower_magica`.*

The **largest and strongest** state in the world, and the one that suffers most
from its own size. Not strong in magic. Not strong in technology. Strong in
**mass**: many cities, huge armies, and discipline.

- **Aesthetic: an eastern caliphate.** Sterile, wealthy, gold and white walls
  and mosaic, immaculate order — a deliberate visual contrast with dark forests
  and squalid villages.
- **The Thirteen Great Eunuchs (13 великих евнухов)** secretly rule it. The
  Emperor is a **puppet**.
- **The Religion of Light is a forgery.** The eunuchs built it to hold power.
  Magic is not dark sorcery — it is the living force of nature — but the faith
  declares it heresy, and **the paladins do not know**.
- **The magebane paladins (паладины-магоборцы)** are the tragedy of the
  setting: sincere, brave, genuinely protective of the weak and the innocent,
  carrying real ideals — and every mage they burn tears another hole in the
  living world, opening the vacuum that black energy fills. They roam **outside**
  imperial borders, intervening in other people's wars and carrying the faith.
  Their magebane shields and auras are useless against **a heavy blunt object
  in a strong man's hands**.
- **The eye-golems (Големы-глаза)** hold the inside: silent watching mechanisms
  that suppress and smother magic at birth. A police state expressed as a unit.
  They are also the world's **systemic answer to an archmage**: a level-100+
  player who flies in to Armageddon an imperial city finds his mana smothered
  and a phalanx closing. — *golems and paladin roaming are `NOT BUILT`.*
- **Fighting the Empire feels like a rolling armoured wall.** No mad magic;
  numbers, armour, and discipline. A fair head-on push gets you flattened.

### 3.3 The Republic of Timaert — technocratic pragmatism

*Registry: `timaert`, `Temperament::Mercantile`. Allied to `northern_magica`,
at war with `cults`.*

The **second largest** state, across the eastern sea. **No ideology** — no dead
gods, no ancient traditions, no faith. Locomotives, cast cannon, manufacture,
logistics, capital. Magic is weak here precisely because progress displaces
nature, and they do not care: they answer magic with **firepower, economy and
supply**.

- **Fighting Timaert is the Vietnam syndrome of fantasy.** You bring your
  beautiful knights and your peasant pikes, and you die on the approach to
  musket volleys and artillery. Your army breaks before it closes. To fight
  them you must change how you think: flanks, terrain, forest, cover, or close
  the distance fast.
- The Republic's **galleons** are floating fortresses; a broadside of fifty guns
  ends any adventurer who attacks one out of greed. — *ships and naval combat
  are `NOT BUILT`.*

### 3.4 The Barbarian Kingdoms — the procedural wildcard

*Registry: `barbarian_north/south/west/east`, `Temperament::Savage`.*

The lands the Peasant King **freed from the mages**, now held by feudal warlord
kings. These are **procedural defaults: different every playthrough**, each
generated with its own pluses and minuses — an aggressive chief with strong
infantry and a starving population here, rich farmers with no defence there.

This is the replayability region and the intrigue region: ally them, unite them,
or set them on each other. In battle they are **Russian roulette** — a pathetic
rabble with pitchforks, or a fanatical heavy infantry that fears nothing and
chews your soldiers apart.

- The Peasant King **roams here and in the Magikas** (§5).
- Few cults: the land was magical until recently, and there is no imperial
  vacuum yet.
- *Per-kingdom procedural strengths/weaknesses are `NOT BUILT` — kingdoms today
  differ by placement, colour and lineage, not by generated economic/military
  profile.*

### 3.5 The Cults — the hidden pressure system

*Registry: `cults`, `Temperament::Cultist` (fights `Magical` — witch hunts cut
both ways), player reputation seed −10.*

At the **start of the game the cults are at their weakest in a thousand years**,
because the Sacrilegist destroyed nearly all black artifacts and cults before he
vanished. From year 0 onward **their influence grows** — and the player is
**never told**. No "cult threat +15%" popup, no meter, no notification. The
player is expected to *notice the phenomenon*: three in-game years ago this
region was quiet, and now there are figures in strange robes on the roads and
the wilds have got worse.

Distribution follows the annihilation law:

| Region | Cult presence | Why |
|---|---|---|
| The Magikas | **None** | Living magic annihilates black energy |
| Barbarian lands | Very few | Recently magical, no vacuum yet |
| Empire of Light | Many | Paladins burn magic → vacuum → nothing opposes the traces |
| The Wilds | Most | No power cleans them; old traces linger |

The player may do anything with them: clear their sites like dungeons and loot
**black artifacts**, befriend them, use them, or aim them at somebody else's
army and watch from a hill. — *cult growth over time is `NOT BUILT`; `cults`
exists as a faction row.*

**Black artifacts are the world's cult engine, and the player's hand is on it.**
An artifact is never inert: **wherever it is, it seeds cults**. What the player
changes is only *where*.

| State of the artifact | What it does |
|---|---|
| **Left in the world** (undestroyed, in its landmark) | Its **landmark spreads cults locally** — a fixed, regional source, ticking away wherever the artifact lies |
| **Collected by the player** | **Ticks cult spawns across the whole world.** The source stops being a place and becomes *you* — you carry it everywhere, and cultists rise everywhere |
| **Destroyed** | Out of the world, **and the ground is cleaned immediately** — the whisper vanishes from the surrounding cells at once and the ticks stop with it. The Sacrilegist's own thousand-year policy, re-enacted by hand |
| **Given to a witch** (Nefesh's standing turn-in) | Out of the world, into hands that owe nothing to the dead gods (§3.6) |

**The whisper is a field on the map, not a global counter.** Destruction
cleansing "the area" is what tells you the shape of the system: an artifact
projects shadow onto **the cells around it**, so the world's darkness is a map
you could draw — and a player who destroys one watches a patch of the world come
back. That is also what makes the fairy-tale route playable rather than
theoretical: cleansing is **local, immediate and visible**, so it can be done a
region at a time by someone who never learns why it works.

**Picking one up is waking it up.** A collected artifact is not a trophy in a
bag; it is a source the player has taken personal custody of. Clear a cult
dungeon and pocket the loot and you have not cleaned the world — you have
converted a fixed local source into a mobile global one, and every instinct a
player brings from other games scores that as heroism.

**At dozens of artifacts the whisper becomes literal.** A hoarder does not just
raise a statistic; **reality bends around him**: the whisper of the dead gods,
special anomalies, demons, and events that follow *him* rather than a place.
The archetype builds itself — the man walking the world with a sack of dead-god
residue, wondering why the world keeps going strange wherever he sleeps.

**It does NOT accelerate the ending.** (Owner's ruling — an earlier draft of
this file had it feeding the ten-year prophecy; it does not.) The prophecy is
its own clock. This is the **shadows-and-whisper mechanic**, and what it moves
is not the deadline but **the genre of the world you live in**:

| Player's artifact policy | The world becomes |
|---|---|
| **Destroy them, cleanse the world** | Broadly a **magic-fantasy fairy tale** — the shadows recede, the world stays legible |
| **Hoard them / leave them lying** | **Dark fantasy** — shadows spread, anomalies, demons, cult growth, whisper events |

This is the deepest expression of §9's thesis in the world simulation: the
player is not choosing an alignment or a karma bar, he is **choosing which kind
of story his ten years are made of** — and nothing ever tells him he is choosing.

It also closes the loop onto §6: the Warrior who takes up a black artifact out
of desperation, exactly as the Peasant King did, is personally pulling his own
world toward dark fantasy. No morality bar, no warning — only a consequence he
can measure years later if he was watching. — `NOT BUILT`.

### 3.6 The Witches — outside the system

*`NOT BUILT` as a faction row (there is a `Witch` NPC type and a witch sfx).*

The witches are **completely insane** and **exist independently of the dead
gods and of magic alike**. They are the one thing in the setting that does not
belong to the two-force metaphysics — a foreign body in their own world.

**They are presented with no explanation, on purpose. They simply are.** No
origin, no hierarchy, no council, no ranking — the game never accounts for them
and never will. Their names come from the **kabbalistic ladder** (Nefesh, Hokma,
Sepheret…), and that is deliberate: a naming system the player can *feel* has a
logic behind it, which the game refuses to confirm.

**They are also openly fanservice** — anime witches, each with her own hair
colour and temperament. The register is the setting's own: gross, absurd,
memorable, and played straight next to the dead gods.

### THE LAW OF THE WITCHES: a domain, whole, without morality

**A witch does not embody a virtue or a vice. She embodies a DOMAIN — and she
gets everything in it at once, unsorted.** This is the rule that generates all
four of them and the one to extend by; it is what keeps them from collapsing
into "the nice one and the evil one".

Worked example, the Witch of Death: she is **merciless** — and **just**, because
before death everyone is equal. She is **cold** — and **polite**. She is
**patient**, because death comes in its own time and never on a whim. Not a
balance of good and bad traits: the *same* trait read from two sides, because
death genuinely is all of those things.

Curiosity likewise is not "the love of knowledge". It is **also** sticking your
nose into other people's business, gossip, and hoarding garbage information —
because a mind that finds everything interesting finds *everything* interesting.

Nothing about them is morally scored, ever. They are not the good witches or the
bad witches; they are what their subject is, in full.

### The four

Hierarchy-free. Each is a colour and a temperament first, a quest generator
second. The Hebrew names are read off the domains, kabbalah-style:

| Witch | Domain | Hair · look | Temperament | Quests she gives |
|---|---|---|---|---|
| **Hokma** (Хокма) — *חכמה, Wisdom: the first spark of knowing, insight before it becomes understanding* | **Knowledge & curiosity** — and nosiness, gossip, junk information. Interested in absolutely everything | **Red** | **Tsundere.** The most sociable of the four. **She cannot lie** — she keeps attempting small domestic lies and, if you press her at all, tells you the whole truth exactly as it is | **Find a thing, go to a place** — above all **subworld exploration quests** |
| **Sepheret** (Сеферет) — *from ספר, to count, to number: the same root the sefirot themselves come from. She keeps the count, and the count is the same for everyone* | **Death & oblivion** — merciless and just, cold and courteous, patient | **Black, thick** — pale | **The most polite and calm of the four; respectful** | **Contracts on bosses and named NPCs** |
| **Nefesh** (Нефеш) — *נפש, the vital soul: the lowest rung, the part of a person that hungers* | **Greed, vanity, ego, hunger for attention.** Believes herself the most beautiful witch alive and has permanent trouble with her own self-worth; it is never enough. Also genuinely **thrifty** | **Blonde** | Acquisitive — and **she will never quarrel with the player, because he is HER apostle** | **Takes black artifacts, over and over** — the standing sink of §3.5 |
| **Temurah** (Темура) — *תמורה, permutation: the kabbalistic art of substituting one letter for another. Her name is a generator* | **Change** — attaches to nothing, remembers nothing, wants one thing today and another tomorrow | **Green** — short | **The maddest of the four**, forgets everything constantly | **The most random and absurd quests in the game** |

Three things fall out of this roster and are worth stating:

- **The opening scene has two witches in it, not one.** You are rescued by the
  red one while the green one talks in the background — the player's first
  contact with the setting is two insane women having a conversation over his
  half-dead body, and one of them will not remember it. That establishes the
  witches' register in a single scene with no explanation at all, which is
  exactly the §10.1 discipline.
- **Hokma's honesty is an information channel.** A world that explains nothing
  (§10.1) has exactly one being in it who *cannot* hold anything back if you
  keep asking. That is not a flaw in the discipline, it is the pressure valve —
  and it costs the player nothing but the nerve to interrogate a witch.
- **Nefesh is a system, not a joke.** "Give it to a witch" (§3.5) is *her*,
  permanently open, no quest gate, and she never turns on you — the standing
  alternative to hoarding for a player who wants the artifact gone but will not
  destroy it. Her greed is the world's disposal service.

> **⚠ Canon amendment, needs the owner's yes.** The name **Nefesh** here sits on
> the blonde greed witch, because *nefesh* is the appetitive soul — the rung that
> hungers — and that is her domain exactly. The earlier transcript gave the name
> **Nefesh** to the witch who rescues the player in the prologue; under this
> mapping the rescuer is **Hokma**, the red one. Either the name moves (this
> file's assumption) or the meanings do; **references to "Nefesh's route" in §4
> and §5 mean the prologue rescuer, whatever she ends up being called.**
> Runner-up name for the green witch if Temurah is not wanted: **Ruach**
> (רוח, wind/breath — the soul-rung directly above Nefesh, and a thing that
> blows wherever it likes).

- **How you meet them:** like the Peasant King, they are **encountered out in
  the world** — but **more randomly and less systematically** than he is. He
  roams a defined region on his own errand; they turn up.
- **What they give you:** each witch hands out **procedural quests in her own
  theme**. One quest generator per witch, seeded by her theme — the same
  procedural machinery the settlements use ([quests.md](quests.md)), not a
  hand-authored line per witch.
- **Sepheret (Сеферет)** is a hard fight, and the fiction explicitly rates her
  far below the Sacrilegist. *Which of the four she is — and which one carries
  the name Hokma (Хокма) — is open (§12).*
- **The Champion / Apostle route:** complete all their quests, become their
  apostle — and they simply **leave this world**. No "you saved the world and
  became king". A dry statement that you were an instrument of something a
  human mind cannot follow.
- **The Rebellion route:** rise against the witches and **the demon gates open,
  in a flatly unfair way**. That is a crisis you survive only if you are levelled,
  prepared and lucky. The game does not scale down for you.
- **Black artifacts have a witch sink:** handing one to a witch is a quest
  turn-in, and one of the three fates of an artifact (§3.5).

### 3.7 The unruled

- **Free Folk** (`freefolk`) — towns and holdings that answer to no crown; what
  a place *without* an owner is, so that "unowned" never has to be spelled
  "imperial".
- **Bandits** (`bandits`) — the world's first teacher: the prologue's attackers.
- **Wildlife** (`wildlife`), **Demons** (`demons`) — indifferent, and the
  abyss.
- **Your Realm** (`player`) — the player's own faction row, from a household to
  a late-game kingdom.

## 4. The player's story

**Year 0.** You are nobody — an inhabitant. Bandits attack you in a forest, rob
you, and leave you to die. **Nefesh finds you.** In parallel, elsewhere on the
macro map, the Peasant King is walking his own path *as an AI actor*, freeing
peasants without any reference to you.

**Where you start is a choice — of three, not four:**

| Start | What it is | Plays best as |
|---|---|---|
| **The Magikas** | Dwindling high kingdoms, deep coffers, no cults at all | **Mage** |
| **The Empire of Light** | Sterile, rich, policed; magic is heresy; cults growing under the floorboards | **Trader / paladin / cleric** |
| **The Barbarian Kingdoms** | The lands the Peasant King freed; procedural, volatile, different every run | **Warrior / rogue** |

**The choice is not just a position on the map.** It sets:

- **your reputation with every faction** — the registry's `playerReputation`
  column is a per-faction seed, so a start is a whole row of standings, not one
  number (`create_factions`, `src/macro/faction.h`). **The Empire does not like
  mages**, and that is a standing you begin with, not one you earn;
- **your starting items**;
- **your starting spells — and only the Magika start gives any.** Everywhere
  else you begin with no magic at all. This is the harshest and best line in
  the whole start system: it means the mage's misery of §7 is *chosen* at the
  new-game screen, and it means a player who starts in the Empire and wants
  magic has to go and take it from a spire in the wilds, like everyone did
  before him.

"Plays best as" is a **consequence, never a class lock** (§6): nothing stops a
mage starting among the barbarians with no spells at all, and that is a
legitimate, brutal run. The lore does the balancing here; no difficulty setting
is needed.

### 4.1 Paladin and cleric — the Empire's two builds

They are real archetypes (owner's ruling), and narrow ones:

- **Paladin — the specialised magebane.** Not a holy warrior generalist: his
  whole identity is *anti-magic*, the player-side version of §3.2's roaming
  paladins. He is the answer to the archmage the same way the eye-golems are.
- **Cleric — the healer.** The role the party/army side of the game has no
  answer for today.

**The open problem, stated plainly:** both traditionally need magic, and this
world spent a thousand years making magic rare, heretical and spire-locked. Give
them spells and you hand the Empire's own faithful the thing the Empire burns
people for; build them a second, parallel "divine" system and **you have doubled
the systems** — two spell registries, two resources, two UIs, two balance
problems. The owner named exactly this trap («либо делать для них магию что
плохо, либо альтернативую систему — что множит системы»).

**Proposal (not a ruling — for the owner to accept or kill):** take neither
horn. Use the systems that already exist, and let the lore do the work.

- **The cleric casts ordinary spells from the ONE spell registry** — same
  `SpellDef` rows, same mana, same cooldowns ([spells.md](spells.md)) — but
  acquires them from **the Church of Light instead of a spire**, and the UI
  calls them **miracles**. Zero new systems: one new *acquisition channel*, one
  label.
- **The joke writes itself, and it is the setting's central joke.** The Religion
  of Light is a forgery (§3.2). So the cleric's miracles **are magic** — the
  precise heresy his own order burns villages for — laundered through a name. A
  cleric who studies far enough is in a position to work that out, and the game
  never has to say it: he can simply notice that his miracle and the heretic's
  spell do the same thing to the same wound.
- **The paladin needs no magic at all** — he needs *anti*-magic, which is
  suppression, resistance and dispel: exactly what **perks and equipment**
  already express ([rpg.md](rpg.md): attributes add, skills multiply). Imperial
  issue relics and magebane perks, no new resource.
- **WIS is their attribute**, and it currently anchors no archetype at all in
  [design.md](design.md) §14.5 — so this fills a real hole rather than carving a
  new one.

If that holds, the Empire start's "trader / paladin / cleric" costs **one
acquisition channel and one word in the UI**, and buys the setting's sharpest
irony as a playable build. — `NOT BUILT`.

**The Republic of Timaert is deliberately not a starting region.** It is
**isolated on purpose** — kept across the sea as a destination: adventure and
exotica you *travel to*, so the first sight of a locomotive, a musket volley and
a galleon lands as a discovery rather than as the furniture of your home town.
The game is named after the one place you cannot begin in.

*Shipping-build note: the start region choice is `NOT BUILT`. New Game anchors
the player at the first generated city (`citiesFlat[0]` in `src/app/main.cpp`),
and Custom New Game exposes map size / seed / city count / layer parameters —
no region pick.*

**Ten years.** Three outcomes:

1. **The Witch route (classic).** Serve Nefesh's line to its end, become the
   witches' apostle — and they leave the world without you.
2. **The Rebellion route.** Turn on the witches; the demon gates open unfairly
   early and unfairly hard; survive it if you can.
3. **Game over by default.** Do nothing about any of it. Ten years pass, the
   barbarian kings betray the Peasant King, in despair he spears an infant, the
   shadows of the dead gods condense, and the world is eaten.

**The third ending is not a failure state.** This is the load-bearing design
claim of the whole project: a player who spent ten years hauling grain to the
city and buying iron at a good price, married a local, raised two children and
watched the red sky from a rooftop with a mug of ale, has played a **complete
story**, and nobody can tell him otherwise. The finale does not devalue the
process.

**After ten years: free play.** The world has had its climax; the clock stops
being the constraint. Collect every spell, pass 100+, close every secret boss on
one character, paint the map in your colour, or empty it.

## 5. The named figures — and why they are unkillable *in fiction*

Three historical titans, each a **living, reachable endgame encounter** and
none of them attached to the main quest. The player finds them because he went
looking.

### The Sacrilegist (Святотатец) — the absent engine

Gone for a thousand years, present in everything: the ruin of the Empire's army,
the founding and fragmentation of Magika, the near-extinction of the cults. He
is the ghost in the foundation.

- **Where:** a side quest chain leads to **a place that does not exist**,
  outside time.
- **Entry condition (natural, not a class lock):** you have visited **every
  spire**. He does not open a door for a passing swordsman.
- **The fight:** infinite mana, every spell in existence, cast at you in
  cluster barrages. Sepheret is not in the same weight class.
- **The meaning:** you wanted to be a god of magic. Here is the god of magic.
  Prove you surpassed the man who made Magika.

### The Peasant King (Царь-крестьянин) — the walking myth

He **roams the map for real**, through the barbarian lands and the Magikas, an
optional elite boss you can simply run into and challenge.

- **The black spear is a black artifact** — confirmed canon, and he does not
  know it. Which makes him, by §3.5's own rule, **a walking source**: the most
  famous artifact in the world, carried across the map for decades by a man
  liberating peasants with it. Whatever the artifact-borne shadow effects are,
  he has been trailing them behind him this whole time — and that is a free
  explanation for anything strange in the lands he has walked.
- **Immune to magic**, like the dragon. An archmage's Firewall and Armageddon
  scatter off the black spear and the will of the dead gods.
- **He cannot be permanently killed** — you beat him, you wound him, you take
  loot from him, and later he respawns somewhere, recovered. **This is not an
  immortal-NPC script**; it is causality, the will of the dead gods holding him
  in the world because he is inseparable from the fate of this land. The
  distinction matters and must be legible to the player: he is not a
  blue-shielded quest marker, he is a metaphysical fact you lost an argument
  with.
- **The meaning:** the test for STR / END / melee timing. Beat magic immunity in
  the mud, by hand.

### The Captain of the Republic of Timaert — the logistics mirror

You will **never even meet him without your own galleon**. He does not come
ashore to trade sword blows; he fights at sea, where calibre, manoeuvre, wind
and the powder in your hold decide it. Fifty guns in one broadside turn a
presumptuous adventurer's ship into splinters and remind the world that
**industry is not a joke**.

### The Thirteen Eunuchs and the puppet Emperor

The intrigue layer of the Empire. Recognisable at a glance as a political
archetype, absurd and menacing in one breath.

**They are thirteen real NPCs — plot lords with bodies — and they live in the
imperial capital.** Not an abstract political layer: if "you can kill anyone" is
true, it has to be true of the people who actually run the world. Two
approaches, and the choice between them is **open**:

- **Fight through** the capital's guard into a local dungeon interior and take
  them as a **boss fight** — the palace as a dungeon, thirteen deep.
- **Pick them off one at a time**, gradually, by whatever means an assassin, a
  magnate or an archmage has.

*Open: which one, or both. The second is the more interesting one for this game
— thirteen separate deaths mean thirteen separate reactions from the Empire, and
a slow visible unravelling instead of one arena. It also asks a question that
needs an answer: **what does the Empire do as its eunuchs die?** — succession,
purge, civil war, a real emperor waking up, or the crusade slackening and the
cults surging in the vacuum.* — `NOT BUILT`.

### Nefesh — the patron you may kill in the prologue

She is level 100 and she saves you in the opening scene. **You can beat her** —
not by an exploit, but by knowing her weakness from a previous playthrough. And
this is **not a secret gimmick: it is a plot branch**. Killing her opens a
questline whose reward is **carrying this character into new games** — a
diegetic New Game+.

- The break of the fourth wall is the point: the game recognises that the person
  at the keyboard is someone who **already knows how this ends**. The scenario
  had a guide; you removed her; causality begins to come apart; you go looking
  behind the world's scenery, and you leave the cycle. Your hero becomes,
  effectively, **a second Sacrilegist** — a wanderer across the game's own
  repetitions, arriving in each fresh world that is still trying to run by the
  rules. — `NOT BUILT`.

## 6. The fractal law — a build is a biography

**The player's build re-walks the life of one of the world's founding figures,
and he usually does not notice until the end.** This is the single most
important lore↔mechanics identity in the project.

| Build | Becomes | The shape of the life |
|---|---|---|
| **Mage** (INT / WIL / Spellcraft) | **The Sacrilegist** | Contemptuous of crowns, taxes and coin. Teleports across the world, burns what stands in his way, does not descend to loot the ruins — the gold is worthless to him and the armour too heavy for his STR. Builds an elitist tower deaf to mortals. |
| **Warrior** (STR / VIT / END) | **The Peasant King** | Cannot win by mana or by capital. Runs out of road, and takes up a **black artifact out of desperation** — exactly as the King took up the spear whose nature he never understood. Slowly becomes the calamity he feared. |
| **Trader / Magnate** (CHA / Travel) | **The Captain of Timaert** | Reads the world as logistics, markets, pragmatism and gunpowder. Hires arquebusiers, buys the galleon, reproduces the most technological faction's ideology on his own knee. |

The **mirror bosses** of §5 are the final exams of each path — you fight the
perfect embodiment of the road you chose.

| Boss | Who can realistically answer | The test |
|---|---|---|
| The Sacrilegist | Archmage (all spires visited) | Survive a cluster barrage of every spell there is |
| The Peasant King | Warrior / non-mage | Break magic immunity in a contact brawl |
| The Captain | Admiral / Magnate (own galleon) | Out-shoot a technological flagship at sea |

**No artificial locks.** Nothing in the code says "you are a Warrior, the spire
is closed". A warrior may walk every spire — go find them in the deep forests,
fight through the mobs, open them one by one. The limits are **natural
consequences of the systems**, never prohibitions, and a level-100+ hybrid who
solves the cluster-spam problem gets his win honestly. After the ten years, in
free play, one character can close all three.

## 7. The classes are three different games

The asymmetry is **deliberate and correct**, because this is a single-player
game. It is not e-sport balance; nobody is owed symmetry.

**Early game.**

- **Mage — borderline unplayable, on purpose.** Spells are rare. They are
  *guaranteed* in the spires — and the spires stand in mountains and deep forest
  wilds, guarded (zone ≥ 5, see [zones.md](zones.md)). You start with **Magic
  Bolt** and one real advantage: you need no bow, no arrows, no musket, no
  powder — your ammunition is your own mana. You are a man with an infinite but
  very weak pistol. You grind goblins **within sprinting distance of the village
  guards**, sleep 12 hours in the tavern to refill mana, and repeat fifty times
  for one level. You cannot even run away from a bandit squad — no SPD, no END —
  so you reload. Finding Firewall is a quarter of the job: without WIL you
  cannot pay for the cast, and without INT it takes 2% of a mob's HP.
- **Warrior — the bum's snowball.** Club some feeble cultists in an alley, loot
  a procedural black artifact, use it to club a small lordly retinue, take their
  armour and sword, become a mini-boss. Progress through **plunder and nerve**.
- **Trader — the huckster's snowball.** Buy a lucky musket, become the terror of
  bandits, resell their loot, collect bounties, buy the galleon and the first
  ten mercenaries. Progress through **capital**.

**Late game.**

- **Mage — the snowball nobody else gets.** Teleport across the map instantly,
  which is also an *economic* superpower: no CHA needed, just read where there is
  a surplus and where a shortage and arbitrage it in one second while a caravan
  takes weeks and pays guards. Then Armageddon a city and fly on without
  descending to the ruins.
- **Warrior — the masochist's joy.** Never levels a city, never beats even a
  middling imperial army. But he beats paladins' faces in without trouble, and
  he is **the only one who can kill the magic-immune dragon**. The heavy hammer
  of the world; every win is chewed out dry, on timing, stubbornness and hit
  points.
- **Magnate — CHA past 100 inverts the sign.** Hiring cost falls linearly (−1%
  per point of CHA) and past 100 **the mercenaries pay you** for the honour of
  serving under your banner. Found the break-even point, broke the economy, and
  in-fiction it is even correct: people pay dues to stand under a legend's
  protection. — *the CHA-100 sign flip is `NOT BUILT` as a stated rule; confirm
  against `microcombat.md` recruitment.*

**The terrain proves the asymmetry better than any stat screen.** Three builds,
one situation — stranded in the mountains ([macroworld.md](macroworld.md) SP
penalty, [rpg.md](rpg.md) `travel` / `athletics`):

- **Mage:** out of mana. Cannot fly, cannot teleport. Restoring mana needs sleep
  and food; there is no food up here; without mana he cannot kill a mountain
  goat. Walking out burns the stamina he never levelled, on the terrain with the
  worst SP penalty in the game. The demigod is trapped in the golden cage of his
  own power, praying no barbarian squad walks past.
- **Trader:** pitches camp. He always has rations, tents and mules. The mountain
  is one more waypoint on a logistics route.
- **Warrior:** keeps walking. Colossal SP regen and a body built for load; he
  simply hikes to the nearest village and does not notice there was a penalty.

The distance between A and B stops being filler and becomes the adventure.

## 8. The endings

| Ending | Trigger | Nature |
|---|---|---|
| **Apostle of the Witches** | Complete Nefesh's line | They leave the world. You were an instrument. |
| **Rebellion** | Turn on the witches | Demon gates open unfairly; survive on preparation and luck |
| **The Prophecy (default)** | Ten years elapse | The King is betrayed, spears the infant, shadows condense, demons take the world |
| **The Empty Land / Dead God** | Genocide run | See below — `NOT BUILT` |
| **Beyond the Cycle** | Kill Nefesh in the prologue → the "Outside Time" branch | Carry the character into new games — diegetic NG+ — `NOT BUILT` |

**Late-game sandbox is unconditional.** Every character in the world can be
killed, every city can be destroyed. The world is not held up by immortal NPCs
with scripted dialogue; it is built to digest any chaos the player commits. Kill
a key figure and the story does not break — it proceeds down its darkest
available branch.

**The Empty Land (genocide) ending.** When there is no living NPC and no intact
city left, the ticking world **stops**: there are no factions left to raise
events. The proposed finale is cold, not moralising — the opposite of Undertale.
Candidate text, kept here as the register to hit:

> «Пророчество не сбылось. Демонам было просто незачем приходить — в этом мире
> больше нечего было поглощать. Вы искали силу, вызов или ответы, но нашли лишь
> тишину. Мёртвым богам больше никто не молится. Теперь единственный мёртвый бог
> этого мира — это вы.»

with the closing system line:

> `[ На карте больше нет разума. Причинность разрушена. Нажмите ESC, чтобы покинуть этот мир. ]`

Two further candidates recorded from the transcript, neither ruled on:
the genocide save becoming **a rare anomalous encounter in future playthroughs**
(a silent shadow of your previous character wandering burnt wastes), and the
**dead gods finally falling silent** once there is nobody left to whisper to.

## 9. Black energy as meta-design — the playable cult

The Black Energy path is not a third magic school. It is a **psychological
experiment on the player**, and it is the sharpest idea in the project.

- **Of the nine attributes** — STR / VIT / END (warrior), WIL / INT / WIS
  (mage), SPD / CHA / LCK (misc), matching `AttributeId` in
  `src/macro/attributes.h` — black energy is touched by **LCK alone**, and even
  then **indirectly and unstated**. Logically airtight: traces of dead gods owe
  nothing to Intelligence or Willpower; they are outside this world's physics.
- **All-or-nothing abilities**, and **deliberately many of them non-obvious**.
  Some operate at a level the UI never mentions — one changes projectile
  **spread** in the code. To know it works, the player must literally keep
  statistics on his own fights.
- **One skill does nothing at all.** Its description vaguely, convincingly
  implies that it does, and the player takes it because the description
  persuades him.

**And it is not a con.** The design claim: a player who takes it starts
*playing as though he believes it* — bolder here, more cautious there, risking
where he would have backed off — and that shifted behaviour pattern is, purely
statistically, sometimes genuinely advantageous. The skill therefore **does**
work — not because it adds damage in the code, but because **it changed the
player**. Which is precisely the fiction's own claim about the dead gods: they
do not intervene, their whisper changes the mind of whoever listens.

The three paths, stated as epistemologies:

- **Mage** — dry calculation and knowledge of the rules (WIL / INT / WIS).
- **Warrior** — legible physics and stats (STR / VIT / END).
- **Black energy** — superstition, style, LARP and mystique. You agree to play
  by rules that are not explained to you, look for patterns that may not be
  there, and get an experience that resembles nothing else.

**The design target above all others:** the player should end a playthrough
unable to answer *"was that the game, or was that me deciding to play it that
way?"*

**Rules for anyone who touches this system.**

1. Never document the empty perk in-game, in a tooltip, or in a patch note.
2. Never "fix" it by giving it a real bonus; it is doing its job.
3. Never add a UI meter for cult influence or black-energy attunement. The
   player observes the phenomenon or he does not.

## 10. The world reads its own history

Atmosphere through systems, never through exposition text. The player walks the
map and knows without a single prompt where the black spear has passed and where
the mages still rule.

| Place | Look | Because |
|---|---|---|
| **Under the mages** | Hunched huts, dry fields, sullen sprites, guards in gaudy armour standing by empty granaries | The surplus goes to mana and to wizards' ambitions |
| **Freed villages** | Fair-day noise, better buildings, a turning mill, people moving briskly, smoke from the tavern | The surplus stays with the people who work the land |
| **Empire of Light** | Sterile, rich, gold and white, caliphate order | Mass, taxation and total control |

This is **not decoration** — it is the visible output of the economy simulation
([economy.md](economy.md), [macrosim.md](macrosim.md)). The owner's evidence
that this reads correctly: a 70-year-old relative who has never touched a
computer looked at the screen and said *"ah, the peasants are working in the
fields, they've run to the fields"*. Archetypal legibility is the visual
standard of this project. — *the liberation/oppression prosperity contrast is
`NOT BUILT`.*

### 10.1 Delivery — how much is ever said out loud

**The lore is never chewed for the player.** Four channels carry it, in
descending order of how much they say:

| Channel | Status | Role |
|---|---|---|
| **Intro comic** | in the build | The one place the setting is stated directly — and only enough to start you |
| **Dialogue** | in the build | Characters speak from inside their own beliefs, including the wrong ones (a paladin is sincere) |
| **Quests** | in the build | What a faction wants, revealed by what it asks you to do |
| **Events** | in the build | The world changing while you look elsewhere |

Everything above those is carried by **the world, its characters, its events
and its atmosphere** — never by exposition. The metaphysics in §1 is the clearest
case: the annihilation law, the forged Religion of Light, the true nature of the
black spear and the growth of the cults are things the player is **allowed to
work out**, and a player who never works them out has still played the game
correctly.

This is the same discipline as §9's rules, applied to writing rather than to
mechanics: **the game states nothing it can instead demonstrate.**

**Language.** The original is **English** — all in-game text, names and content
tables are authored in English, and the code and docs already are.
**Russian localisation comes later**, from the English original.

The canonical English names (owner's rulings):

| Русский | English — canonical |
|---|---|
| Святотатец | **the Sacrilegist** |
| Царь-крестьянин | the Peasant King |
| 13 великих евнухов | the Thirteen Great Eunuchs |
| Империя Света | the Empire of Light |
| Чёрная энергия / Чёрное копьё | black energy / the black spear |
| Магия | Magic (the living force of nature) |
| Причинность | Causality |

*Sacrilegist* over *the Sacrilege* or *the Blasphemer*: it names him as a
**practitioner** — sacrilege as a vocation he worked at for a thousand years,
not an act he once committed — and it keeps the faint absurdity of a man titled
like a member of a profession. Use this spelling everywhere; do not reintroduce
variants.

## 11. Lore ↔ code parity ledger

**Shipped and consistent with the fiction:**

- The faction registry (`src/macro/faction.h`) carries every realm as an
  ordinary row, with the temperaments the lore implies: Magical, Lawful,
  Mercantile, Savage, Outlaw, Abyssal, Cultist, Feral.
- Cults fight the Magical temperament in the relation matrix — the witch-hunt
  cuts both ways — and `timaert` is at war with `cults` by authored override.
- `freefolk` means "answers to no crown", so no ownerless town falls to the
  Empire by accident.
- Nine attributes exactly as the fiction splits them (3 warrior / 3 mage /
  3 misc), with LCK present for the black-energy path.
- Spires gated on danger zone ≥ 5 ([zones.md](zones.md)) — the mage's spells
  really are out in the guarded wilds.
- Ten-year plot horizon and level ~100 late plot ([design.md](design.md) §17.6).
- The world runs without the player: macro NPCs, caravans, economy, kingdoms
  ([macrosim.md](macrosim.md)).
- Possession — the player is a movable flag on an ordinary body
  ([possession.md](possession.md)) — is the mechanical seed of "kill anyone,
  become anyone".

**Fiction with no system yet (`NOT BUILT`), roughly in order of leverage:**

1. **Cult influence growth over time**, per region, silent (§3.5).
2. **Black artifacts as the cult source** (§3.5): an artifact left in the world
   makes its landmark a *local* cult source; an artifact **carried by the
   player** turns cult spawning *global*; at dozens held, whisper effects —
   anomalies, demons, events — follow the player himself. Destroying it, or
   handing it to the Witch of Greed, removes it. This IS mechanism 1's input —
   build them together or neither makes sense. **Does not touch the ten-year
   clock.**
3. **The Peasant King as a roaming, respawning, magic-immune elite NPC** (§5).
4. **Named figures at all** — the Sacrilegist, Nefesh, Sepheret, the Captain, the
   Thirteen Eunuchs; there is a `Witch` NPC *type*, no persons.
5. **The four witches** — encounters + one procedural quest generator each,
   themed by temperament (§3.6); the Witch of Greed's standing artifact
   turn-in; the two-witch opening scene.
6. **The prologue** — bandit ambush, left for dead, Nefesh's rescue.
7. **The start-region choice** — Empire / Magika / Barbarian, Timaert
   deliberately excluded (§4): a full row of faction reputations (the Empire
   already disliking mages), an item kit, and **starting spells for the Magika
   start only**. Today the player is anchored at `citiesFlat[0]` with no choice
   at all.
8. **The ten-year clock and its endings**, including the default doom.
9. **Prosperity contrast** freed vs mage-ruled villages (§10).
10. **The Thirteen Eunuchs as thirteen killable bodies in the capital**, and the
    Empire's reaction to each death (§5).
11. **Eye-golems, roaming magebane paladins**, Empire police-state units.
12. **Line infantry, artillery, ships, naval combat**, the Timaert galleon.
13. **Procedural per-kingdom profiles** for the barbarian realms.
14. **Black-energy skills**, including the opaque statistical ones and the empty
    one (§9).
15. **The magic-immune dragon.**
16. **The place that does not exist** and the Sacrilegist fight.
17. **The Nefesh-in-the-prologue branch and diegetic NG+.**

**Resolved against the code, no work needed:** *Southern Magika* is simply
another name for **Lower Magica** (owner's ruling) — the registry row
`lower_magica` stands, no new row, and the proxy-war framing attaches to the
existing `empire` ↔ `lower_magica` alliance override.

## 12. Open questions for the owner

Answered questions move up into the body of this file; these are the gaps that
would change what gets built.

1. **The name swap (§3.6).** Does **Nefesh** move onto the blonde greed witch —
   the appetitive soul, the rung that hungers — leaving **Hokma** as the red one
   who rescues you? And is **Temurah** the green one's name, or **Ruach**?
2. **The thresholds of the whisper** (§3.5). Destruction cleansing the local
   area is settled. What remains: how many carried artifacts before the
   anomalies start — a smooth ramp or a few visible steps? Do the effects follow
   the **carrier** or the **hoard** (a chest left at home)? And what is the
   radius of one artifact's shadow field — a few cells, a region?
3. **What does the Empire do as its eunuchs die** — succession, purge, civil
   war, a real emperor waking up, or a slackening crusade and a cult surge? And
   is it one palace dungeon, thirteen separate hunts, or both (§5)?
4. **The three start kits, authored.** Three reputation rows, one spell set
   (Magika only), three item kits. How negative is the Empire's opening
   standing toward a mage — cold, or actively hunted?
5. **Are paladin and cleric real build archetypes?** The Empire start names
   them, [design.md](design.md) §14.5 does not, and **WIS has no archetype of
   its own** in the current five. If they are real, what does a cleric do that a
   mage cannot — and does the forged Religion of Light still empower him?
6. **Which endings are reachable in the Early Access build**, and does the
   ten-year clock tick from the first EA build or arrive later?
7. **Does the Peasant King's spear stay unknown to him forever**, or is there a
   discovery beat the player can trigger (telling him, taking it, destroying
   it)? And does it emit the same shadow effects as any other artifact — i.e.
   are the barbarian lands quietly warped by the man who freed them?
8. **The genocide ending** — which of the three recorded variants is canon (cold
   text finale / save-as-encounter / dead gods fall silent)?

## Connections

Systems that carry the fiction: factions and relations
(`src/macro/faction.h`, [macroworld.md](macroworld.md)); the ten-year clock
([time.md](time.md)); the plot arc, endings and events
([progression.md](progression.md), [quests.md](quests.md)); the archetypes and
the nine attributes ([rpg.md](rpg.md), [design.md](design.md) §14.5); the mage's
spire grind ([zones.md](zones.md), [landmarks.md](landmarks.md),
[spells.md](spells.md)); the class-defining feel of each faction's army
([microcombat.md](microcombat.md), [macrosim.md](macrosim.md)); the visible
prosperity of a freed village ([economy.md](economy.md)); killing and becoming
anyone ([possession.md](possession.md), [monsters.md](monsters.md)).
