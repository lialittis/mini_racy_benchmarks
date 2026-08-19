# Mini Racy Benchmarks

面向 Ascend AI Core 的小型、可复现故障基准集。项目将算子语义、故障注入内核、预编译模型、执行器和实验结果分层管理，当前包含一个 FP16 MatMul 基准及六类同步/跨核写故障。

## 快速开始

环境要求：Ascend 310P、CANN 9.0、CMake 3.16+、支持 C++11 的编译器，以及安装了 NumPy 的 Python 3。

```bash
cd /root/mini_racy_benchmarks
./scripts/build.sh
./scripts/run_case.sh baseline
./scripts/run_matrix.sh
```

默认使用 `/usr/local/Ascend/cann-9.0.0`。其他安装位置可显式指定：

```bash
CANN_ROOT=/path/to/cann ./scripts/build.sh
```

常用命令：

```bash
python3 tools/bench.py list
./scripts/run_case.sh cross_core_mod4
./scripts/run_case.sh baseline --tool none
./scripts/run_matrix.sh --run-id experiment_001
```

## 指导文档

- [文档索引](docs/README.md)：生成、注入、测试和记录的阅读顺序。
- [基准 CCE 生成](docs/cce_generation.md)：从单算子配置导出 CCE、内核元数据和 OM。
- [故障注入与 OM 回灌](docs/fault_injection.md)：修改 CCE，通过 CCEC shim 重编译，并由 ATC 封装新 OM。
- [模型生成与完整测试](docs/model_generation_and_testing.md)：环境快照、全部模型命令、临时验证、模型提升和七 case 回归。
- [MatMul 0909_retry 生成记录](docs/records/2026-06-09-matmul-0909-retry.md)：当前精选模型的来源、哈希、历史限制和迁移复验结果。

## 项目结构

```text
mini_racy_benchmarks/
├── benchmarks/
│   └── matmul/
│       ├── benchmark.json       # case、输入和预期行为的唯一清单
│       ├── config/              # ACL 与 ATC 单算子配置
│       ├── kernels/             # 基线及可审阅的故障注入 CCE
│       └── models/              # 精选、可直接执行的 OM 测试夹具
├── runner/
│   ├── include/                 # ACL runner 接口
│   └── src/                     # 参数化 MatMul 执行器
├── tools/bench.py               # 数据、执行、校验、报告编排
├── scripts/                     # 稳定的用户入口与 CANN 环境加载
├── docs/architecture.md         # 架构边界和扩展方式
├── build/                       # CMake 产物，不进入 Git
└── artifacts/                   # 输入、日志、输出和报告，不进入 Git
```

每次矩阵运行都会写入：

```text
artifacts/runs/<run-id>/
├── <case>/
│   ├── console.log
│   ├── racecheck.log
│   ├── result/output_0.bin
│   └── summary.json
├── matrix_summary.json
└── matrix_summary.md
```

`benchmarks/` 中的内容是实验定义，应进入 Git；`build/` 和 `artifacts/` 是可重新生成的产物，不应进入 Git。详细约定见 [架构说明](docs/architecture.md) 和 [MatMul 基准说明](benchmarks/matmul/README.md)。
