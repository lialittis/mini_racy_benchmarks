# Change Log

## 2026-08-20

- Added a reproducible Ascend310P3 MatMulV3 dynamic-kernel build and optimized-IR inspection probe.
- Checked tiling keys `0` and `65536` for the `M -> V` synchronization, L0C-to-UB move, and
  subsequent VMULS sequence without an intervening `PIPE_V` barrier.
- Documented why preprocessed `.i` text alone is insufficient and separated static template evidence
  from unproven runtime address overlap or racecheck behavior.
- Recorded the local ACLNN dispatch limitation: generic Matmul selected MatMulV2, while the official
  MatMulV3 WeightNz example failed workspace setup with error `161002`.
- Added a reusable two-stage CCEC bitcode/text LLVM IR extractor for legacy TBE-generated CCE.
- Verified O0, O2, and sanitizer IR generation and all seven MatMul baseline/injection CCE sources.
- Confirmed that the `l46_rm_barrier_v` and `l63_no_m_to_v` injection differences remain visible in
  optimized LLVM IR.

## 2026-08-19

- Migrated Add, SoftmaxV2, and GEMM baselines, injected CCE sources, and selected OM fixtures.
- Replaced the MatMul-only executable with a dtype/shape-parameterized ACL op runner and added
  a parameterized `aclblasGemmEx` runner.
- Generalized deterministic data generation and NumPy references for Add, MatMul, Softmax, and
  GEMM workloads.
- Added process-group timeout cleanup and explicit manual-only cases for models that can fail
  to terminate.
- Added per-benchmark manifests, SHA-256 inventories, provenance, and migration verification
  records.
- Initialized `/root/mini_racy_benchmarks` as a Git repository.
- Migrated the MatMul baseline, six fault-injection CCE sources, and seven selected OM fixtures.
- Replaced fixed working-directory behavior with a parameterized ACL runner.
- Added deterministic input generation, per-case isolation, racecheck orchestration, NumPy verification, and JSON/Markdown reports.
- Separated versioned experiment definitions from ignored build and runtime artifacts.
- Verified the migrated project with a full seven-case CANN 9.0 racecheck matrix on Ascend 310P3.
- Added required/allowed hazard sets and match/mismatch/either output policies to represent schedule-dependent race behavior without weakening each case's required signal.
- Recorded intermittent UB WAR observations from the baseline instruction sequence and preserved every raw report under ignored run artifacts.
- Added a standalone analysis of MatMul intermittent hazards, nondeterministic outputs, likely causes, and planned follow-up checks.
- Added separate guides for baseline CCE generation, fault injection with CCEC recompilation and ATC repackaging, model generation, and complete testing.
- Added a repository-local CCEC injection shim, a reusable generation-record template, and an audited provenance record for the selected MatMul models.
- Expanded `.gitignore` for CMake, ATC/CCEC, mssanitizer, Python, editor, and crash artifacts without excluding versioned CCE or OM fixtures.
