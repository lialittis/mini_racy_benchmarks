# GEMM benchmark

This benchmark migrates the `GEMM` experiment from `/root/4_blas/gemm_test`.

- Operation: `C_out = 2 * A * B + C`
- Shapes: `A=[16,16]`, `B=[16,16]`, `C=[16,16]`
- Dtype/format: FP16/ND
- Runner: parameterized `aclblasGemmEx` runner
- Cases: baseline, twenty systematic synchronization mutations, and three historical
  line-specific mutations
- Default matrix: baseline plus twenty terminating injections; three potentially nonterminating
  cases remain available for explicit negative testing

Run the matrix with:

```bash
MRB_BENCHMARK=gemm ./scripts/run_matrix.sh --tool racecheck --run-id gemm_matrix
```
