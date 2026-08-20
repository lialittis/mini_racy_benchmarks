#!/usr/bin/env python3
"""Find the MatMulV3 L0C-to-UB then VMULS sequence in optimized CCE LLVM IR."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

MAD = "llvm.hivm.MAD."
MOVE = "llvm.hivm.MOV.L0C32.TO.UB"
VMULS = "llvm.hivm.VMULS."
V_BARRIER = "llvm.hivm.BARRIER(i64 1)"
SET_M_TO_V = "llvm.hivm.SET.FLAG.REG(i64 2, i64 1"
WAIT_M_TO_V = "llvm.hivm.WAIT.FLAG.REG(i64 2, i64 1"


def inspect(path: Path, look_back: int, mad_look_back: int, look_ahead: int) -> int:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    candidates = 0
    missing = 0

    for move_index, line in enumerate(lines):
        if MOVE not in line:
            continue

        l0c_match = re.search(r"ptr addrspace\(5\) (%[A-Za-z0-9._-]+)", line)
        if l0c_match is None:
            continue
        l0c_pointer = l0c_match.group(1)
        mad_before = lines[max(0, move_index - mad_look_back) : move_index]
        has_mad_to_same_l0c = any(
            MAD in item and f"ptr addrspace(5) {l0c_pointer}," in item for item in mad_before
        )
        if not has_mad_to_same_l0c:
            continue

        before = lines[max(0, move_index - look_back) : move_index]
        has_m_to_v = any(SET_M_TO_V in item for item in before) and any(
            WAIT_M_TO_V in item for item in before
        )
        if not has_m_to_v:
            continue

        vmuls_index = next(
            (
                index
                for index in range(move_index + 1, min(len(lines), move_index + look_ahead + 1))
                if VMULS in lines[index]
            ),
            None,
        )
        if vmuls_index is None:
            continue

        candidates += 1
        has_v_barrier = any(V_BARRIER in item for item in lines[move_index + 1 : vmuls_index])
        state = "barrier-present" if has_v_barrier else "missing-PIPE_V-barrier"
        print(
            f"{path}: move={move_index + 1}, vmuls={vmuls_index + 1}, "
            f"MAD->L0C={l0c_pointer}, M->V-sync=yes, result={state}"
        )
        if not has_v_barrier:
            missing += 1

    if candidates == 0:
        print(f"{path}: no matching sequence found")
        return 1
    if missing == 0:
        print(f"{path}: all {candidates} matching sequences contain a PIPE_V barrier")
        return 1

    print(f"{path}: {missing}/{candidates} matching sequences lack a PIPE_V barrier")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ir", nargs="+", type=Path)
    parser.add_argument("--look-back", type=int, default=100)
    parser.add_argument("--mad-look-back", type=int, default=400)
    parser.add_argument("--look-ahead", type=int, default=320)
    args = parser.parse_args()

    return max(
        inspect(path, args.look_back, args.mad_look_back, args.look_ahead) for path in args.ir
    )


if __name__ == "__main__":
    raise SystemExit(main())
