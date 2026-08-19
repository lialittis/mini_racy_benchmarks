#!/usr/bin/env bash

set -euo pipefail

REAL_CCEC=${CCEC_REAL:-}
INJECT_SOURCE=${CCEC_INJECT_SOURCE:-}
INJECT_LOG=${CCEC_INJECT_LOG:-/tmp/ccec_inject.log}
TARGET_REGEX=${CCEC_INJECT_TARGET_REGEX:-.*}

if [[ -z "$REAL_CCEC" || ! -x "$REAL_CCEC" ]]; then
  echo "[ccec_inject] CCEC_REAL is missing or not executable: $REAL_CCEC" >&2
  exit 127
fi
if [[ -n "$INJECT_SOURCE" && ! -f "$INJECT_SOURCE" ]]; then
  echo "[ccec_inject] CCEC_INJECT_SOURCE does not exist: $INJECT_SOURCE" >&2
  exit 2
fi

mkdir -p "$(dirname "$INJECT_LOG")"
{
  printf '\n[ccec_inject] begin pid=%s time=%s\n' "$$" "$(date -Is)"
  printf '[ccec_inject] real_ccec: %q\n' "$REAL_CCEC"
  printf '[ccec_inject] inject_source: %q\n' "$INJECT_SOURCE"
  printf '[ccec_inject] target_regex: %q\n' "$TARGET_REGEX"
  printf '[ccec_inject] incoming_args:'
  printf ' %q' "$@"
  printf '\n'
} >> "$INJECT_LOG"

if [[ -n "$INJECT_SOURCE" ]]; then
  for arg in "$@"; do
    case "$arg" in
      *te_*.cce)
        if [[ -f "$arg" && "$(basename "$arg")" =~ $TARGET_REGEX ]]; then
          python3 - "$INJECT_SOURCE" "$arg" <<'PY'
import re
import sys
from pathlib import Path

source = Path(sys.argv[1])
destination = Path(sys.argv[2])
text = source.read_text(encoding="utf-8")

# ATC metadata expects the generated kernel stem. Keep the injected body but
# rewrite any source stem before CCEC compiles the temporary ATC source.
source_stems = set(
    re.findall(r"(te_[A-Za-z0-9_]+_[0-9a-f]{64})(?=__kernel0|\.cce|\b)", text)
)
for source_stem in sorted(source_stems, key=len, reverse=True):
    text = text.replace(source_stem, destination.stem)

if not text.endswith("\n"):
    text += "\n"
destination.write_text(text, encoding="utf-8")
PY
          printf '[ccec_inject] injected %s into %s\n' "$INJECT_SOURCE" "$arg" >> "$INJECT_LOG"
        else
          printf '[ccec_inject] skipped candidate: %q\n' "$arg" >> "$INJECT_LOG"
        fi
        ;;
    esac
  done
fi

{
  printf '[ccec_inject] final_command: %q' "$REAL_CCEC"
  printf ' %q' "$@"
  printf '\n[ccec_inject] end pid=%s handoff=exec\n' "$$"
} >> "$INJECT_LOG"

exec "$REAL_CCEC" "$@"
