# MatMul FP16 Race Benchmark

固定计算为 `[16, 64] x [64, 1024] -> [16, 1024]`，输入输出均为 FP16，使用 8 个 AI Core block。输入由 `benchmark.json` 中的固定 seed 生成。

| Case | 注入 | 预期 racecheck | 预期数值 |
| --- | --- | --- | --- |
| `baseline` | 无 | 无 hazard；偶发允许 UB WAR | 必须精确匹配 |
| `cross_core_mod4` | `block_idx % 4` 复用输出区 | GM WAW | 不匹配 |
| `cross_core_large_offset` | GM 写长度从 8 扩为 16 | GM WAW | 不匹配 |
| `l43_no_m_to_v` | 删除 MTE2 到 Vector event | 必有 UB RAW、UB WAW；可能附带 UB WAR | 允许波动 |
| `l46_rm_barrier_v` | 删除循环头 Vector barrier | 无 hazard 或 UB WAR（调度相关） | 允许波动 |
| `l54_no_v_to_m` | 删除 Vector 到 MTE3 event | 必有 UB RAW；可能附带 UB WAR | 允许波动 |
| `l63_no_m_to_v` | 删除 Matrix 到 Vector event | 必有 UB WAR | 允许波动 |

`l43_no_m_to_v` 是历史文件名，实际删除的是 `PIPE_MTE2 -> PIPE_V` 同步。`l46_rm_barrier_v` 的删除已经进入 OM；重复运行中 UB WAR 可能出现也可能不出现，因此按调度敏感 case 管理。

模型来自原目录中 2026-06-09 的 `*_injected_0909_retry` 最终版本。OM 是本 benchmark 的固定实验输入，不写入 `artifacts/`；运行产生的 instrumentation 和结果必须写入 `artifacts/runs/`。

预编译 OM 保留了生成时的绝对调试路径，因此 sanitizer 回溯可能显示旧工程 `/root/4_op_dev/...`。`summary.json` 中的 `kernel_source` 始终映射到本仓库对应 CCE，实验定义不依赖旧目录存在。

## 生成与测试

- 从 `config/operator.json` 导出基准源码：[基准 CCE 生成](../../docs/cce_generation.md)
- 制作注入源码并构建新 OM：[故障注入与 OM 回灌](../../docs/fault_injection.md)
- 完整模型命令和回归门槛：[模型生成与完整测试](../../docs/model_generation_and_testing.md)
- 当前模型来源和 SHA-256：[2026-06-09 生成记录](../../docs/records/2026-06-09-matmul-0909-retry.md)
