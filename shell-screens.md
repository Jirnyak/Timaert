# Shell screens — THE doc for everything before the world exists

> Studio splash → title menu → intro slideshow → character creation → the
> world. This document owns that chain: what each screen is, who owns its
> state, and the laws they all obey. Built 2026-09-04.

The game is **Legacy of Sacrilege**, subtitled *The Timaert Chronicles*; the
studio is **TENEVIK GAMES** ("An experimental indie studio"). The codebase
keeps the name `timaert` throughout — it is the project's name, not the
product's.

---

## The chain

| Screen | `AppState` | Drawn by | Leaves on |
|---|---|---|---|
| Studio splash | `Splash` | `draw_studio_splash` (ui/intro_screens.cpp) | any key (2nd), Esc, or idle timeout |
| Title menu | `Title` | `draw_title_menu` (ui/screens.cpp) | a menu button |
| Intro slideshow | `IntroSlides` | `draw_intro_slides` (ui/intro_screens.cpp) | last slide, or Esc |
| Character creation | `CharacterCreation` | `draw_character_creation` (ui/screens.cpp) | Start / Back |
| The world | `Playing` | the game | — |

`Splash` is the launch state, and only the launch state: returning to the
title from a game lands on `Title`, so the splash plays once per run.

**Every screen answers with a `ShellResult` and changes nothing itself.**
`apply_shell_actions` (app/main.cpp) is the ONE transition dispatcher; screens
raise intent flags, it moves the state machine and boots worlds. `boot_world`
deliberately does not touch `app.state` — when it did, the custom-world
preview had to fight it back to its own screen after every regeneration.

`merge_shell_result` must carry EVERY flag: a smoke action that raises one the
merge forgot is silently dropped on the floor.

---

## The cinematic module (ui/intro_screens.h/.cpp)

The splash and the slideshow live apart from the other screens on purpose.
They include ImGui, the two UI bricks (`ui_image`, `ui_theme`) and the
authored slide table — **nothing from the game**: no `GameState`, no ECS, no
world, no save. Delete the module and the game still boots.

### The studio splash

A swarm of blood-dark pixels assembles into an emblem: the pixel sword
(bitmap `kSwordRows`, 11×16, point down) standing between **TENEVIK** and
**GAMES**, whose letters are 5×7 bitmap glyphs blown up into big square cells
(`kSplashGlyphs`). Arriving sword pixels cool from blood into steel, bronze
and leather; stray specks die out; a few blots stay. Then blood runs down the
blade in whole-cell segments and drips from the point as falling squares.

Three laws it must keep:

1. **Everything is a separate square.** Each cell is drawn inset by ~12% of
   its side, and parchment cells wobble across three shades. Flush,
   same-coloured cells melt into one solid shape — which is exactly what made
   the first attempt read as a font instead of an assembly.
2. **The cursor is a toy, never a door.** Hovering shoves, a click *punches*.
   The physics is ported verbatim from the reference intros (starcluster
   `shell.cpp` `applyCursor`/`applyPunch`, gigahrush `intro_ui.cpp`
   `integrate`) under the same constant names. The load-bearing one is the
   **projectile gate** `kFlySpeed`: a struck particle has its homeward spring
   switched OFF until friction eats the impulse, which is why a good swipe
   scatters pixels across half the screen instead of wobbling them back like
   jelly. Also: friction 0.988/frame, the cursor hands over its own velocity
   (`kPushDrag`), a held button presses harder, the punch is an instant
   impulse with no `dt`, edges bounce, and the return path is noisy.
   Blood obeys the same law (clinging a little harder), so it can be smeared
   off the blade; falling drops can be batted aside.
3. **Poking never closes anything.** Mouse buttons are excluded from
   "any key" (ImGui counts them as keys — that trap is guarded in
   `splash_any_key_pressed`), and the auto-exit clocks IDLE time, not show
   time: while the mouse plays, the screen stays open forever.

### What "any key" must never mean

Three exclusions, each one a bug that shipped:

- **Mouse buttons.** See law 2 above.
- **Modifiers alone.** Ctrl by itself is nobody pressing a key.
- **Anything while a modifier is held.** A chord is a system command. This
  was the fullscreen bug: macOS fullscreen is **Ctrl+Cmd+F**, so `Ctrl` read
  as the first key (assemble at once) and `Cmd` as the second (leave) — the
  splash ended before it had even finished forming.

And input is deafened across a **window transition** (`window_busy`): the
frame a resize lands on plus a ~0.35 s grace, triggered by a changed
DisplaySize or a `DeltaTime` over 0.25 s. macOS blocks the run loop for the
whole fullscreen animation and then delivers a burst of size changes with one
enormous frame; that input belongs to the window command, not the player.
The slideshow uses the same guard (a resize burst must not turn pages) and
clamps its typewriter delta, or the blocked frame would print a whole line at
once. Nothing is ever *rebuilt* on resize — only input is ignored.

Pacing: swarm ~3.5 s, subtitle at 4 s, blood at 4.6 s, auto-exit after the
show has played and the hands have been off for ~15 s (the fade itself takes
another 1.5 s).

### The intro slideshow

The nine authored slides (`content::intro_story()`) play here, pre-world, on
no engine at all. Frame aspect-fit in the upper screen inside a bronze border,
typewriter caption below it, slide marks at the foot, `Esc skip` at the left.
First input completes the line, the second turns the page.

Two lessons inherited from the reference intro: the typewriter counts **code
points, not bytes** (a byte cut tears a multi-byte character in half), and any
future translation must happen **before** the prefix cut, never on the cut-off
prefix.

The in-world story channel is untouched and still needed: `arrival_story()`
plays one slide through `draw_story_overlay` after the world boots, and
chapter breaks will add rows to that table rather than branches to the code.

---

## The laws every shell screen obeys

- **Logical points, never drawable pixels.** All geometry reads
  `ImGui::GetIO().DisplaySize` (`viewport_size()`). On Retina the SDL drawable
  size is twice the window size; passing it would push menus off screen. No
  screen takes a viewport-size parameter any more — the ones that did ignored
  them.
- **Nothing is cached against the window size.** Layouts (including particle
  homes) are derived every frame. Both reference intros rebuilt or restarted
  their scene on each resize event, and macOS sends a burst of them while
  animating into fullscreen — that is the "it jumps when you go fullscreen"
  bug, and it cannot happen here by construction.
- **One palette.** `ui/ui_theme.h` holds every colour these screens use
  (parchment, grass, bronze, night, three bloods, steel/fuller/leather) plus
  `viewport_size()` and `draw_title_backdrop()`. Fabled sword-and-sorcery, not
  grimdark — the owner's ruling.
- **One image door.** `ui/ui_image.h` (`ui_image_for` / `draw_ui_image`) is
  the only path from a PNG on disk to an `ImTextureID`: lazy, cached by path
  *value*, a missing file remembered as missing so draw code may probe every
  frame for free. Freed at shutdown after the GPU textures.

---

## The title menu

Backdrop `assets/backgrounds/0.png` covers the viewport — **cropped, never
letterboxed**; a backdrop must reach every edge — under a warm dusk wash that
deepens toward the menu. The footer shows `build <git hash>`, stamped into
`generated/git_hash.h` on **every build** by `cmake/git_hash.cmake` (a
configure-time variable goes stale between commits). A trailing `+` means the
build came from a dirty tree.

## Character creation

Authors a `CreationState` through the sheet's real doors
(`spend_attribute_point`, `spend_learn_pick`), so the screen cannot grant what
the game would refuse; the two refunds are creation's own right.

**Default** fills the standard wanderer in one click
(`creation_apply_default`: Male / Empire of Light, STR+2 END+2 SPD+1, Sword,
Light Armor, Shield, Athletics, Travel). The `new_game` smoke presses the same
door — `startNewGame` + `creationDefault` + `startCreatedGame` in one frame —
so the sheet a test boots with is exactly the sheet a player gets. Scenarios
that exercise the spend doors grant themselves the point or pick they need,
because the preset spends the creation budget to zero.

---

## Open tails

- Portraits on the creation screen (`male.png` / `female.png` are authored in
  the choice table and not yet drawn).
- `arrival_story()` carries placeholder text and a borrowed frame.
- The codex article on perks/skills still describes the pre-war skill law.
