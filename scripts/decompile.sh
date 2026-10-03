#!/usr/bin/env bash
# Run Ghidra headless auto-analysis on a binary and export the results as text.
#
#   scripts/decompile.sh [binary] [output-dir]
#
# Defaults to etc/original-binaries/ski32.exe -> analysis/ski32/. If <output-dir>/symbols.tsv exists,
# its names/signatures are applied before export. The Ghidra project is kept in
# build/ghidra/ (gitignored) so it can also be opened in the Ghidra GUI.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
bin="${1:-$root/etc/original-binaries/ski32.exe}"
name="$(basename "$bin" .exe)"
out="${2:-$root/analysis/$name}"

# First address of the statically linked C runtime; functions below it are game code.
crt_start="${CRT_START:-00406cd0}"

mkdir -p "$root/build/ghidra" "$out"

post=(-postScript CreateMissingFunctions.java)
if [[ -f "$out/symbols.tsv" ]]; then
  post+=(-postScript ApplySymbols.java "$out/symbols.tsv")
fi
post+=(-postScript ExportAnalysis.java "$out" "$crt_start")

ghidra-analyzeHeadless "$root/build/ghidra" skifree \
  -import "$bin" -overwrite \
  -scriptPath "$root/scripts/ghidra" \
  "${post[@]}"
