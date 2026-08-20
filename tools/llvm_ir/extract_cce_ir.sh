#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: extract_cce_ir.sh <source.cce> [output-dir] [-- <extra-ccec-args>]

Environment:
  CANN_ROOT          CANN installation root (default: /usr/local/Ascend/cann-9.0.0)
  CCEC_ARCH          AI Core architecture (default: dav-m200)
  CCEC_OPT_LEVEL     Optimization level without -O (default: 2)
  CCEC_SANITIZER     Set to 1 to add --cce-enable-sanitizer -g (default: 0)
EOF
}

if [[ $# -lt 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  [[ $# -ge 1 ]] && exit 0 || exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SOURCE="$(realpath "$1")"
shift

name="$(basename "${SOURCE}" .cce)"
if [[ $# -gt 0 && "$1" != "--" ]]; then
  OUT_DIR="$1"
  shift
else
  OUT_DIR="${REPO_ROOT}/artifacts/llvm_ir/${name}"
fi
[[ "${1:-}" == "--" ]] && shift
EXTRA_ARGS=("$@")

CANN_ROOT="${CANN_ROOT:-/usr/local/Ascend/cann-9.0.0}"
CCEC_ARCH="${CCEC_ARCH:-dav-m200}"
CCEC_OPT_LEVEL="${CCEC_OPT_LEVEL:-2}"
CCEC_SANITIZER="${CCEC_SANITIZER:-0}"
CCEC="${CANN_ROOT}/bin/ccec"
LLVM_LINK="${CANN_ROOT}/tools/bisheng_compiler/bin/llvm-link"

test -f "${SOURCE}" || { echo "missing CCE source: ${SOURCE}" >&2; exit 1; }
test -x "${CCEC}" || { echo "missing CCEC: ${CCEC}" >&2; exit 1; }
test -x "${LLVM_LINK}" || { echo "missing llvm-link: ${LLVM_LINK}" >&2; exit 1; }
[[ "${CCEC_OPT_LEVEL}" =~ ^[0-3]$ ]] || { echo "CCEC_OPT_LEVEL must be 0-3" >&2; exit 1; }
[[ "${CCEC_SANITIZER}" == "0" || "${CCEC_SANITIZER}" == "1" ]] || {
  echo "CCEC_SANITIZER must be 0 or 1" >&2
  exit 1
}

mkdir -p "${OUT_DIR}"
BC_FILE="${OUT_DIR}/${name}_O${CCEC_OPT_LEVEL}.bc"
LL_FILE="${OUT_DIR}/${name}_O${CCEC_OPT_LEVEL}.ll"

COMMAND=(
  "${CCEC}" -c -emit-llvm "-O${CCEC_OPT_LEVEL}" "${SOURCE}"
  "--cce-aicore-arch=${CCEC_ARCH}" --cce-aicore-only --cce-auto-sync=off
  -mllvm -cce-aicore-fp-ceiling=2
  -mllvm -cce-aicore-record-overflow=false
  -mllvm -cce-aicore-jump-expand=true
  -mllvm -cce-aicore-mask-opt=false
  -mllvm -cce-aicore-long-call
)
if [[ "${CCEC_SANITIZER}" == "1" ]]; then
  COMMAND+=(--cce-enable-sanitizer -g)
fi
COMMAND+=("${EXTRA_ARGS[@]}" -o "${BC_FILE}")

{
  printf '%q ' "${COMMAND[@]}"
  printf '\n'
} > "${OUT_DIR}/compile.command.txt"

"${COMMAND[@]}"
"${LLVM_LINK}" -S "${BC_FILE}" -o "${LL_FILE}"
sha256sum "${SOURCE}" > "${OUT_DIR}/SOURCE_SHA256SUM"
(
  cd "${OUT_DIR}"
  sha256sum "$(basename "${BC_FILE}")" "$(basename "${LL_FILE}")" > SHA256SUMS
)

echo "bitcode: ${BC_FILE}"
echo "text IR: ${LL_FILE}"
