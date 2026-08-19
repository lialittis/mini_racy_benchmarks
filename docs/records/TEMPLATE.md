# <Date> <Operator> Model Build Record

## Identity

- Run ID: `<run-id>`
- Git commit before build: `<sha>`
- Git worktree state: `<clean or describe changes>`
- Builder: `<person or automation>`
- Purpose: `<baseline refresh / new fault / CANN migration>`

## Environment

- Host architecture and kernel: `<uname -a>`
- Device and driver: `<npu-smi summary>`
- SoC: `<soc_version>`
- CANN/OPP: `<path and version.info>`
- CCEC: `<ccec --version>`
- mssanitizer: `<mssanitizer --version revision>`
- Python/NumPy: `<versions>`
- CMake/G++: `<versions>`

Raw snapshot: `artifacts/model_builds/<run-id>/environment.txt`.

## Inputs

| Input | SHA-256 |
| --- | --- |
| `benchmarks/<operator>/config/operator.json` | `<sha256>` |
| baseline CCE | `<sha256>` |
| injected CCE(s) | `<sha256>` |

## Commands

Record the exact baseline ATC command and exact injected ATC/CCEC shim command. Do not replace important flags with `...`.

```bash
<commands>
```

## Products

| Case | CCE SHA-256 | OM SHA-256 | CCEC injection confirmed |
| --- | --- | --- | --- |
| `baseline` | `<sha>` | `<sha>` | N/A |
| `<case>` | `<sha>` | `<sha>` | `<yes/no>` |

## Verification

- Direct baseline run: `<result>`
- Baseline NumPy comparison: `<result>`
- Baseline racecheck: `<result>`
- Full matrix report: `artifacts/runs/<run-id>/matrix_summary.md`
- Matrix result: `<PASS/FAIL>`

| Case | Required/allowed hazards | Observed hazards | Output comparison | Status |
| --- | --- | --- | --- | --- |
| `baseline` | `<expectation>` | `<observed>` | `<mismatches/total>` | `<PASS/FAIL>` |

## Promotion

- `benchmark.json` updated: `<yes/no>`
- `models/SHA256SUMS` verified: `<yes/no>`
- `CHANGELOG.md` updated: `<yes/no>`
- Raw artifacts retained at: `<path or retention note>`
- Known limitations: `<none or details>`
