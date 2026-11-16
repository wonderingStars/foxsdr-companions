#!/usr/bin/env bash
# Runs the rtl433 helper over a wideband I/Q recording, one run per channel, and prints COUNTS ONLY (no ids, no
# payloads, nothing written to disk): wideband file -> rtl433-iqshift (move the channel to 0 Hz, decimate) ->
# rtl433-helper -> tools/summarize_events.py.
#
#   tools/run_real_recordings.sh <file> <cs16|cu8> <file rate Hz> <file centre Hz> <decim> <floor dBFS|native> <channel centre Hz>...
#
# Example (868 MHz, B200 2.5 MS/s -> 250 kS/s, two channels, noise floor put at -45 dBFS):
#   tools/run_real_recordings.sh rec.cs16 cs16 2500000 869000000 10 -45 868300000 868950000
# `native` leaves the recording's own level (digital gain 0), `-45` applies the gain that puts the channel's median
# envelope at -45 dBFS (see tools/iqshift.c: rtl_433's detector has a minimum level the weak B200 files do not reach).
# Set BUILD to the build directory that holds Release/rtl433-helper.exe and Release/rtl433-iqshift.exe.
set -u
file=$1; fmt=$2; rate=$3; centre=$4; decim=$5; floor=$6; shift 6
BUILD=${BUILD:-build}
here=$(cd "$(dirname "$0")" && pwd)
out_rate=$((rate / decim))
if [ "$floor" = native ]; then gain=(--gain-db 0); else gain=(--floor-dbfs "$floor"); fi
for ch in "$@"; do
  shift_hz=$((ch - centre))
  "$BUILD/Release/rtl433-iqshift.exe" --format "$fmt" --rate "$rate" --shift "$shift_hz" --decim "$decim" "${gain[@]}" < "$file" 2> >(sed 's/^/  /' >&2) \
    | "$BUILD/Release/rtl433-helper.exe" --foxsdr-helper 1 --format cs16 --rate "$out_rate" --centre "$ch" --hide-ids \
    | python -I "$here/summarize_events.py" "centre ${ch} Hz, ${out_rate} S/s, floor ${floor}"
done
