# 2026-08-19 Add, Softmax and GEMM Migration Record

## Scope

This record covers migration of three historical projects into the shared benchmark repository:

| Benchmark | Historical project | Versioned cases | Default matrix |
| --- | --- | ---: | ---: |
| Add | `/root/4_op_dev/2_verify_op/acl_execute_add_test` | 7 | 6 |
| SoftmaxV2 | `/root/4_op_dev/2_verify_op/acl_execute_softmax_test` | 7 | 5 |
| GEMM | `/root/4_blas/gemm_test` | 24 | 21 |

Every versioned case has a selected OM, the corresponding CCE, a manifest entry and an OM
SHA-256 entry. Cases that can fail to terminate are retained with `matrix_enabled: false`;
they are not silently discarded.

## Environment

- CANN/OPP: `9.0.0`, timestamp `20260428_134817545`
- CCEC: clang `15.0.5`, build timestamp `2026-04-25T15:46:25+08:00`
- mssanitizer: `26.0.0-3f55c5f8ac51bff79176f4e59b16f8e77ad2a5ab`
- Device: Ascend 310P3, driver/npu-smi `25.6.rc1.b010`
- Host: aarch64, Linux `6.6.0-145.3.17.150.oe2403sp3.aarch64`
- Python/NumPy: `3.11.15` / `2.4.4`
- CMake/G++: `3.22.1` / `11.4.0`

The original 2026-06-09 build records did not preserve a complete immutable environment
snapshot. The values above describe migration verification, not the historical build host.

## Selected inputs

| Input | SHA-256 |
| --- | --- |
| Add `config/operator.json` | `da76b7dd31a06b89f46b14a0b67fae34a3de2259388263dec27147d42f66b385` |
| Add baseline CCE | `3beba6ebda4b45ef29f97c63d1f6413a0b2fa82cf7b711f8c1a1a48fa69c888f` |
| Softmax `config/operator.json` | `60aa0db9928a027f255df1e58fb6821f31132fb9e349181325475418ac5c7b26` |
| Softmax baseline CCE | `5890f59df83be268260e70d72f2999bf91722a9401288284c72c37f4c9de760b` |
| GEMM `config/operator.json` | `25e7016c362dfc71c2e537fd3a012b3a2e99ce797abc386fae3059839f70abb0` |
| GEMM baseline CCE | `28dcc5fb620657bfff619083112c55fc82c7613d8578a10012d8b16164618d4a` |

Complete CCE paths are declared in each `benchmark.json`. Complete selected OM hashes are
authoritative in:

- `benchmarks/add/models/SHA256SUMS` (7 OM)
- `benchmarks/softmax/models/SHA256SUMS` (7 OM)
- `benchmarks/gemm/models/SHA256SUMS` (24 OM)

Baseline OM hashes are Add
`3cf9c485e4e63fa56bb677c0f3fae956609ba7e12d373226d567795bd8d5d073`,
Softmax `a50699adabcd7b30b99ecb68ed093353d03811544217e491324b7dea3cb32b47`
and GEMM `df58d7f67ac47670b8bad3581b533628ca0c2b943024ceedfc299d215785ee67`.

## Historical generation

The selected injected fixtures come from the `0909_retry` output directories. The historical
builder used this command shape for each project:

```bash
cd /root/Ascend-NPU-Memory-Exploits/DefenceTools/data_race_checker

RUN_STAMP=0909_retry tools/op_inject_and_mssan.sh \
  --project <historical-project> \
  --inject <historical-project>/run/out/corrupted_cce \
  --original <historical-project>/run/out/original_cce/<baseline>.cce
```

For each case the builder invoked ATC with the corresponding configuration:

```bash
atc --singleop=<project>/run/out/test_data/config/<operator>.json \
  --soc_version=Ascend310P3 \
  --output=<project>/run/out/op_models_<source-case>_injected_0909_retry \
  --op_debug_level=2
```

The CCEC shim replaced the ATC-generated source before invoking the real CCEC. Historical
source summaries remain at:

```text
/root/Ascend-NPU-Memory-Exploits/DefenceTools/data_race_checker/mssanitizer_results/logs/
  acl_execute_add_test_injected_0909_retry_summary.csv
  acl_execute_softmax_test_injected_0909_retry_summary.csv
  gemm_test_injected_0909_retry_summary.csv
```

The Add `mismatched_event_id` and Softmax `cross_core_shift_read` historical runs failed.
All 23 selected GEMM injected builds were recorded as `ok`. Historical hazard counts are
supporting evidence only; migration verification is authoritative for the new runner.

## Runner migration

- MatMul, Add and Softmax now share `mrb_op_runner`, which receives operator type and repeated
  dtype/shape tensor specifications on the command line.
- GEMM uses `mrb_gemm_runner`, preserving `aclblasGemmEx` with `alpha=2`, `beta=1`.
- `tools/bench.py` now generates inputs by manifest dtype and supports Add, MatMul, Softmax
  and GEMM references.
- Timeout handling starts each sanitizer invocation in a new process group and terminates the
  complete group on timeout.
- `matrix_enabled: false` separates explicit nontermination tests from the default matrix.

## Verification commands

```bash
cd /root/mini_racy_benchmarks
./scripts/build.sh

./scripts/run_case.sh baseline --tool none --run-id migration_matmul_baseline_none_20260819
MRB_BENCHMARK=add ./scripts/run_case.sh baseline --tool none \
  --run-id migration_add_baseline_none_20260819
MRB_BENCHMARK=softmax ./scripts/run_case.sh baseline --tool none \
  --run-id migration_softmax_baseline_none_20260819
MRB_BENCHMARK=gemm ./scripts/run_case.sh baseline --tool none \
  --run-id migration_gemm_baseline_none_20260819

MRB_BENCHMARK=add ./scripts/run_matrix.sh --tool racecheck \
  --run-id migrated_add_final_20260819
MRB_BENCHMARK=softmax ./scripts/run_matrix.sh --tool racecheck \
  --run-id migrated_softmax_verified_20260819
MRB_BENCHMARK=gemm ./scripts/run_matrix.sh --tool racecheck \
  --run-id migrated_gemm_verified_20260819
```

## Verification results

All four direct baseline runs without sanitizer initially produced exact reference output.

Add final default matrix:

| Case | Hazards | Mismatch |
| --- | --- | ---: |
| baseline | `UB_RAW=2` | 0/128 |
| no_v_to_mte3 | `UB_RAW=2` | 0/128 |
| rm_final_barrier | none | 0/128 |
| no_mte2_to_v | `UB_RAW=3, UB_WAW=1` | 0/128 |
| oversized_gm_to_ub | `UB_WAW=1` | 0/128 |
| v_to_wrong_pipe | `UB_RAW=2` | 0/128 |

Result: PASS. Raw report:
`artifacts/runs/migrated_add_final_20260819/matrix_summary.md`.

Softmax final default matrix:

| Case | Hazards | Mismatch |
| --- | --- | ---: |
| baseline | `UB_RAW=2, UB_WAW=1` | 0/128 |
| cross_core_mod4 | `GM_WAW=4, UB_RAW=2, UB_WAW=1` | 93/128 |
| no_v_to_s | `UB_RAW=3, UB_WAW=1` | 0/128 |
| no_s_to_v | `UB_RAW=2, UB_WAW=1` | 0/128 |
| no_exp_barrier | `UB_RAW=3, UB_WAW=2` | 0/128 |

Result: PASS. Raw report:
`artifacts/runs/migrated_softmax_verified_20260819/matrix_summary.md`.

After synchronizing the migration into the Git working tree, the runner was rebuilt and one
racecheck baseline smoke test was repeated for each shared-runner benchmark:

| Benchmark | Hazards | Mismatch | Raw report |
| --- | --- | ---: | --- |
| MatMul | none | 0 | `artifacts/runs/20260819T213614Z_378432/matrix_summary.md` |
| Add | `UB_RAW=2` | 0 | `artifacts/runs/20260819T213628Z_378629/matrix_summary.md` |
| Softmax | `UB_RAW=2, UB_WAW=1` | 0 | `artifacts/runs/20260819T213644Z_378827/matrix_summary.md` |

All three target-tree smoke tests passed.

The first GEMM probe and the next full run showed exact baseline output and stable hazard types
for the terminating injections. A later verification run, after deliberately executing
nonterminating scalar/event cases, produced `254/256` baseline mismatches without a reported
hazard; an isolated retry produced `256/256` mismatches, and a no-sanitizer retry produced
`254/256` mismatches. Earlier correct baseline output SHA-256 was
`a9ffbe606be6aea3c51cf45af69ce5c0367b9674884dcaf523dcb72530a0e859`;
the later output hashes differed.

This establishes a run-state-dependent GEMM baseline failure in the current device session.
It must not be hidden by changing baseline `output_policy` from `match`. A clean-device
rerun is required before declaring the GEMM default matrix fully verified.

## Manual negative cases

| Benchmark | Case | Reason excluded from default matrix |
| --- | --- | --- |
| Add | `mismatched_event_id` | historical run failed |
| Softmax | `cross_core_shift_read` | last block reads beyond input; historical run failed |
| Softmax | `mismatched_event_id` | current run timed out after stream creation |
| GEMM | `no_alpha_scalar_wait` | one run completed with hazards; a later run timed out |
| GEMM | `aggressive_sync_strip` | current run timed out |
| GEMM | `l45_no_beta_scalar_wait` | one run completed with hazards; a later run timed out |

These cases can be run explicitly with `run_case.sh`. The manifest keeps a timeout and the
orchestrator now cleans up the entire process group.

## Known limitations and next checks

1. Reboot or administratively reset the otherwise idle device, then run GEMM baseline three
   times without sanitizer and three times with racecheck before running any manual case.
2. If clean-device GEMM baseline is stable, run the 21-case default matrix and preserve a PASS
   report. Then execute each manual case last, followed by another baseline check.
3. If clean-device GEMM baseline still varies, compare CCE dependency fingerprints and test a
   separately rebuilt sanitizer baseline control OM; do not promote a permissive output policy.
4. Repeat Add and Softmax baselines to determine whether their existing UB hazard counts are
   stable. They are allowed but not required because baseline is not a positive fault case.
5. The historical build environment is incomplete; rebuilding any selected OM requires a new
   record with current ATC/CCEC logs and hashes.
