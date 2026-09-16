#!/bin/bash
# golden.sh — corpus de regressao bit-exata (§7)
#   ./golden.sh make   -> gera corpus + manifest (delimitador '|')
#   ./golden.sh check  -> verify bit-exact + re-encode deterministico
cd "$(dirname "$0")" || exit 1
CLI=./build/slacodec-cli
G=golden
PRESET=high
NAMES="silence sine noise transient wasted mono stereo-wide stereo-asym"
TRACK="GET READY! (GAME OST).wav"

if [ "$1" == "make" ]; then
  mkdir -p $G/wav $G/slac
  : > $G/manifest.txt
  for n in $NAMES; do
    $CLI gen $n "$G/wav/$n.wav" > /dev/null || exit 1
    $CLI encode "$G/wav/$n.wav" "$G/slac/$n.slac" --preset-encode $PRESET > /dev/null || exit 1
    echo "$n|$G/wav/$n.wav|$(sha256sum "$G/slac/$n.slac" | awk '{print $1}')" >> $G/manifest.txt
  done
  if [ -f "$TRACK" ]; then
    $CLI encode "$TRACK" "$G/slac/track.slac" --preset-encode $PRESET > /dev/null || exit 1
    echo "track|$TRACK|$(sha256sum "$G/slac/track.slac" | awk '{print $1}')" >> $G/manifest.txt
  fi
  echo "Golden corpus criado: $(wc -l < $G/manifest.txt) entradas em $G/"

elif [ "$1" == "check" ]; then
  fail=0
  while IFS='|' read -r n wav s; do
    [ -z "$n" ] && continue
    ok=1
    if ! $CLI verify "$wav" "$G/slac/$n.slac" > /dev/null 2>&1; then ok=0; fi
    tmp=$(mktemp)
    if $CLI encode "$wav" "$tmp" --preset-encode $PRESET > /dev/null 2>&1; then
      s2=$(sha256sum "$tmp" | awk '{print $1}')
      if [ "$s2" != "$s" ]; then ok=0; fi
    else
      ok=0
    fi
    rm -f "$tmp"
    if [ $ok == 1 ]; then echo "PASS $n"; else echo "FAIL $n"; fail=1; fi
  done < $G/manifest.txt
  if [ $fail == 0 ]; then echo "--- golden check: ALL PASS ---"; else echo "--- golden check: FAILURES ---"; fi
  exit $fail
else
  echo "Usage: ./golden.sh make|check"
fi
