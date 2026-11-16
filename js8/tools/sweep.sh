#!/bin/sh
# Decode-threshold sweep for the js8 helper: for each speed and nominal SNR (dB in 2500 Hz) it makes N noisy files
# with a known heartbeat at different audio frequencies and counts how many the helper decodes correctly.
#   tools/sweep.sh <js8gen> <js8-helper> <workdir> <mode> <from-snr> <to-snr> [N]
# Example: tools/sweep.sh b/Release/js8gen.exe b/Release/js8-helper.exe /tmp/sweep A -18 -30 12
GEN=$1; HELPER=$2; WORK=$3; MODE=$4; FROM=$5; TO=$6; N=${7:-12}
mkdir -p "$WORK"
snr=$FROM
while [ "$snr" -ge "$TO" ]; do
  ok=0; sum=0; seed=1
  while [ "$seed" -le "$N" ]; do
    freq=$((600 + seed * 197 % 2800))
    truth=$("$GEN" --out "$WORK/s.raw" --noise-rms 1000 --seed "$seed" --signal "mode=$MODE,freq=$freq,snr=$snr,kind=hb,call=K1ABC,grid=FN42")
    frame=$(echo "$truth" | sed 's/.*"frame":"\([^"]*\)".*/\1/')
    line=$("$HELPER" --foxsdr-helper 1 --format s16 --rate 12000 --modes "$MODE" < "$WORK/s.raw" | grep "\"frame\":\"$frame\"" | head -1)
    if [ -n "$line" ]; then
      ok=$((ok + 1))
      rep=$(echo "$line" | sed 's/.*"snr":\(-\{0,1\}[0-9]*\),.*/\1/')
      sum=$((sum + rep))
    fi
    seed=$((seed + 1))
  done
  if [ "$ok" -gt 0 ]; then avg=$((sum / ok)); else avg=-; fi
  echo "mode $MODE nominal $snr dB: decoded $ok/$N (mean reported SNR $avg)"
  snr=$((snr - 1))
done
