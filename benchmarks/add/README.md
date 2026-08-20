# Add benchmark

This benchmark migrates the `Add` experiment from
`/root/4_op_dev/2_verify_op/acl_execute_add_test`.

- Operator: `Add`
- Shape: `[8,16] + [8,16] -> [8,16]`
- Dtype/format: INT32/ND
- Runnable matrix: baseline plus five successful historical injections
- Disabled case: `mismatched_event_id`, retained for explicit negative testing because the
  historical run did not complete successfully

Run the matrix with:

```bash
MRB_BENCHMARK=add ./scripts/run_matrix.sh --tool racecheck --run-id add_matrix
```
