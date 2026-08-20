# 实验与生成指南

本文档目录同时描述两条路径：使用仓库内已经精选的 OM 复现实验，以及从单算子配置重新生成 CCE、制作故障并构建新 OM。建议按下列顺序阅读。

1. [基准 CCE 生成](cce_generation.md)：从 `operator.json` 调用 ATC/TBE，理解并保存 `.cce`、`.o`、编译 JSON 和 `.om`。
2. [故障注入与 OM 回灌](fault_injection.md)：修改 CCE，通过 CCEC shim 在 ATC 编译阶段替换内核，并由 ATC 完成 OM 封装。
3. [模型生成与测试](model_generation_and_testing.md)：记录环境，构建模型，执行 runner、NumPy 对照和 mssanitizer 完整矩阵。
4. [MatMul 偶发告警与非确定性分析](analysis/matmul_intermittency_and_nondeterminism.md)：汇总各 case 的动态波动、当前成因分析和后续检查计划。
5. [架构说明](architecture.md)：项目各层的职责、版本控制边界和新增 benchmark 规则。
6. [生成记录](records/)：进入版本控制的模型来源、命令、哈希和验证结论。

当前多算子迁移的具体来源和限制见
[Add、Softmax 与 GEMM 迁移记录](records/2026-08-19-add-softmax-gemm-migration.md)。

## 两类产物

| 内容 | 目录 | 是否进入 Git |
| --- | --- | --- |
| benchmark 定义、精选 CCE、精选 OM、生成记录 | `benchmarks/`、`docs/records/` | 是 |
| ATC/CCEC 临时文件、原始日志、测试输出 | `artifacts/` | 否 |

`artifacts/` 中的原始材料可以删除和重新生成；`docs/records/` 只保存足够审计一次模型提升的摘要。不要把一次临时 ATC 输出直接覆盖到 `benchmarks/*/models/`，必须先完成文档规定的验证和哈希更新。
