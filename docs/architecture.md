# Architecture

## 设计目标

- 可读性：一个 benchmark manifest 可以回答“运行什么、故障在哪里、预期看到什么”。
- 可扩展性：执行器、benchmark 定义和实验编排相互独立，新增 case 不复制工程。
- 可实验性：一次运行拥有独立目录、确定性输入、原始日志、结构化摘要和数值对照。
- 可追溯性：精选 OM、对应 CCE、SHA-256 和来源说明共同进入版本控制。

## 分层边界

`benchmarks/<name>/` 是版本化实验定义。每个 case 必须在 manifest 中声明稳定 ID、模型目录、内核来源、预期 hazard 类型和预期数值行为。

`runner/` 只完成一次设备执行。它不选择 case、不解析 sanitizer 日志，也不创建实验编号。模型、输入、输出、ACL 配置和设备号均由命令行传入。MatMul、Add 和 Softmax 共享由 dtype/shape 参数化的 `mrb_op_runner`；GEMM 使用保持 `aclblasGemmEx` 语义的 `mrb_gemm_runner`。

`tools/bench.py` 是实验控制面，负责确定性数据、runner/mssanitizer 命令、数值比较、进程组超时清理和 JSON/Markdown 报告。shell 脚本只负责加载 CANN 环境并提供短命令入口。通过 `MRB_BENCHMARK=<name>` 选择 manifest。

`artifacts/` 是运行数据面。任何时间戳日志、生成输入、输出和临时 sanitizer 文件都只能出现在这里。

## 新增 MatMul 故障 case

1. 按[基准 CCE 生成](cce_generation.md)固定本次基准源码和构建环境。
2. 按[故障注入与 OM 回灌](fault_injection.md)制作最小 CCE diff，并通过 CCEC shim 构建临时 OM。
3. 按[模型生成与完整测试](model_generation_and_testing.md)验证临时 OM，再将其提升到 `benchmarks/matmul/models/<case-id>/`。
4. 在 `benchmark.json` 的 `cases` 中添加模型、CCE 和预期行为。
5. 更新 `models/SHA256SUMS` 和 `docs/records/` 生成记录。
6. 运行 `./scripts/run_case.sh <case-id>`，再运行完整矩阵避免基线回归。

## 新增算子 benchmark

新算子应创建自己的 `benchmarks/<operator>/`，包括 manifest、配置、内核和模型。普通 ACL 单算子优先复用 `mrb_op_runner`；只有 GEMM 这类使用专用 host API 的 workload 才增加 runner target。算子参考计算接入 `tools/bench.py`，不复制 sanitizer 编排代码。

可能超时、死等或预期失败的负向模型仍应版本化，但必须设置 `matrix_enabled: false`。它们只能通过 `run_case.sh` 显式执行，避免默认矩阵被非终止 case 阻塞。

## 结果判定

实验通过需要同时满足：执行结果符合 `execution_policy`、实际 hazard 类型包含 manifest 的 required 集合且不超出 allowed 集合、数值结果满足 manifest 的 match、mismatch、either 或 any 策略。hazard 数量保留在报告中观察，但不作为硬编码门槛，以允许 CANN instrumentation 版本变化。
