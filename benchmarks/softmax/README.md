# Softmax benchmark

This benchmark migrates the `SoftmaxV2` experiment from
`/root/4_op_dev/2_verify_op/acl_execute_softmax_test`.

- Operator: `SoftmaxV2`
- Shape: `[8,16] -> [8,16]`
- Dtype/format: FP32/ND
- Runnable matrix: baseline plus four terminating historical injections
- Disabled cases: `cross_core_shift_read` and `mismatched_event_id`, retained for explicit
  negative testing because they can fail to complete

Run the matrix with:

```bash
MRB_BENCHMARK=softmax ./scripts/run_matrix.sh --tool racecheck --run-id softmax_matrix
```
