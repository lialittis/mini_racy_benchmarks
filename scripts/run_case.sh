#!/usr/bin/env bash

set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/common_env.sh"
python3 "$PROJECT_ROOT/tools/bench.py" prepare
python3 "$PROJECT_ROOT/tools/bench.py" run "$@"
