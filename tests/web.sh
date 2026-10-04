#!/usr/bin/env bash
# The game core compiled to wasm must play exactly the same games as the
# native build: same traces, high-score lists and state hashes. Run via
# `make web-test`.
#
#   tests/web.sh path/to/native/skifree-sim path/to/wasm/skifree-sim
set -euo pipefail

native="$1"
wasm="$2"
fails=0

pass() { echo "ok: $*"; }
fail() { echo "FAIL: $*" >&2; fails=$((fails + 1)); }

# Run the same arguments through both builds and compare everything printed.
same() { # name args...
  local name="$1" a b
  shift
  a="$("$native" "$@" 2>&1)" || true
  b="$("$wasm" "$@" 2>&1)" || true
  if [[ -n "$a" && "$a" == "$b" ]]; then
    pass "$name ($(echo "$a" | tail -1))"
  else
    fail "$name differs:"
    diff <(echo "$a") <(echo "$b") | head -10 >&2
  fi
}

# The golden-hash game from tests/run.sh.
script="0:down,40:left,80:right,120:insert,200:down,300:click,301:click,400:pgdn,500:end,600:down"
golden="224d8c49"
r="$("$wasm" --headless --seed 1 --ticks 1000 --script "$script" --hash)"
[[ "$r" == "hash $golden" ]] && pass "golden hash" || fail "golden hash: got '$r', want 'hash $golden'"

same "yeti at 2000 m" --headless --seed 7 --ticks 120 --script "0:warp=0/31200,0:down" --trace 10 --hash
downs="$(seq 0 25 6000 | sed 's/$/:down/' | paste -sd, -)"
same "Slalom" --headless --seed 5 --ticks 6000 --script "0:warp=-450/500,$downs" --trace 500 --hash
same "Freestyle" --headless --seed 5 --ticks 6000 --script "0:warp=0/500,$downs" --trace 500 --hash
same "Tree Slalom" --headless --seed 5 --ticks 6000 --script "0:warp=416/500,$downs" --trace 500 --hash

# 2 x 30000 ticks of random play, including restarts and fast mode.
for seed in 42 7; do
  a="$(tests/fuzz.sh "$native" "$seed")"
  b="$(tests/fuzz.sh "$wasm" "$seed")"
  [[ "$a" == "$b" ]] && pass "random play, seed $seed ($(echo "$a" | grep '^hash'))" ||
    fail "random play, seed $seed: native '$a' wasm '$b'"
done

if [[ $fails -ne 0 ]]; then
  echo "$fails test(s) failed" >&2
  exit 1
fi
echo "wasm matches native"
