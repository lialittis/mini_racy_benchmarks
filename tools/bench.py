#!/usr/bin/env python3
"""Build, run, validate, and summarize mini racy benchmarks."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import signal
import shutil
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
BENCHMARK_NAME = os.environ.get("MRB_BENCHMARK", "matmul")
BENCHMARK_DIR = ROOT / "benchmarks" / BENCHMARK_NAME
MANIFEST_PATH = BENCHMARK_DIR / "benchmark.json"
ARTIFACTS_DIR = ROOT / "artifacts"
BUILD_DIR = ROOT / "build"
HAZARD_PATTERN = re.compile(r"Potential (RAW|WAR|WAW) hazard detected at ([A-Za-z0-9_]+)")
DTYPES = {
    "float16": np.dtype(np.float16),
    "float32": np.dtype(np.float32),
    "int8": np.dtype(np.int8),
    "int32": np.dtype(np.int32),
}


def load_manifest() -> dict[str, Any]:
    with MANIFEST_PATH.open(encoding="utf-8") as stream:
        return json.load(stream)


def get_case(manifest: dict[str, Any], case_id: str) -> dict[str, Any]:
    for case in manifest["cases"]:
        if case["id"] == case_id:
            return case
    available = ", ".join(case["id"] for case in manifest["cases"])
    raise ValueError(f"unknown case '{case_id}'; available cases: {available}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def data_dir(manifest: dict[str, Any]) -> Path:
    return ARTIFACTS_DIR / "data" / manifest["id"]


def generate_data(manifest: dict[str, Any], force: bool = False) -> Path:
    output_dir = data_dir(manifest)
    input_paths = [output_dir / f"input_{index}.bin" for index, _ in enumerate(manifest["tensors"]["inputs"])]
    if all(path.exists() for path in input_paths) and not force:
        return output_dir

    output_dir.mkdir(parents=True, exist_ok=True)
    config = manifest["data"]
    rng = np.random.default_rng(config["seed"])
    low = config["integer_low"]
    high = config["integer_high_exclusive"]
    for path, tensor in zip(input_paths, manifest["tensors"]["inputs"]):
        dtype = DTYPES[tensor["dtype"]]
        rng.integers(low, high, size=tensor["shape"]).astype(dtype).tofile(path)

    metadata = {"seed": config["seed"]}
    metadata.update({f"input_{index}_sha256": sha256(path) for index, path in enumerate(input_paths)})
    (output_dir / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return output_dir


def expected_output(manifest: dict[str, Any], inputs: Path) -> np.ndarray:
    tensors = manifest["tensors"]
    values = [
        np.fromfile(inputs / f"input_{index}.bin", dtype=DTYPES[tensor["dtype"]]).reshape(tensor["shape"])
        for index, tensor in enumerate(tensors["inputs"])
    ]
    reference = manifest["reference"]
    kind = reference["kind"]
    if kind == "add":
        result = values[0] + values[1]
    elif kind == "matmul":
        result = np.matmul(values[0], values[1])
    elif kind == "softmax":
        axis = reference.get("axis", -1)
        shifted = values[0] - np.max(values[0], axis=axis, keepdims=True)
        exponentials = np.exp(shifted)
        result = exponentials / np.sum(exponentials, axis=axis, keepdims=True)
    elif kind == "gemm":
        result = reference["alpha"] * np.matmul(values[0], values[1]) + reference["beta"] * values[2]
    else:
        raise ValueError(f"unsupported reference kind: {kind}")
    return result.astype(DTYPES[tensors["output"]["dtype"]])


def configure_and_build() -> None:
    cann_root = os.environ.get("CANN_ROOT", "/usr/local/Ascend/cann-9.0.0")
    subprocess.run(
        ["cmake", "-S", str(ROOT), "-B", str(BUILD_DIR), f"-DCANN_ROOT={cann_root}"],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(BUILD_DIR), "--parallel"], check=True)


def new_run_id() -> str:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return f"{timestamp}_{os.getpid()}"


def parse_hazards(report_path: Path) -> Counter[str]:
    counts: Counter[str] = Counter()
    if not report_path.exists():
        return counts
    for match in HAZARD_PATTERN.finditer(report_path.read_text(encoding="utf-8", errors="replace")):
        hazard_kind, memory_space = match.groups()
        counts[f"{memory_space}_{hazard_kind}"] += 1
    return counts


def compare_output(manifest: dict[str, Any], inputs: Path, output_path: Path) -> dict[str, Any]:
    if not output_path.exists():
        return {"exists": False, "exact_match": False, "mismatched_elements": None, "total_elements": None}
    expected = expected_output(manifest, inputs)
    actual = np.fromfile(output_path, dtype=DTYPES[manifest["tensors"]["output"]["dtype"]])
    if actual.size != expected.size:
        return {
            "exists": True,
            "exact_match": False,
            "mismatched_elements": None,
            "total_elements": int(expected.size),
            "actual_elements": int(actual.size),
        }
    actual = actual.reshape(expected.shape)
    comparison = manifest.get("comparison", {"mode": "exact"})
    if comparison["mode"] == "allclose":
        matches = np.isclose(
            actual,
            expected,
            rtol=comparison.get("rtol", 0.0),
            atol=comparison.get("atol", 0.0),
            equal_nan=comparison.get("equal_nan", False),
        )
    else:
        matches = actual == expected
    mismatches = int(np.count_nonzero(~matches))
    max_abs_error = float(np.max(np.abs(actual.astype(np.float32) - expected.astype(np.float32))))
    return {
        "exists": True,
        "exact_match": mismatches == 0,
        "mismatched_elements": mismatches,
        "total_elements": int(expected.size),
        "max_abs_error": max_abs_error,
    }


def tensor_spec(tensor: dict[str, Any]) -> str:
    return f"{tensor['dtype']}:{','.join(str(dimension) for dimension in tensor['shape'])}"


def runner_command(
    manifest: dict[str, Any],
    runner_path: Path,
    model_dir: Path,
    inputs: Path,
    result_dir: Path,
    device: int,
) -> list[str]:
    command = [
        str(runner_path),
        "--model-dir",
        str(model_dir),
        "--input-dir",
        str(inputs.resolve()),
        "--output-dir",
        str(result_dir.resolve()),
        "--acl-config",
        str((BENCHMARK_DIR / "config" / "acl.json").resolve()),
        "--device",
        str(device),
    ]
    if manifest.get("runner_kind", "op") == "gemm":
        reference = manifest["reference"]
        shape_a = manifest["tensors"]["inputs"][0]["shape"]
        shape_b = manifest["tensors"]["inputs"][1]["shape"]
        command.extend([
            "--m", str(shape_a[0]),
            "--n", str(shape_b[1]),
            "--k", str(shape_a[1]),
            "--alpha", str(reference["alpha"]),
            "--beta", str(reference["beta"]),
        ])
    else:
        command.extend(["--operator", manifest["operator"]])
        for tensor in manifest["tensors"]["inputs"]:
            command.extend(["--input-spec", tensor_spec(tensor)])
        command.extend(["--output-spec", tensor_spec(manifest["tensors"]["output"])])
    return command


def run_case(
    manifest: dict[str, Any],
    case: dict[str, Any],
    tool: str,
    device: int,
    run_root: Path,
) -> dict[str, Any]:
    runner_path = BUILD_DIR / "bin" / manifest["runner_binary"]
    if not runner_path.exists():
        raise FileNotFoundError(f"runner not built: {runner_path}; run ./scripts/build.sh first")

    inputs = generate_data(manifest)
    case_dir = run_root / case["id"]
    result_dir = case_dir / "result"
    case_dir.mkdir(parents=True, exist_ok=True)
    result_dir.mkdir(exist_ok=True)
    os.chmod(case_dir, 0o750)
    os.chmod(result_dir, 0o750)

    model_dir = (BENCHMARK_DIR / case["model_dir"]).resolve()
    command = runner_command(manifest, runner_path, model_dir, inputs, result_dir, device)

    report_path = case_dir / f"{tool}.log"
    if tool != "none":
        sanitizer = shutil.which("mssanitizer")
        if sanitizer is None:
            raise FileNotFoundError("mssanitizer is not on PATH; source the CANN set_env.sh first")
        command = [sanitizer, f"--tool={tool}", f"--log-file={report_path}", "--", *command]

    console_path = case_dir / "console.log"
    timed_out = False
    with console_path.open("w", encoding="utf-8") as console:
        process = subprocess.Popen(
            command,
            cwd=case_dir,
            stdout=console,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            return_code = process.wait(timeout=case.get("timeout_seconds", manifest.get("timeout_seconds")))
        except subprocess.TimeoutExpired:
            return_code = 124
            timed_out = True
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()

    hazards = parse_hazards(report_path)
    output = compare_output(manifest, inputs, result_dir / "output_0.bin")
    detected_types = set(hazards)
    required_hazards = case.get("required_hazards", manifest.get("required_hazards", []))
    allowed_hazards = case.get("allowed_hazards", manifest.get("allowed_hazards", required_hazards))
    required_types = set(required_hazards) if tool == "racecheck" else detected_types
    allowed_types = set(allowed_hazards) if tool == "racecheck" else detected_types
    hazard_expectation_met = required_types.issubset(detected_types) and detected_types.issubset(allowed_types)
    output_policy = case["output_policy"]
    if output_policy == "match":
        output_expectation_met = output["exists"] and output["exact_match"]
    elif output_policy == "mismatch":
        output_expectation_met = output["exists"] and not output["exact_match"]
    elif output_policy == "either":
        output_expectation_met = output["exists"]
    else:
        output_expectation_met = True
    execution_policy = case.get("execution_policy", "success")
    execution_expectation_met = (
        return_code == 0 if execution_policy == "success"
        else return_code != 0 if execution_policy == "failure"
        else True
    )
    passed = execution_expectation_met and hazard_expectation_met and output_expectation_met

    summary = {
        "benchmark": manifest["id"],
        "case": case["id"],
        "description": case["description"],
        "tool": tool,
        "device": device,
        "return_code": return_code,
        "timed_out": timed_out,
        "execution_policy": execution_policy,
        "execution_expectation_met": execution_expectation_met,
        "hazards": dict(sorted(hazards.items())),
        "required_hazards": required_hazards if tool == "racecheck" else None,
        "allowed_hazards": allowed_hazards if tool == "racecheck" else None,
        "hazard_expectation_met": hazard_expectation_met,
        "output": output,
        "output_policy": output_policy,
        "output_expectation_met": output_expectation_met,
        "passed": passed,
        "model_dir": str(model_dir.relative_to(ROOT)),
        "kernel_source": str((BENCHMARK_DIR / case["kernel_source"]).relative_to(ROOT)),
        "console": str(console_path.relative_to(ROOT)),
        "report": str(report_path.relative_to(ROOT)) if tool != "none" else None,
    }
    (case_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    mismatch_text = output["mismatched_elements"] if output["mismatched_elements"] is not None else "n/a"
    print(
        f"{case['id']}: {'PASS' if passed else 'FAIL'}; hazards={dict(hazards)}; "
        f"mismatches={mismatch_text}"
    )
    return summary


def write_matrix_report(run_root: Path, summaries: list[dict[str, Any]]) -> None:
    matrix = {
        "run_id": run_root.name,
        "passed": all(summary["passed"] for summary in summaries),
        "cases": summaries,
    }
    (run_root / "matrix_summary.json").write_text(json.dumps(matrix, indent=2) + "\n", encoding="utf-8")

    lines = [
        f"# Matrix Run {run_root.name}",
        "",
        "| Case | Status | Hazards | Output mismatches |",
        "| --- | --- | --- | ---: |",
    ]
    for summary in summaries:
        hazards = ", ".join(f"{name}={count}" for name, count in summary["hazards"].items()) or "none"
        output = summary["output"]
        mismatch = output["mismatched_elements"]
        mismatch_text = "n/a" if mismatch is None else f"{mismatch}/{output['total_elements']}"
        lines.append(
            f"| `{summary['case']}` | {'PASS' if summary['passed'] else 'FAIL'} | {hazards} | {mismatch_text} |"
        )
    lines.append("")
    (run_root / "matrix_summary.md").write_text("\n".join(lines), encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("list", help="list benchmark cases")

    prepare_parser = subparsers.add_parser("prepare", help="generate deterministic inputs")
    prepare_parser.add_argument("--force", action="store_true")

    subparsers.add_parser("build", help="configure and build the ACL runner")

    run_parser = subparsers.add_parser("run", help="run one benchmark case")
    run_parser.add_argument("case")
    run_parser.add_argument("--tool", choices=("none", "racecheck"), default="racecheck")
    run_parser.add_argument("--device", type=int, default=0)
    run_parser.add_argument("--run-id")

    matrix_parser = subparsers.add_parser("matrix", help="run every case and create a matrix report")
    matrix_parser.add_argument("--tool", choices=("none", "racecheck"), default="racecheck")
    matrix_parser.add_argument("--device", type=int, default=0)
    matrix_parser.add_argument("--run-id")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    manifest = load_manifest()

    if args.command == "list":
        for case in manifest["cases"]:
            scope = "matrix" if case.get("matrix_enabled", True) else "manual"
            print(f"{case['id']:<28} [{scope}] {case['description']}")
        return 0
    if args.command == "prepare":
        print(generate_data(manifest, force=args.force))
        return 0
    if args.command == "build":
        configure_and_build()
        return 0

    run_id = args.run_id or new_run_id()
    run_root = ARTIFACTS_DIR / "runs" / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    os.chmod(run_root, 0o750)

    if args.command == "run":
        summary = run_case(manifest, get_case(manifest, args.case), args.tool, args.device, run_root)
        write_matrix_report(run_root, [summary])
        print(f"report: {run_root / 'matrix_summary.md'}")
        return 0 if summary["passed"] else 1

    summaries = [
        run_case(manifest, case, args.tool, args.device, run_root)
        for case in manifest["cases"]
        if case.get("matrix_enabled", True)
    ]
    write_matrix_report(run_root, summaries)
    print(f"report: {run_root / 'matrix_summary.md'}")
    return 0 if all(summary["passed"] for summary in summaries) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (FileNotFoundError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
