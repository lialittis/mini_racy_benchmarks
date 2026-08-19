# Model Fixtures

每个目录只包含一个与 case 同名语义对应的 MatMul OM。基线模型来自原 `op_models`，故障模型来自各 `*_injected_0909_retry` 目录。

这些模型面向 Ascend 310P3，并作为可重复实验的固定输入进入 Git。更新模型时必须同时更新对应 CCE、`benchmark.json`、`SHA256SUMS`，并重新运行完整 racecheck 矩阵。

模型不得直接从临时 ATC 目录覆盖进来。构建、CCEC 注入、验证和提升步骤见[模型生成与完整测试](../../../docs/model_generation_and_testing.md)；当前七个模型的来源、哈希和历史限制见[生成记录](../../../docs/records/2026-06-09-matmul-0909-retry.md)。
