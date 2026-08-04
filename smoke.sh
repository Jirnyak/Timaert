#!/bin/sh
# Run one smoke scenario and print only its report.
#
#   sh smoke.sh                 -> the macro travel-stamina scenario
#   sh smoke.sh subworld_enter  -> any other scenario token
#   sh smoke.sh a,b,c           -> several, in order
#
# The boot prefix (new_game,wait_boot_done) and the trailing quit are added
# here, because every scenario needs a world and nobody wants a window left
# open. Nothing is exported: this shell dies with the run, so your next plain
# ./build/timaert starts the game normally, with its menu.
ACTIONS="${1:-macro_travel_sp}"
TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,${ACTIONS},quit" \
    ./build/timaert 2>&1 | grep '^\[smoke\]'
