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

The fourth is the one that keeps the other three from reading as stock dark
fantasy: **somewhere across the sea there is a lit, elegant, industrial place
that refuses to participate** — the Republic of Timaert (§3.3). Dead gods and
cults are furniture anyone can buy; a steam republic sitting indifferent across
the water from the prophecy is not. The shadows read as shadows because of what
they are next to.

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

### 1.1 THE FIELD — the law as arithmetic

The two forces are **one signed per-cell world layer**, not two fields with an
interaction rule (owner's ruling):

```
  −127 ....................... 0 ....................... +127
  saturated MAGIC          neutral            saturated BLACK ENERGY
```

One `int8` per cell — 1 MiB for the whole 1024² world, and exactly the house
style for this project (discrete, integer, one byte where a byte will do). It
diffuses: sources pour in, the value spreads outward and saturates at the ends.

**And THE LAW of §1 stops being a rule the code has to enforce — it becomes the
addition operator.** Magic and black energy annihilate on contact because
−40 + 40 = 0. There is no annihilation routine, no special case, no
"if (magic && black)" anywhere. The deepest metaphysical claim in the setting is
a `+=`.

Everything else in this document that touches shadows or cults reads this one
number.

**The sources are asymmetric, and that asymmetry is the whole balance of the
world:**

| Side | Sources | Character |
|---|---|---|
| **Black energy (+)** | **Black artifacts and the ruins that hold them** — wherever they are kept, plus a trail behind a carrier (§3.5) | **Objects.** Inert, patient, everywhere, and they never stop |
| **Magic (−)** | **Beings, and very few of them:** whoever *destroys* a black artifact — the player, or any NPC who does it; plus **very strong mage squads, and dragons** | **Living, rare, mobile, mortal** |

*On dragons: they are sources the way a mountain is tall — a property, not a
politics. **Dragons are just dragons**: boss monsters, animals. They do not know,
do not care, and have no agenda. Nothing in this document should be read as
giving them one.*

Read that table again, because it is the tragedy of the setting in two rows.
Black energy is furniture: it accumulates by simply existing. Magic has to be
**someone**, and there are almost none of them left — which is precisely what
the Empire of Light has spent a thousand years arranging (§3.2).

**Destroying an artifact does not merely remove a source — it makes you one.**
That is the Sacrilegist's biography, expressed as a field mechanic: he crossed
the world for a thousand years destroying black artifacts, which means he *was*
a walking negative source, and the world was brighter for it while he existed.
A player who takes up that policy is not imitating him thematically (§6) — he is
running the same subroutine.

**ONE signed field, ruled** — not two kept in step. And the model is
**interference, with no bookkeeping**: a source only ever *adds* to the cells it
touches, the field then behaves as a field (diffuse, saturate), and the two
signs sum honestly wherever they meet. Nothing is "held": a mage squad marching
through dark country does not leave a timer or an aura behind it, it leaves the
value it deposited — which from that moment is indistinguishable from any other
field value and lives or dies by what flows into it next. **Both sides work by
exactly the same rule**; sources here, sources there, and diffusion.

**Your relationship to artifacts IS your signature in the field:**

| What you do with artifacts | Your sign | Reversible? |
|---|---|---|
| **Hold them** | Positive — you are a walking cult source (§3.5) | **Yes.** Destroy them or hand them to Nefesh and it stops |
| **Destroy them** | Negative — and **each one destroyed makes you a permanently stronger source of magic** | **No. Ever.** |

That asymmetry is the best thing in the whole system, and it is worth saying
out loud: **hoarding is a state, destruction is a fact.** A hoarder can put the
sack down; a destroyer has changed what he is, cumulatively, for good. Which
tells you exactly how bright the Sacrilegist must have been by the end — he
spent a thousand years destroying nearly every black artifact in the world, and
under this rule that made him the single brightest thing in it.

— `NOT BUILT`.

## 2. Timeline

| When | Event |
|---|---|
| An eternity ago | The gods create the world and **vanish**. Nothing of them remains but traces — black energy — and the black artifacts made from it. |
| −1000 years | **The Sacrilegist (Святотатец)**, the greatest mage who ever lived, founds the kingdom of **Magika**. He withdraws from ruling it almost at once and spends his reign teleporting across the world **destroying black artifacts and cults**. |
| −1000 years | War with the **Empire of Light**. He annihilates **half its army** — and then **truly disappears**. Not dies: disappears. He is the engine of the whole plot and is never once present in it. |
| −1000 → 0 | Magika **fragments** into several successor states — the "high kingdoms": few lands, deep coffers, strong mages, fading grandeur. Cults and black artifacts, nearly extinguished by the Sacrilegist, begin their slow thousand-year return. |
| Recent decades | The **Peasant King (Царь-крестьянин)** rises with a **black spear** — a black artifact whose nature *nobody knows, including him* — and frees the peasantry from the mages. The freed lands become the **Barbarian Kingdoms**, ruled by feudal warlords. |
| The east | The **Republic of Timaert**, across the sea, industrialises: the first locomotives, the first firearms, line infantry and galleons. The game is named after it. |
| **Year 0** | **The game begins.** The player is an ordinary inhabitant, attacked by bandits in a forest, robbed and **left to die**. **Hokma**, the red witch, saves him. |
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
the registry column are meant to stay in sync; see §12 for the current diffs.

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

**Why the game is named after it — the title is a promise, not a description**
(owner's ruling). Calling the game *The Thirteen Eunuchs* or *Empire of Light*
would be flat. Calling it **Timaert** and then putting Timaert **off at the edge
of the map**, reachable only by sea, turns the title into a question the player
carries for hours: *what is this place the game is named after?* He sails. He
arrives. **RAILWAYS.**

**And it is the aesthetic counterweight the setting needs.** Stated plainly by
the owner, and correct: **dark fantasy on its own is fairly generic** — dead
gods, cults, prophecy and doom are furniture anyone can buy. What makes this
world specific is that somewhere out east there is a place that is **elegant and
beautiful in its own right** — the **Arcanum** register: steam, iron, ledgers,
clean lines, progress that owes nothing to anybody's gods. Not comic relief, not
a tech tier: a whole other idea of what a world can be, sitting across the water
from the shadows, refusing to participate.

That contrast is the setting's real signature. The shadows read as shadows
*because* there is a lit, indifferent, industrial republic to compare them
against.

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

**The cults are an ordinary faction on an extraordinary trajectory.** They are
not an event system or a spawn table: they are the `cults` registry row, playing
the same game as everyone else — and what changes over the years is **how much
of them there is**.

| Era | What the cults are |
|---|---|
| **Game start** | Barely expressed. They **live and wander around their own landmarks** — the places where the artifacts are. Few, weak, and **largely passive and non-aggressive**: you can walk past one |
| **As the years pass** | **More cultists, more squads, and the squads themselves get stronger.** Nothing announces it; the roads simply have more strangers on them |
| **Late — many years in** | Where the whisper field has settled over a village or a city, the cults **take the settlement outright.** Whole cities change hands |

**A cult-held city keeps working — and stops being the same city.** It goes on
producing and trading; the economy does not switch off, so the map does not
develop holes. But **the fact of the capture changes many of the rules that
apply to it** — fact → rule again (§5). The recorded starting list, to be worked
out properly later:

- **the city itself changes in the subworld** — you walk into a different place
  than the one you remember;
- **sacrifices**;
- **strangenesses**;
- **cultist spawns** in and around it.

A city under the cults is a going concern with new laws, which is far more
unsettling than a ruin. — *the specifics are deliberately deferred.*

That last line is where the shadow field stops being atmosphere and becomes
politics: **the field is what makes a settlement takeable.** And it lands on
machinery that already exists — a settlement's owner is its `kingdomIdx`, and
changing an owner is a data change, not an engine change
(`src/macro/faction.h`). A city lost to the cults is the same operation as a
city lost to a rival crown.

The player may do anything with them: clear their sites like dungeons and loot
**black artifacts**, befriend them, use them, or aim them at somebody else's
army and watch from a hill. — *the growth curve and settlement capture are
`NOT BUILT`; `cults` exists as a faction row today.*

**The shadow field is the positive half of THE FIELD (§1.1).** The primary
object is not the artifact; it is the field — one signed per-cell world layer,
black energy on the plus side, magic on the minus. It belongs to the family this
engine already builds: danger zones ([zones.md](zones.md)) and the baked
night-glow field with occluded spread ([macro-lighting.md](macro-lighting.md)).
**Black artifacts only feed it, contextually.**

The rules of the field (owner's ruling):

- **It ramps smoothly, and it SATURATES.** A cell fills up to a maximum; once
  full, the excess **diffuses outward — very slowly**. No thresholds, no steps,
  no "level 3 corruption unlocked": the shadows simply thicken, and a player who
  is not paying attention will not be able to name the moment it happened.
- **THE CALIBRATION CONSTANT: unopposed, the cults swallow everything in about
  a hundred years.** That single number fixes the whole system's rate — living
  magic is the only thing pressing back (§1), so with nothing opposing them the
  field saturates the world in ~100 in-game years. This is the tuning target to
  build against, and it is a beautiful one, because **the game is ten years
  long**: a normal playthrough sees roughly **a tenth** of that pressure. The
  darkness is real, visible and moving, and it is nowhere near able to end the
  world on its own within your lifetime — *unless you feed it*.
- **A source is any place holding artifacts, universally** — a cult landmark
  that still has its own, a stash, a chest in a house the player bought. The
  rule is "where they are kept", so nothing needs a special case for the player.
- **A carrier leaves a trail.** Walk the world with artifacts on you and the
  field thickens along the path behind you. This is what makes the hoarder's
  archetype self-writing: the man wandering with a sack of dead-god residue,
  wondering why the world keeps going strange wherever he sleeps.
- **Destroying one cleans its area immediately** — the field falls back around
  where it stood, and the source's contribution stops with it.

| Artifact's state | Effect on the field |
|---|---|
| **Left where it lies** (its cult landmark) | A fixed local source, thickening the cells around it |
| **Stashed by the player** | The stash becomes the source — same rule, new place |
| **Carried** | A moving source: the trail thickens behind the carrier, everywhere he goes |
| **Destroyed** | Source gone, area cleaned at once — the Sacrilegist's thousand-year policy, re-enacted by hand |
| **Given to Nefesh** (§3.6) | Out of the world, into hands that owe nothing to the dead gods |

**Picking one up is waking it up.** Clear a cult dungeon, pocket the loot, and
you have not cleaned the world — you have converted a fixed local source into a
mobile one that follows you. Every instinct a player brings from other games
scores that as heroism.

**It does NOT accelerate the ending.** (Owner's ruling — an earlier draft of
this file had it feeding the ten-year prophecy; it does not.) The prophecy is
its own clock. What the field moves is not the deadline but **the genre of the
world you live in**:

| Player's artifact policy | The world becomes |
|---|---|
| **Destroy them, cleanse the world** | Broadly a **magic-fantasy fairy tale** — the shadows recede, the world stays legible |
| **Hoard them / leave them lying** | **Dark fantasy** — thickening shadows, anomalies, demons, cult growth, whisper events |

This is the deepest expression of §9's thesis in the world simulation: the
player is not choosing an alignment or a karma bar, he is **choosing which kind
of story his ten years are made of** — and nothing ever tells him he is choosing.
The smooth ramp is what makes it work: a stepped meter would be a score, and a
score would be a moral judgement.

**Who can actually hold ground against it: the player, and almost nobody else.**
A player who has been destroying artifacts *and* is a strong mage is a negative
source (§1.1) — permanently stronger with every artifact he breaks — and his
mage squads with him. That is the entire list. No kingdom garrisons the dark
back; no faction policy cleans a province. If a region of this world stays
bright over ten years, it is because one person and his people stood in it. **That is the game's answer to "can I actually change
anything?" — yes, and you are the only one who can.**

It also closes the loop onto §6: the Warrior who takes up a black artifact out
of desperation, exactly as the Peasant King did, is personally pulling his own
world toward dark fantasy. — `NOT BUILT`.

### 3.6 The Witches — outside the system

*`NOT BUILT` as a faction row (there is a `Witch` NPC type and a witch sfx).*

The witches are **completely insane** and **exist independently of the dead
gods and of magic alike**. They are the one thing in the setting that does not
belong to the two-force metaphysics — a foreign body in their own world.

**What they are: demiurges. They are playing in this world.** That is the whole
answer, and it explains everything without softening anything — why they stand
outside the two-force metaphysics (they are not made of it), why they are
insane by mortal reading (a player's whims look like madness from inside the
save file), why their quests are arbitrary, and why the apostle route ends with
them **simply leaving** (§3.6): they were finished playing.

It also puts them in a very specific relationship to the person holding the
controller. **The witches are the player's peers** — other beings who came to
this world to see what it does, who are not bound by its rules and who will
leave when they are bored. Everything the game says about the player observing a
phenomenon (§0, §9) is true of them too. That is the quiet reason the
prologue-witch branch (§5) leads *outside the cycle*: to beat a demiurge is to
apply for the job.

**Beyond that they are presented with no explanation, on purpose.** No origin,
no hierarchy, no council, no ranking — the game never accounts for them and
never will. Their names are read off their domains, kabbalah-style (Nefesh, Hokma,
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

**Naming is settled** (owner's ruling). **Nefesh** is the blonde greed witch —
*nefesh* is the appetitive soul, the rung of a person that hungers, and that is
her domain exactly. The witch who rescues the player in the prologue is
therefore **Hokma**, the red one. (An early transcript used the name Nefesh for
the rescuer; that is superseded. Outside the roster table the plot text calls
her "the red witch", which stays correct.) **Temurah** is the green one's name,
chosen over Ruach.

- **How you meet them: each witch is a macro NPC squad**, like any other party
  on the map — **and she can teleport.** That one property is the whole reason
  she feels less systematic than the Peasant King: he walks a region on an
  errand, she is simply *somewhere*, and then somewhere else. It also needs
  almost nothing new — the macro AI already has a **teleporter** behaviour among
  its kinds ([design.md](design.md) §3 baseline; `macro/npc_spawn`), so a witch
  is a party row with the teleport behaviour and her own quest generator.
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
you, and leave you to die. **The red witch finds you.** In parallel, elsewhere on the
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
  mages**, and that is a standing you begin with, not one you earn — but the
  numbers stay **small negatives**, not a hunt;
- **your starting items**;
- **one starting spell — Magic Bolt — and only for the Magika start.** Not a
  spellbook: *the* bolt, the same infinite-ammo popgun §7 describes. Every other
  start begins with none at all. That keeps the promise of "a start is flavour":
  the gap between starts is one weak cantrip, not a magic system, and a mage who
  begins in the Empire simply goes and takes his magic out of a spire in the
  wilds like everybody else did.

**A start is flavour, not a fork** (owner's ruling: «начало игры это чисто
антураж, не должно кардинально менять геймплей»). It colours the opening hours
— who nods at you, what you are holding, which road is obvious — and then the
game is the same game. So: mild reputation offsets, a small item kit, and no
start locked out of anything.

"Plays best as" is therefore a **consequence, never a class lock** (§6):
nothing stops a mage starting among the barbarians with no spells at all, and
that is a legitimate, brutal run. The lore does the balancing here; no
difficulty setting is needed.

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

**Ruling: take neither horn.** Reuse the spell system and let the lore do the
work.

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

### THE ACTIVE-ABILITY LAW

The ruling generalises past clerics, and it is the important part:

> **Every active ability in the game is a spell.** One registry
> ([spells.md](spells.md)), one set of rows, one resource path, one cast path —
> miracles, magebane dispels, black-energy powers, whatever comes later.

It is **technically honest**: a miracle is not a spell wearing a costume, it *is*
a spell — no special flags, no hardcoded branch, no second registry. At most it
carries **one extensible kind field** (`spell` / `miracle` / …) for naming and
for who will teach it, in the same style as every other table in this project:
one row to add a kind, no engine change.

That is what makes the setting's central joke free: the cleric's miracles **are
magic**, the precise heresy his own order burns villages for, and they are the
same rows in the same file. The game never has to say it. A cleric who pays
attention can notice that his miracle and the heretic's spell do the same thing
to the same wound.

So the Empire start's "trader / paladin / cleric" costs **one acquisition
channel, one kind value and one word in the UI**. — `NOT BUILT`.

**The Republic of Timaert is deliberately not a starting region.** It is
**isolated on purpose** — kept across the sea as a destination you *travel to*,
so the first sight of a locomotive, a musket volley and a galleon lands as a
discovery rather than as the furniture of your home town. The game is named
after the one place you cannot begin in — see §3.3 for why that is the point.

*Shipping-build note: the start region choice is `NOT BUILT`. New Game anchors
the player at the first generated city (`citiesFlat[0]` in `src/app/main.cpp`),
and Custom New Game exposes map size / seed / city count / layer parameters —
no region pick.*

**Ten years.** Three outcomes:

1. **The Witch route (classic).** Serve the red witch's line to its end, become the
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

- **The black spear is a black artifact, and NOBODY knows what it is** — not the
  King, not the player, not the game. That question is never answered to anyone.
  By §3.5's own rule he is therefore **a walking source**: the most famous
  artifact in the world, trailing shadow across the map for decades behind a man
  liberating peasants with it. Anything strange in the lands he freed is
  explained for free, and never explained out loud.
- **The spear cannot be taken from him.** Beat him and you still do not get it —
  **the spear has a will of its own**. After a while he returns, and it is back
  in his hands. This is the same law as his own return (below), applied to the
  object: causality keeps them together.
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

**They are thirteen honest agents of the world simulation.** Not a dungeon full
of bosses waiting for the player: each eunuch **travels between the Empire's
cities with a large army and a retinue**, holds his **own sphere of influence**,
and periodically they **convene a council in the capital**. AI state plus
context, in the macro-party model the game is already built around (a party IS
its leader NPC — see the macro-parties design and [macrosim.md](macrosim.md)).

Two ways to reach one, and the world produces both without authoring them:

- **On the road** — catch him travelling. He comes with an army, so this is a
  battle, not an assassination.
- **At the council** — when they convene in the capital, **all thirteen are in
  one place at one time**. The single hardest thing in the game, and it exists
  because their AI puts them there, not because anyone designed a raid.

**What their deaths do to the Empire — fact → rule.** A eunuch's death is a
**fact of the world**, not a scripted cutscene, and the rules read the facts.
This is the project's existing condition→effect machinery
([progression.md](progression.md): EventBus + LogicNodeEngine), so it needs no
new system:

| Facts accumulated | The rule that fires |
|---|---|
| One, two, several eunuchs dead | **The Empire comes apart at the seams** — rebellions flare, and it loses lands |
| **The last eunuch dead** | **Civil war swallows it. The Empire of Light ceases to exist as a state.** |

Note how cleanly that lands on what already exists: a city whose `kingdomIdx`
goes to −1 becomes **Free Folk** by construction, with no code to change
(`src/macro/faction.h`). The largest state in the world dissolving is, at the
data level, its cities losing their owner one at a time.

**And the survivors notice** — as everything in this game does. Nothing here is
a state machine that ignores its own world: the Empire's remaining eunuchs read
their context like every other actor, so a court that has been losing members
behaves like one. (Owner's ruling: «да, замечает, всё в игре контекстно».)

**And it is a side line.** Plenty of lore and story hangs off it, but a player
on a normal run **may never meet a eunuch at all** — this is a thing the world
permits, not a thing it asks for. Which is exactly the register: the biggest
political event the game can produce is optional, unmarked, and driven entirely
by the player deciding to go and do it. — `NOT BUILT`.

### The red witch — the patron you may kill in the prologue

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
| **Apostle of the Witches** | Complete the red witch's line | They leave the world. You were an instrument. |
| **Rebellion** | Turn on the witches | Demon gates open unfairly; survive on preparation and luck |
| **The Prophecy (default)** | Ten years elapse | The King is betrayed, spears the infant, shadows condense, demons take the world |
| **The Empty Land / Dead God** | Genocide run | See below — `NOT BUILT` |
| **Beyond the Cycle** | Kill the red witch in the prologue → the "Outside Time" branch | Carry the character into new games — diegetic NG+ — `NOT BUILT` |

**Late-game sandbox is unconditional.** Every character in the world can be
killed, every city can be destroyed. The world is not held up by immortal NPCs
with scripted dialogue; it is built to digest any chaos the player commits. Kill
a key figure and the story does not break — it proceeds down its darkest
available branch.

**The Empty Land (genocide) ending — deferred.** The owner's ruling: too early
to decide, this belongs to **a later expansion**. What follows is the recorded
material, held for then rather than built now.

 When there is no living NPC and no intact
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

## 11. Early Access scope

**The EA build is an early demo, and its lore surface is deliberately tiny**
(owner's ruling): **the opening quests, then free play.** Nothing else from this
document is promised for it.

**The authored opening, in full:**

1. **The rescue.** Bandits leave you for dead; **the red witch saves you** — with
   her green sister talking in the background (§3.6).
2. **The first quest: explore the ruins next to the town.**

Then free play. That is the whole authored surface of the demo, and it is a good
one: it teaches the two scales in the order the game uses them — the world map
takes you to the ruins, the subworld is what the ruins *are*.

| In EA | Not in EA |
|---|---|
| The rescue + the ruins quest | The ten-year clock |
| Free play afterwards: the world, the economy, the factions, the map | Any of the endings |
| Whatever of §12's ledger happens to be built by then | The witch routes, the rebellion, the prophecy |

That is a scoping decision, not a lore change: the fiction below stays canon,
and EA simply ships the first hours of it. Worth stating plainly so nobody
builds the doom clock to hit an EA date, and so EA's marketing never promises
the ten years.

## 12. Lore ↔ code parity ledger

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

1. **The cult growth curve** (§3.5) — from passive wanderers around their own
   landmarks, through more and stronger squads, to **taking settlements** where
   the field has settled; a captured city keeps trading but plays by new rules.
   Silent throughout; no meter, ever.
2. **THE FIELD** (§1.1, §3.5) — ONE signed per-cell world layer, −127 magic …
   +127 black energy, diffusing and saturating, in the family of `zones.h` and
   the night-glow bake. **Annihilation is the addition operator, not a rule.**
   Positive sources: artifacts wherever they are kept, plus a carrier's trail.
   Negative sources: whoever destroys an artifact (player or NPC), very strong
   mage squads, dragons. **Calibration: unopposed → whole world in ~100 game
   years**, i.e. ~10 % of it over a full playthrough. Does not touch the
   ten-year clock.
3. **The Peasant King as a roaming, respawning, magic-immune elite NPC** (§5),
   with an **unlootable spear** that returns to him when he does.
4. **Named figures at all** — the Sacrilegist, the four witches, the Captain,
   the Peasant King, the Thirteen Eunuchs; there is a `Witch` NPC *type* today,
   and no persons anywhere.
5. **The four witches as teleporting macro NPC squads** + one procedural quest
   generator each, keyed to her domain (§3.6): exploration/fetch (Hokma), boss &
   named-NPC contracts (Sepheret), the standing artifact turn-in (Nefesh), pure
   absurd randomness (Temurah); plus the two-witch opening scene and Hokma's
   cannot-lie dialogue rule.
6. **Paladin & cleric builds** (§4.1) — church-granted **miracles** as an
   acquisition channel + one `kind` value over the ONE spell registry (the
   active-ability law), magebane perks and relics for the paladin, WIS as their
   attribute.
7. **The prologue + the ruins quest** — the EA opening (§11): bandit ambush,
   left for dead, the red witch's rescue with her sister in the background, then
   "explore the ruins next to the town".
8. **The start-region choice** — Empire / Magika / Barbarian, Timaert
   deliberately excluded (§4): mild reputation offsets, a small item kit, and
   **Magic Bolt for the Magika start only**. Flavour, not a fork. Today the
   player is anchored at `citiesFlat[0]` with no choice at all.
9. **The ten-year clock and its endings**, including the default doom.
10. **Prosperity contrast** freed vs mage-ruled villages (§10).
11. **The Thirteen Eunuchs as thirteen travelling macro parties** — army,
    retinue, sphere of influence, periodic council in the capital; each death a
    world fact; rebellions and land loss as they die, and **the Empire
    dissolving into civil war when the last one falls** (§5).
12. **Eye-golems, roaming magebane paladins**, Empire police-state units.
13. **Line infantry, artillery, ships, naval combat**, the Timaert galleon.
14. **Procedural per-kingdom profiles** for the barbarian realms.
15. **Black-energy skills**, including the opaque statistical ones and the empty
    one (§9).
16. **The magic-immune dragon.**
17. **The place that does not exist** and the Sacrilegist fight.
18. **The prologue-witch branch and diegetic NG+** (§5).

**Resolved against the code, no work needed:** *Southern Magika* is simply
another name for **Lower Magica** (owner's ruling) — the registry row
`lower_magica` stands, no new row, and the proxy-war framing attaches to the
existing `empire` ↔ `lower_magica` alliance override.

## 13. Open questions for the owner

**Every question raised so far has been ruled on.** What follows is not a list
of gaps in the fiction — it is the list of **design risks** an outside reader
would raise against this document as it stands, kept here so they are decided
rather than discovered late.

1. **The ten years are disconnected from the simulation.** The clock is a timer,
   not an outcome: the field, the cults, the artifacts, the eunuchs, the Empire's
   collapse and a hundred hours of economy have — by explicit ruling — **no
   effect on how the game ends**. Only one authored quest line does. The fatalism
   is deliberate and defended (§4), but there is a cheap fix that costs the
   fatalism nothing: let the **epilogue read the world state** — the field, who
   holds which cities, whether the Empire still exists, what your realm became.
   Same ending, described by the world you actually made. Owner's call.
2. **The empty perk depends on a secret a shipped binary cannot keep** (§9). It
   will be datamined within a week of release and become "the troll perk", which
   destroys the intended discovery in exactly the wrong order. The opaque
   *statistical* effects survive datamining; the truly-empty one does not.
   Possible repair that preserves the idea completely: **make WHICH perk is empty
   vary per save/seed**, so a wiki can say "one of these does nothing" and never
   which one is yours. Then the player must still decide what he believes.
3. **The opening is the most generic thing in the game — and it is the whole EA
   demo.** "Bandits rob you and leave you for dead; a mysterious woman saves
   you" is the single most-used RPG opening there is, and EA ships exactly that
   plus "explore the ruins near the town" (§11). So the demo is the most
   ordinary slice of a deeply unusual world. Everything specific is *already in
   that scene* and merely under-used: **make the two witches' conversation the
   content of it.** Let the player lie there and overhear two demiurges
   discussing him as an object, with the green one forgetting he is present and
   the red one unable to lie about what is going to happen to him. The cliché
   frame then becomes the joke, and the first ninety seconds tell the player
   exactly what kind of game this is.
4. **What exactly changes in a cult-held city** (§3.5) — deferred by ruling, but
   it is the payoff of the entire field system, so it should not stay deferred
   for long.

## 14. Will a player care? — an honest read

Written for the Early Access decision, not as promotion. What follows is what
this document's fiction is actually likely to do to a player.

**The four things that will land, in the order they land:**

1. **The names.** *The Peasant King*, *the Thirteen Great Eunuchs*, *the
   Sacrilegist*. They are repeatable after one hearing and funny without being
   jokes. Proven in the wild already: the owner's own non-gaming relatives
   picked "the king of the bums" up and ran with it. This is the cheapest,
   strongest marketing asset the project owns and it already exists.
2. **The peasants in the fields.** The world is legible without a tutorial — a
   70-year-old who has never touched a computer read the screen correctly in one
   glance (§10). A player who cannot name a single faction still understands
   that grain moves, that caravans are robbed, that a village near a mountain is
   poor. **Legibility is the hook; the metaphysics is the reward.**
3. **The Timaert reveal.** A title that is a question, answered by a sea voyage
   and a railway (§3.3). Players talk about moments like that for years.
4. **"So that was possible?"** Two players comparing runs — the archmage who
   burned the Empire and the trader who married a local and watched the red sky
   from a roof — is a conversation that sells the game to a third player better
   than any trailer (§4, §7).

**What will genuinely be hard:**

- **The lore is delivered so indirectly that most players will never assemble
  it.** The forged religion, the annihilation law, the true nature of the spear:
  by design, none of it is stated (§10.1). That is the right choice for the ten
  people who will write essays about it, and it means the median player finishes
  a run with "there were witches and something about dead gods". The mitigation
  is not exposition — it is **making the consequences visible even when the
  causes are not**: a village that got richer, a road that got worse, a city with
  new laws.
- **The mage's early game is deliberately punishing** (§7). It is the author's
  own favourite class and the one most likely to make a new player quit in the
  first hour. Worth knowing before EA reviews say it.
- **The ten years do not read the world** (§13.1). A hundred hours that cannot
  change the ending is the one place the project's own philosophy bends.

**The honest bottom line.** This world's strength is not its plot — the plot is
serviceable dark fantasy with a good twist about a forged religion. Its strength
is that **the fiction and the systems are the same object**: a build is a
biography (§6), annihilation is an addition operator (§1.1), a hoard is a
weather system (§3.5), belief is a mechanic (§9). Very few games can say that,
and the players who notice are exactly the players who never stop talking about
a game once they find one.

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
