#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
CANN_ROOT=${CANN_ROOT:-/usr/local/Ascend/cann-9.0.0}

if [[ ! -f "$CANN_ROOT/set_env.sh" ]]; then
  echo "CANN environment script not found: $CANN_ROOT/set_env.sh" >&2
  exit 2
fi

source "$CANN_ROOT/set_env.sh"
export CANN_ROOT PROJECT_ROOT
