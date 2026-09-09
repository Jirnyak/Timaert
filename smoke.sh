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

# `all` runs THE suite — smoke_suite.txt, one line per scenario. Before this
# existed the set had never been run whole: two scenarios were red and nobody
# knew, because "the suite" was a thing each session rebuilt from the token
# table and got wrong (a scenario is not always a token — five of them need a
# scene standing before them). The list is data now, and the coverage check
# below refuses to run a suite that has lost a scenario.
SUITE_FILE="smoke_suite.txt"

# THE coverage check. Every token the harness knows must appear somewhere in
# the suite, or be one of the three smoke.sh supplies itself. A new scenario
# that nobody added to the list is named HERE, on the next run, instead of
# going unrun for a month — the same law as the save's fold witness: the
# thing that enumerates is the code, never a human's memory.
check_suite_covers_every_token() {
    missing=""
    for tok in $(sed -n 's/.*{"\([a-z_0-9]*\)", *SmokeAction::.*/\1/p' \
                     src/app/smoke.cpp); do
        case "$tok" in
            new_game|wait_boot_done|quit) continue ;;   # the boot prefix
        esac
        # Word match: the token must stand alone, not inside a longer name
        # (subworld_enter must not be answered by subworld_enter_foo).
        if ! grep -Eq "(^|[,[:space:]])${tok}([,[:space:]]|$)" "$SUITE_FILE"; then
            missing="$missing $tok"
        fi
    done
    if [ -n "$missing" ]; then
        echo "[smoke.sh] SUITE INCOMPLETE — these tokens are in smoke.cpp but"
        echo "[smoke.sh] in no line of $SUITE_FILE:$missing"
        echo "[smoke.sh] Add each to the suite (with the scene it needs before"
        echo "[smoke.sh] it, if any), or to the boot-prefix exemption here."
        return 1
    fi
    return 0
}

if [ "$ACTIONS" = "all" ]; then
    if [ ! -f "$SUITE_FILE" ]; then
        echo "[smoke.sh] $SUITE_FILE not found — run me from the repo root."
        exit 1
    fi
    check_suite_covers_every_token || exit 1
fi

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
reds=""

# One scenario on one world. Kept as a function so `all` and a named scenario
# walk the SAME path — a suite that ran its runs differently from a hand run
# would be a second harness.
run_one() {
    scenario="$1"
    seed="$2"
    # Capture first, filter second: a pipeline would hand us grep's verdict.
    output=$(TIMAERT_SMOKE_SEED="$seed" \
             TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,${scenario},quit" \
             ./build/timaert 2>&1)
    status=$?
    runs=$((runs + 1))
    printf '%s\n' "$output" | grep '^\[smoke\]'
    if [ "$status" -ne 0 ]; then
        failures=$((failures + 1))
        reds="$reds
    $scenario  (seed $seed, exit $status)"
        echo "[smoke.sh] seed $seed FAILED (exit $status)"
    fi
}

if [ "$ACTIONS" = "all" ]; then
    SUITE=$(grep -v '^[[:space:]]*#' "$SUITE_FILE" | grep -v '^[[:space:]]*$')
    SUITE_COUNT=$(printf '%s\n' "$SUITE" | wc -l | tr -d ' ')
    echo "[smoke.sh] the suite: $SUITE_COUNT scenarios x $SEED_COUNT seed(s) ="
    echo "[smoke.sh] $((SUITE_COUNT * SEED_COUNT)) runs. Minutes, not seconds."
    for seed in $SEEDS; do
        echo "--- seed $seed"
        # A pipe would run this loop in a SUBSHELL and lose `failures` with it —
        # the suite would then report every world green. Feed it a here-doc.
        while IFS= read -r scenario; do
            [ -z "$scenario" ] && continue
            echo "--- $scenario"
            run_one "$scenario" "$seed"
        done <<SUITE_EOF
$SUITE
SUITE_EOF
    done
else
    for seed in $SEEDS; do
        [ "$SEED_COUNT" -gt 1 ] && echo "--- seed $seed"
        run_one "$ACTIONS" "$seed"
    done
fi

if [ "$failures" -ne 0 ]; then
    echo "[smoke.sh] $failures of $runs run(s) FAILED:$reds"
    # A red from a long sweep is a HYPOTHESIS. Three macro-time scenarios have
    # gone red inside a full suite and green every time they were run alone
    # (postdemoaudit.md SMOKE-7) — re-run the line above by itself before you
    # believe it names a defect.
    [ "$ACTIONS" = "all" ] && echo "[smoke.sh] re-run each red ALONE before believing it (SMOKE-7)."
    exit 1
fi
echo "[smoke.sh] $runs run(s) passed"
