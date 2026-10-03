#!/usr/bin/env bash
# Random play for 30000 ticks (20 minutes of game time), including restarts
# and fast mode. Used by `make sanitize`; fails on a crash or sanitizer report.
#
#   tests/fuzz.sh path/to/skifree-sim [seed]
set -euo pipefail

sim="$1"
seed="${2:-42}"

script="$(python3 - "$seed" <<'EOF'
import random, sys
random.seed(int(sys.argv[1]))
acts = ["left", "right", "down", "down", "down", "up", "insert", "click",
        "home", "end", "pgup", "pgdn", "char=f"]
out = []
for t in range(0, 30000, 3):
    if random.random() < 0.5:
        out.append(f"{t}:{random.choice(acts)}")
    if random.random() < 0.002:
        out.append(f"{t}:f2")
print(",".join(out))
EOF
)"

out="$("$sim" --headless --ticks 30000 --seed "$seed" --script "$script" --trace 10000 --hash 2>&1)"
echo "$out" | grep -E '^(tick|hash)'
if echo "$out" | grep -qiE 'sanitizer|runtime error|assertion failed'; then
  echo "$out" | grep -iE -A20 'sanitizer|runtime error|assertion failed' | head -40
  echo "FAIL: fuzz" >&2
  exit 1
fi
echo "ok: fuzz ($(echo "$out" | grep -c '^High Scores') course finishes)"
