# Change Log

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
