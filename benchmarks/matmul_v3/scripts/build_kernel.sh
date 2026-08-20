#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${CASE_DIR}/../.." && pwd)"

CANN_ROOT="${CANN_ROOT:-/usr/local/Ascend/cann-9.0.0}"
OUT_DIR="${1:-${REPO_ROOT}/artifacts/matmul_v3_probe}"
CONFIG="${CASE_DIR}/config/kernel_compile.json"
OP_IMPL="${CANN_ROOT}/opp/built-in/op_impl/ai_core/tbe/impl/ops_nn/dynamic/mat_mul_v3.py"

test -f "${CANN_ROOT}/set_env.sh" || { echo "missing ${CANN_ROOT}/set_env.sh" >&2; exit 1; }
test -x "${CANN_ROOT}/bin/asc_opc" || { echo "missing ${CANN_ROOT}/bin/asc_opc" >&2; exit 1; }
test -f "${OP_IMPL}" || { echo "missing ${OP_IMPL}" >&2; exit 1; }

# shellcheck disable=SC1090
source "${CANN_ROOT}/set_env.sh"
mkdir -p "${OUT_DIR}"

"${CANN_ROOT}/bin/asc_opc" "${OP_IMPL}" \
  --main_func=mat_mul_v3 \
  --input_param="${CONFIG}" \
  --soc_version=Ascend310P3 \
  --output="${OUT_DIR}/kernel" \
  --impl_mode=high_performance,optional \
  --op_mode=dynamic \
  --op_debug_config=dump_cce \
  --debug_dir="${OUT_DIR}/debug"

find "${OUT_DIR}/kernel" -maxdepth 1 -type f -printf '%f\n' | sort
