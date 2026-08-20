# Mini Racy Benchmarks

面向 Ascend AI Core 的小型、可复现故障基准集。项目将算子语义、故障注入内核、预编译模型、执行器和实验结果分层管理。

当前包含：

| Benchmark | 规格 | Case | 默认矩阵 |
| --- | --- | ---: | ---: |
| `matmul` | FP16 `[16,64] x [64,1024]` | 7 | 7 |
| `add` | INT32 `[8,16] + [8,16]` | 7 | 6 |
| `softmax` | FP32 `[8,16]` | 7 | 5 |
| `gemm` | FP16 `C=2*A*B+C`, `16x16` | 24 | 21 |

未进入默认矩阵的 case 仍保留 CCE 和 OM，用于可能超时或失败的显式负向测试。

## 快速开始

环境要求：Ascend 310P、CANN 9.0、CMake 3.16+、支持 C++11 的编译器，以及安装了 NumPy 的 Python 3。

```bash
cd /root/mini_racy_benchmarks
./scripts/build.sh

# MatMul 是默认 benchmark
./scripts/run_matrix.sh --run-id matmul_001

# 选择其他 benchmark
MRB_BENCHMARK=add ./scripts/run_matrix.sh --run-id add_001
MRB_BENCHMARK=softmax ./scripts/run_matrix.sh --run-id softmax_001
MRB_BENCHMARK=gemm ./scripts/run_matrix.sh --run-id gemm_001
```

默认使用 `/usr/local/Ascend/cann-9.0.0`。其他安装位置可通过 `CANN_ROOT` 指定。

列出 case 时会显示 `[matrix]` 或 `[manual]`：

```bash
MRB_BENCHMARK=gemm python3 tools/bench.py list
MRB_BENCHMARK=gemm ./scripts/run_case.sh aggressive_sync_strip --tool racecheck
./scripts/run_case.sh baseline --tool none
```

## 指导文档

- [文档索引](docs/README.md)：生成、注入、测试和记录的阅读顺序。
- [基准 CCE 生成](docs/cce_generation.md)：从单算子配置导出 CCE、内核元数据和 OM。
- [故障注入与 OM 回灌](docs/fault_injection.md)：修改 CCE，通过 CCEC shim 重编译，并由 ATC 封装新 OM。
- [模型生成与完整测试](docs/model_generation_and_testing.md)：环境快照、模型命令、测试和模型提升。
- [多 benchmark 迁移记录](docs/records/2026-08-19-add-softmax-gemm-migration.md)：Add、Softmax 和 GEMM 的来源、选择范围及复验结果。

## 项目结构

```text
mini_racy_benchmarks/
├── benchmarks/
│   ├── matmul/
│   ├── add/
│   ├── softmax/
│   └── gemm/
│       ├── benchmark.json       # 输入、runner、case 和验收策略
│       ├── config/              # ACL 与 ATC 单算子配置
│       ├── kernels/             # baseline 和故障注入 CCE
│       └── models/              # 精选 OM 与 SHA256SUMS
├── runner/
│   ├── include/
│   └── src/                     # 通用 ACL op runner 与 GEMM runner
├── tools/bench.py               # 数据、执行、超时清理、校验和报告
├── scripts/                     # CANN 环境及稳定命令入口
├── docs/
├── build/                       # 不进入 Git
└── artifacts/                   # 输入、日志、输出和报告，不进入 Git
```

每次矩阵运行写入 `artifacts/runs/<run-id>/`，包括每个 case 的 console、racecheck、输出和 JSON 摘要，以及矩阵级 JSON/Markdown 报告。

`benchmarks/` 是版本化实验定义；`build/` 和 `artifacts/` 是可重新生成产物。详细约定见[架构说明](docs/architecture.md)。
