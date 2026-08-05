#!/bin/sh
# Run one smoke scenario and print only its report.
#
#   sh smoke.sh                      -> the macro travel-stamina scenario
#   sh smoke.sh subworld_enter       -> any other scenario token
#   sh smoke.sh a,b,c                -> several, in order
#   sh smoke.sh cast_spell 1,7,999   -> the same scenario on three worlds
#
# The boot prefix (new_game,wait_boot_done) and the trailing quit are added
# here, because every scenario needs a world and nobody wants a window left
# open. Nothing is exported: this shell dies with the run, so your next plain
# ./build/timaert starts the game normally, with its menu.
ACTIONS="${1:-macro_travel_sp}"

# ONE world unless you ask for more. new_game seeds itself from SDL_GetTicks()
# (choose_new_game_seed), so before this every run was a different planet: a red
# smoke could not be repeated, and a regression was indistinguishable from bad
# luck. 12345 is the seed the graphics captures already pin, so the whole suite
# now agrees on one world.
#
# Name several seeds to sweep — that is where world-DEPENDENT behaviour lives,
# and it is real behaviour, not noise: subworld_self_fireball is green on 12345
# and red on seed 1, because on seed 1 the ground rises within the blast radius
# and your own fireball detonates in your face. A sweep is how you find that on
# purpose instead of by accident.
SEEDS="${2:-${TIMAERT_SMOKE_SEED:-12345}}"
SEEDS=$(echo "$SEEDS" | tr ',' ' ')
SEED_COUNT=$(echo "$SEEDS" | wc -w)

for seed in $SEEDS; do
    [ "$SEED_COUNT" -gt 1 ] && echo "--- seed $seed"
    TIMAERT_SMOKE_SEED="$seed" \
    TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,${ACTIONS},quit" \
        ./build/timaert 2>&1 | grep '^\[smoke\]'
done
