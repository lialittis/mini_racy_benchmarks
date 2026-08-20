#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${CASE_DIR}/../.." && pwd)"
OUT_DIR="${1:-${REPO_ROOT}/artifacts/matmul_v3_probe}"

"${SCRIPT_DIR}/build_kernel.sh" "${OUT_DIR}"
"${SCRIPT_DIR}/extract_optimized_ir.sh" "${OUT_DIR}"
python3 "${SCRIPT_DIR}/check_missing_pipe_v_barrier.py" \
  "${OUT_DIR}/optimized_ir/matmul_v3_0.ll" \
  "${OUT_DIR}/optimized_ir/matmul_v3_65536.ll"
