// THE sprite table (macro/sprite_rows.h) — the binding, not the numbers.
//
// The law it serves (sprites.md): every visible kind resolves to SOMETHING —
// drawn art if the artist has drawn it, a procedural body plan if he has not.
// One table holds both, so a goblin and a peasant sit in the same list.
//
// What is asserted here is deliberately NOT "row 12 is 0x4A8A2Au". A restated
// literal breaks on every retune and proves nothing about intent (AGENTS.md
// testing law #4). What is asserted is the properties that make the table a
// system:
//   * the table is TOTAL — every creature and every NPC kind names a row, and
//     none of them names the empty row, so no kind can be invisible;
//   * a row RESOLVES — art or body plan, never neither, or a kind pointing at
//     it draws nothing at all;
//   * a procedural body is TINTED — a silhouette drawn with tint 0 is a black
//     hole in the world, which is what the fauna rows' colours used to prevent
//     from inside FaunaEntry before they moved here;
//   * a creature's row is ITS OWN — the copy-paste failure (a new creature
//     pointing at the row above it, which no smoke can see because a wolf drawn
//     as a bear is still a plausible quadruped) is caught by name.
//
// The last one carries a negative control: the check is only worth anything if
// the detector can tell two rows apart in the first place.
#include "check.h"

#include "macro/fauna.h"
#include "macro/npc.h"
#include "macro/sprite_rows.h"

#include <cstring>

namespace {

using sm::kSpriteRows;
using sm::SpriteDef;
using sm::SpriteId;

// A row is useful only if SOMETHING can be drawn from it. `None` is the one
// legitimate blank — it exists so a zeroed field draws nothing rather than a
// city, and the places without art (a ruin, a shrine) point at it and fall back
// to the glyph mark their own presentation row carries.
void test_every_row_resolves() {
    for (std::size_t i = 0; i < std::size_t(SpriteId::Count_); ++i) {
        const SpriteDef& row = kSpriteRows[i];
        if (SpriteId(i) == SpriteId::None) {
            CHECK(row.asset == nullptr && row.archetype == sm::kNoBody,
                  "the None row must stay empty — it is what 'draws nothing' means");
            continue;
        }
        CHECK(row.name != nullptr && row.name[0] != '\0',
              "every sprite row needs a machine id");
        CHECK(row.asset != nullptr || row.archetype != sm::kNoBody,
              "a row with neither art nor a body plan can never be drawn");
        if (row.asset == nullptr) {
            CHECK(row.tint != 0u,
                  "a procedural body drawn with tint 0 is a black silhouette");
            CHECK(row.archetype < 7u,
                  "a body plan must be one the shader knows (creature_sprite.glsl)");
        }
    }
}

// Totality, creature side: the table must cover the monster registry, or a
// creature spawns with no picture and the renderer draws whatever row 0 holds.
void test_every_creature_names_a_row() {
    const auto& catalog = sm::creature_catalog();
    CHECK(!catalog.empty(), "the creature catalog must not be empty");
    for (const sm::FaunaEntry* f : catalog) {
        CHECK(f != nullptr, "a null creature row would spawn nothing");
        if (!f) continue;
        CHECK(f->sprite != SpriteId::None,
              "a creature must name a real sprite row, not the empty one");
        CHECK(std::size_t(f->sprite) < std::size_t(SpriteId::Count_),
              "a creature's sprite row must be inside the table");
        const SpriteDef& row = sm::sprite_row(f->sprite);
        CHECK(row.asset != nullptr || row.archetype != sm::kNoBody,
              "a creature's row must be drawable");
        // The binding, and the copy-paste guard: a creature and its picture
        // share one machine id. Humanoid KINDS share rows on purpose (every
        // unremarkable townsman is a peasant), but a creature's body is its own.
        CHECK(std::strcmp(f->id, row.name) == 0,
              "a creature's sprite row must be the row of the same name");
    }
}

// Totality, humanoid side. Sharing is legal here — several kinds may point at
// one picture — so the assertion is only that every kind HAS one.
void test_every_npc_kind_names_a_row() {
    for (std::size_t i = 0; i < std::size_t(sm::NPCType::Count); ++i) {
        const sm::NpcTypeDef& def = sm::npc_def(sm::NPCType(i));
        CHECK(def.sprite != SpriteId::None,
              "an NPC kind must name a real sprite row, not the empty one");
        CHECK(std::size_t(def.sprite) < std::size_t(SpriteId::Count_),
              "an NPC kind's sprite row must be inside the table");
        const SpriteDef& row = sm::sprite_row(def.sprite);
        CHECK(row.asset != nullptr || row.archetype != sm::kNoBody,
              "an NPC kind's row must be drawable");
    }
}

// NEGATIVE CONTROL for the name binding above. If every row answered to the
// same name, `strcmp(...) == 0` would pass on a table where every creature
// pointed at the same picture — the check would be a decoration. Two rows that
// really are distinct prove the detector can see the defect it guards.
void test_the_name_check_can_fail() {
    CHECK(std::strcmp(sm::sprite_row(SpriteId::Goblin).name,
                      sm::sprite_row(SpriteId::Troll).name) != 0,
          "two different rows must carry two different names");
    // And the out-of-range guard really clamps rather than reading past the end.
    CHECK(sm::sprite_row(SpriteId::Count_).asset == nullptr,
          "an out-of-range id must resolve to the empty row");
}

} // namespace

int main() {
    test_every_row_resolves();
    test_every_creature_names_a_row();
    test_every_npc_kind_names_a_row();
    test_the_name_check_can_fail();
    return sm::test::report("sprite_rows_test");
}
