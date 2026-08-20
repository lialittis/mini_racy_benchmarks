# TBE/CCE LLVM IR Extraction

旧式 TBE 流程导出的 `.cce` 可以直接通过 CCEC 前端生成 LLVM bitcode，不需要从最终 `.o`
反编译。当前 CANN 9.0.0 的 CCEC 是基于 Clang 15 的驱动，支持 `-c -emit-llvm`。

## 快速使用

```bash
cd /root/mini_racy_benchmarks
./tools/llvm_ir/extract_cce_ir.sh \
  benchmarks/matmul/kernels/baseline/matmul.cce
```

默认输出到：

```text
artifacts/llvm_ir/matmul/
├── compile.command.txt
├── matmul_O2.bc
├── matmul_O2.ll
├── SOURCE_SHA256SUM
└── SHA256SUMS
```

指定输出目录、优化级别或 sanitizer：

```bash
CCEC_OPT_LEVEL=0 ./tools/llvm_ir/extract_cce_ir.sh input.cce /tmp/input_O0
CCEC_SANITIZER=1 ./tools/llvm_ir/extract_cce_ir.sh input.cce /tmp/input_sanitizer
```

算子需要额外 CCEC 参数时放在 `--` 后：

```bash
./tools/llvm_ir/extract_cce_ir.sh input.cce /tmp/input -- \
  -mllvm <operator-specific-option>
```

## 两步转换

第一步让 CCEC 输出二进制 LLVM bitcode：

```bash
ccec -c -emit-llvm -O2 input.cce \
  --cce-aicore-arch=dav-m200 \
  --cce-aicore-only \
  --cce-auto-sync=off \
  -o input.bc
```

第二步使用 CANN 自带 LLVM 工具输出文本 IR：

```bash
/usr/local/Ascend/cann-9.0.0/tools/bisheng_compiler/bin/llvm-link \
  -S input.bc -o input.ll
```

本版本 CCEC 不接受 `-S -emit-llvm` 组合，因此不能用单条命令直接得到 `.ll`。

## 参数来源

脚本的默认参数来自本仓库 Ascend310P3 MatMul 的 ATC/CCEC 实测命令。对其他 SoC 或算子，
应先通过 ATC debug 输出或 CCEC shim 记录真实命令，再把算子特有参数传给脚本。不要假设
`dav-m200`、`--cce-auto-sync=off` 或某个 `-mllvm` 选项对所有平台都成立。

## IR 能证明什么

优化 IR 会保留 `llvm.hivm.*` intrinsic，例如：

```text
llvm.hivm.MAD.f162f32
llvm.hivm.SET.FLAG.IMM
llvm.hivm.WAIT.FLAG.IMM
llvm.hivm.MOV.L0C32.TO.UB.f2h
llvm.hivm.VMULS.f16
llvm.hivm.BARRIER
```

它适合检查模板展开、循环展开、地址空间、显式 event/barrier 和故障注入是否进入编译表示。
它仍是机器码生成前的 IR，不等同于最终 AI Core 反汇编，也不能单独证明运行时调度或竞争。

O0 IR 更接近源码控制流；O2 会内联、常量折叠并展开小循环，更适合检查最终保留的 intrinsic。
启用 `--cce-enable-sanitizer -g` 后，IR 会增加 sanitizer 参数、debug 信息以及
`asan.cce.api.name` 等元数据，应与非 sanitizer IR 分开比较。

完整实测结果见
[2026-08-20 TBE/CCE LLVM IR probe](records/2026-08-20-tbe-cce-llvm-ir-probe.md)。
