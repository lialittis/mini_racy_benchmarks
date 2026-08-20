# MatMulV3 Local Kernel Probe

本目录用于回答一个限定问题：Ascend310P3 的 `MatMulV3` 动态内核中，是否仍能看到旧
`MatMul` baseline 的以下管线序列？

```text
MAD / PIPE_M
SET_FLAG + WAIT_FLAG / PIPE_M -> PIPE_V
L0C32 -> UB / PIPE_V
没有 PIPE_V completion barrier
VMULS / PIPE_V
```

它是内核编译与静态 IR 检查探针，不属于当前 OM/mssanitizer 执行矩阵。

## 运行

```bash
cd /root/mini_racy_benchmarks
./benchmarks/matmul_v3/scripts/run_probe.sh
```

也可以指定输出目录：

```bash
./benchmarks/matmul_v3/scripts/run_probe.sh /tmp/matmul_v3_probe
```

默认生成物位于 `artifacts/matmul_v3_probe/`，已被 Git 忽略。主要文件包括：

- `kernel/*.o` 和 `kernel/*.json`：`asc_opc` 输出的动态内核包。
- `debug/**/*.i` 和 `debug/**/*.mk`：预处理源码和实际编译命令。
- `optimized_ir/matmul_v3_{0,65536}.ll`：两个 tiling key 的优化 LLVM IR。
- `optimized_ir/compile_*.command.txt`：从 `.mk` 提取并改为 `-emit-llvm` 的命令。

## asc_opc、Python 与 Ascend C 的关系

本探针把 `dynamic/mat_mul_v3.py` 传给 `asc_opc`，因此编译入口确实是 Python；但该文件在
当前 MatMulV3 中主要是注册和编译适配层，不是矩阵计算的主体实现。实际调用链为：

```text
kernel_compile.json
    -> asc_opc 动态加载 dynamic/mat_mul_v3.py
    -> 调用 @register_operator("MatMulV3") 注册的 mat_mul_v3(...)
    -> Python 根据输入 dtype/format/属性构造 -D 宏、include 和 SoC 编译选项
    -> get_kernel_source() 定位 ascendc/mat_mul_v3/mat_mul_v3.cpp
    -> compile_op(src, ...) 调用 bisheng
    -> .i / .mk / tiling-key .o / kernel JSON
```

当前安装包 Python 入口中的关键步骤是：

- `get_dtype_fmt_options()` 生成 `DTYPE_X1`、`FORMAT_X1`、`DTYPE_X2`、`FORMAT_X2` 等宏。
- `ascendc_src_file = "mat_mul_v3.cpp"` 明确选择 Ascend C 源文件。
- `get_kernel_source()` 将其解析到 `impl/ops_nn/ascendc/mat_mul_v3/mat_mul_v3.cpp`。
- `compile_op()` 把该 `.cpp`、算子信息和编译选项交给 Bisheng。

矩阵分块、数据搬运、Matmul API 和输出转换位于 `mat_mul_v3.cpp` 及同目录的
`mat_mul_base_kernel.h`、`mat_mul_optimized_fixpipe_algorithm.h` 等 C++ 头文件中。Python
使用 `tbe_register.register_operator` 是因为 `asc_opc` 复用了统一算子注册框架，不能据此
判断该算子是传统 TBE DSL 实现。

| 路径 | Python 的作用 | 进入编译器的计算实现 |
| --- | --- | --- |
| 传统 TBE | 用 TE/TVM DSL 描述 compute 和 schedule | Python 生成的 `.cce` |
| 当前 MatMulV3 Ascend C | 注册、规格适配、源码选择、发起编译 | `mat_mul_v3.cpp` 及其头文件 |

因此，本探针不是对 Python 字节码提取 LLVM IR。它先让 Python 编译入口生成真实 Bisheng
命令，再对 Bisheng 编译的 Ascend C/C++ 内核请求 `-emit-llvm`。

## 为什么不只 grep `.i`

预处理 `.i` 包含大量未实例化的 Ascend C API 模板，同时出现
`copy_matrix_cc_to_ubuf` 和 `vmuls` 并不能证明目标内核实际生成了两条指令。探针从编译器
生成的 `.mk` 复用同一套宏、include 和优化选项，输出优化 IR，再检查以下映射：

| CCE 概念 | 优化 IR intrinsic |
| --- | --- |
| `PIPE_M -> PIPE_V` event | `SET/WAIT.FLAG.REG(i64 2, i64 1, ...)` |
| `copy_matrix_cc_to_ubuf` | `MOV.L0C32.TO.UB.f2h` |
| `vmuls` | `VMULS.f16` |
| `pipe_barrier(PIPE_V)` | `BARRIER(i64 1)` |

检查器要求此前的 `MAD` 目标与搬运源是同一个 L0C SSA 指针、搬运前存在 `M -> V`
event，并检查搬运到后续 `VMULS` 之间是否出现 `BARRIER(i64 1)`。当前 CANN 9.0.0 的
tiling key `0` 和 `65536` 都能找到缺少该 barrier 的匹配区间。

## 证据边界

- 该结果证明已编译的动态内核模板包含与旧 MatMul 相同的指令级顺序。
- IR 的 UB 操作数属于 `addrspace(6)`，但本探针尚未对具体运行时 tiling data 求值，因此
  不直接声称每个实际 shape 都具有与旧 MatMul 完全相同的重叠字节区间。
- 该结果不是数值错误证明，也不是 mssanitizer 漏报证明；后两者需要让目标设备确实执行
  `MatMulV3` 内核后再采集。
- 普通 `aclnnMatmul` 在本机实测分派到 `MatMulV2`。官方 `aclnnMatmulWeightNz` 示例在当前
  Ascend310P3/CANN 9.0.0 环境的 workspace 查询阶段返回 `161002`，所以尚未完成目标内核的
  racecheck 运行。

完整命令、哈希和实测结论见
[2026-08-20 MatMulV3 local probe](../../docs/records/2026-08-20-matmul-v3-local-probe.md)。
