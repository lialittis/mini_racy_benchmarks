#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${CASE_DIR}/../.." && pwd)"

CANN_ROOT="${CANN_ROOT:-/usr/local/Ascend/cann-9.0.0}"
OUT_DIR="${1:-${REPO_ROOT}/artifacts/matmul_v3_probe}"
IR_DIR="${OUT_DIR}/optimized_ir"
LLVM_LINK="${CANN_ROOT}/tools/bisheng_compiler/bin/llvm-link"
MK_FILE="$(find "${OUT_DIR}/debug" -type f -name '*.mk' -print -quit)"

test -n "${MK_FILE}" || { echo "no compiler .mk found under ${OUT_DIR}/debug" >&2; exit 1; }
test -x "${LLVM_LINK}" || { echo "missing ${LLVM_LINK}" >&2; exit 1; }
mkdir -p "${IR_DIR}"

for key in 0 65536; do
  line="$(awk -v key="${key}" \
    'index($0, "bisheng -c ") && index($0, "-DTILING_KEY_VAR=" key "UL") { print; exit }' \
    "${MK_FILE}")"
  test -n "${line}" || { echo "no compile command for tiling key ${key}" >&2; exit 1; }

  # Keep the primary compiler invocation and emit LLVM bitcode with identical flags.
  line="${line%% 2>/dev/null*}"
  line="${line#"${line%%[![:space:]]*}"}"
  line="${line#/usr/bin/ccache }"
  line="${line/\/bisheng -c /\/bisheng -emit-llvm -c }"

  old_output="$(printf '%s\n' "${line}" | sed -n 's/.* -o \([^ ]*\.o\) .*/\1/p')"
  test -n "${old_output}" || { echo "cannot parse output from tiling key ${key} command" >&2; exit 1; }

  bc_file="${IR_DIR}/matmul_v3_${key}.bc"
  ll_file="${IR_DIR}/matmul_v3_${key}.ll"
  line="${line/ -o ${old_output} / -o ${bc_file} }"
  printf '%s\n' "${line}" > "${IR_DIR}/compile_${key}.command.txt"
  eval "${line}"
  "${LLVM_LINK}" -S "${bc_file}" -o "${ll_file}"
  echo "generated ${ll_file}"
done
