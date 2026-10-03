#!/usr/bin/env bash
# Headless checks for the port. Run via `make test`.
#
#   tests/run.sh build/port
set -euo pipefail

out="$1"
sim="$out/skifree-sim"
sdl="$out/skifree"
fails=0

pass() { echo "ok: $*"; }
fail() { echo "FAIL: $*" >&2; fails=$((fails + 1)); }

# Last player position/state from --trace output: "x y state".
final_player() { grep '^tick' | tail -1 | sed -E 's/.*x=(-?[0-9]+) y=(-?[0-9]+) state=(-?[0-9]+)/\1 \2 \3/'; }

# Determinism: a fixed seed and script must always give the same game, in
# both binaries. The golden hash is a regression check; update it (and say
# why in the commit) when a change to the core is meant to alter play.
script="0:down,40:left,80:right,120:insert,200:down,300:click,301:click,400:pgdn,500:end,600:down"
golden="224d8c49"
a="$("$sim" --headless --seed 1 --ticks 1000 --script "$script" --hash)"
b="$("$sim" --headless --seed 1 --ticks 1000 --script "$script" --hash)"
c="$("$sdl" --headless --seed 1 --ticks 1000 --script "$script" --hash)"
[[ "$a" == "$b" && "$a" == "$c" ]] && pass "deterministic ($a)" || fail "nondeterministic: '$a' '$b' '$c'"
[[ "$a" == "hash $golden" ]] && pass "golden hash" || fail "golden hash: got '$a', want 'hash $golden'"

# The skier passes 2000 m and the Yeti eats him.
r="$("$sim" --headless --seed 7 --ticks 120 --script "0:warp=0/31200,0:down" --trace 10)"
y60="$(echo "$r" | grep '^tick 60:' | final_player | cut -d' ' -f2)"
state="$(echo "$r" | final_player | cut -d' ' -f3)"
[[ "$y60" -gt 32000 && "$state" == "-1" ]] && pass "yeti at 2000 m" || fail "yeti: y@60=$y60 final state=$state"

# Each course: start inside its gate, keep pointing downhill (getting up
# after crashes), and expect a finish with a high-score list.
downs="$(seq 0 25 6000 | sed 's/$/:down/' | paste -sd, -)"
check_course() { # name warp-to result-regex
  local r line
  r="$("$sim" --headless --seed 5 --ticks 6000 --script "0:warp=$2,$downs")"
  line="$(echo "$r" | grep -A1 '^High Scores' | sed -n 2p)"
  if [[ "$line" =~ $3 ]] && [[ "$line" == *"that's you"* ]]; then
    pass "$1 finished: $(echo "$line" | sed 's/ *<--.*//; s/^ *//')"
  else
    fail "$1 did not finish (got '$line')"
  fi
}
check_course Slalom -450/500 '^ *[0-9]+:[0-9]{2}:[0-9]{2}\.[0-9]{2} <-- '
check_course Freestyle 0/500 '^ *-?[0-9]+ <-- '
check_course "Tree Slalom" 416/500 '^ *[0-9]+:[0-9]{2}:[0-9]{2}\.[0-9]{2} <-- '

# High scores through the SDL frontend's file storage: a first finish is
# written to entpack.ini, and a run slower than a full top 10 is "try again".
scores="$out/test-scores.ini"
rm -f "$scores"
r="$(SKIFREE_SCORES="$scores" "$sdl" --headless --seed 5 --ticks 1000 --script "0:warp=-450/500,$downs")"
if echo "$r" | grep -q "that's you" && grep -qE '^SS=-[0-9]+ $' "$scores"; then
  pass "high score saved: $(grep '^SS=' "$scores")"
else
  fail "high score not saved: $(cat "$scores" 2>/dev/null)"
fi
printf '[Ski]\nSS=%s\n' "$(printf -- '-1000 %.0s' 1 2 3 4 5 6 7 8 9 10)" > "$scores"
before="$(cat "$scores")"
r="$(SKIFREE_SCORES="$scores" "$sdl" --headless --seed 5 --ticks 1000 --script "0:warp=-450/500,$downs")"
if echo "$r" | grep -q "try again" && [[ "$(echo "$r" | grep -c '0:00:01.00')" == 10 ]] &&
   [[ "$(cat "$scores")" == "$before" ]]; then
  pass "full top 10: try again, list unchanged"
else
  fail "full top 10: $(echo "$r" | tail -3)"
fi

# Screenshots (inspect by eye): the start screen and the Yeti eating.
"$sdl" --headless --seed 1 --ticks 1 --screenshot "$out/test-start.bmp" >/dev/null &&
  pass "screenshot $out/test-start.bmp" || fail "start screenshot"
"$sdl" --headless --seed 7 --ticks 70 --script "0:warp=0/31200,0:down" --screenshot "$out/test-yeti.bmp" >/dev/null &&
  pass "screenshot $out/test-yeti.bmp" || fail "yeti screenshot"

if [[ $fails -ne 0 ]]; then
  echo "$fails test(s) failed" >&2
  exit 1
fi
echo "all tests passed"
