#!/usr/bin/env bash
# JT9 threshold sweep (not part of CTest: it takes minutes). For each SNR it generates NSEEDS noisy files with
# jt9_gen (same message, same frequency, seeds 1..N), decodes each with the helper and counts how many gave the
# transmitted message. SNR is jt9sim's nominal value (noise sigma 1, signal amplitude 10^(snr/20)).
#
#   test/jt9_sweep.sh <jt9_gen> <weakmodes-helper> <wmtool> "<snr list>" <nseeds> [message] [freq]
#   test/jt9_sweep.sh Release/jt9_gen.exe Release/weakmodes-helper.exe Release/wmtool.exe "-24 -25 -26 -27" 20
set -u
GEN=$1; HELPER=$2; WMTOOL=$3; SNRS=$4; N=$5
MSG=${6:-"CQ K1ABC FN42"}; FREQ=${7:-1500}
TMP=${TMPDIR:-/tmp}/jt9_sweep.$$
mkdir -p "$TMP"
for snr in $SNRS; do
  ok=0; false=0
  for seed in $(seq 1 "$N"); do
    "$GEN" --out "$TMP/t.wav" --snr "$snr" --seed "$seed" --freq "$FREQ" --msg "$MSG" > /dev/null
    "$WMTOOL" wav2raw "$TMP/t.wav" "$TMP/t.raw"
    out=$("$HELPER" --foxsdr-helper 1 --format s16 --rate 12000 --mode jt9 < "$TMP/t.raw" 2>/dev/null | tail -n +2)
    if echo "$out" | grep -q "\"text\":\"$MSG\""; then ok=$((ok+1)); fi
    # any decode whose text differs from the transmitted message is a false decode
    bad=$(echo "$out" | grep -v '^$' | grep -v "\"text\":\"$MSG\"" | wc -l)
    false=$((false+bad))
  done
  echo "snr $snr: $ok/$N decoded, $false false decodes"
done
rm -rf "$TMP"
