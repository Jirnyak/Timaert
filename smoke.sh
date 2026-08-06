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
#
# THIS SCRIPT'S EXIT CODE IS THE GAME'S. That sentence had to be earned: the
# run used to be `./build/timaert 2>&1 | grep '^\[smoke\]'`, so the shell
# reported GREP's status — and grep succeeds whenever it prints a line, which
# it always does. main.cpp returns 2 for a failed scenario and that 2 was
# discarded on every single run: a nine-run sweep with nine red scenarios still
# finished "successfully", and only a human reading the lines would ever know.
# The output is captured first and filtered second, so the game's own verdict
# survives, and a sweep fails if ANY of its runs failed.
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

runs=0
failures=0

for seed in $SEEDS; do
    [ "$SEED_COUNT" -gt 1 ] && echo "--- seed $seed"
    # Capture first, filter second: a pipeline would hand us grep's verdict.
    output=$(TIMAERT_SMOKE_SEED="$seed" \
             TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,${ACTIONS},quit" \
             ./build/timaert 2>&1)
    status=$?
    runs=$((runs + 1))
    printf '%s\n' "$output" | grep '^\[smoke\]'
    if [ "$status" -ne 0 ]; then
        failures=$((failures + 1))
        echo "[smoke.sh] seed $seed FAILED (exit $status)"
    fi
done

if [ "$failures" -ne 0 ]; then
    echo "[smoke.sh] $failures of $runs run(s) FAILED"
    exit 1
fi
echo "[smoke.sh] $runs run(s) passed"
