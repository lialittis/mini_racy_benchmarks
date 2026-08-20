# 2026-08-20 TBE/CCE LLVM IR Probe

## 目的

验证 MatMulV3 Ascend C 探针使用的 LLVM IR 思路能否迁移到旧式 TBE 生成的 `.cce`，并
检查 baseline 与六个故障注入版本的差异是否进入编译器 IR。

## 环境和输入

- SoC: Ascend310P3，CCEC arch `dav-m200`
- CANN: `/usr/local/Ascend/cann-9.0.0`
- CCEC: Clang 15.0.5，构建时间标识 `2026-04-25T15:46:25+08:00`
- Baseline: `benchmarks/matmul/kernels/baseline/matmul.cce`
- Injection inputs: `benchmarks/matmul/kernels/injections/*.cce`
- Default optimization: `-O2`
- Auto sync: `--cce-auto-sync=off`

## 已验证命令

```bash
ccec -c -emit-llvm -O2 matmul.cce \
  --cce-aicore-arch=dav-m200 \
  --cce-aicore-only \
  --cce-auto-sync=off \
  -mllvm -cce-aicore-fp-ceiling=2 \
  -mllvm -cce-aicore-record-overflow=false \
  -mllvm -cce-aicore-jump-expand=true \
  -mllvm -cce-aicore-mask-opt=false \
  -mllvm -cce-aicore-long-call \
  -o matmul_baseline.bc

/usr/local/Ascend/cann-9.0.0/tools/bisheng_compiler/bin/llvm-link \
  -S matmul_baseline.bc -o matmul_baseline.ll
```

`ccec -S -emit-llvm ...` 实测失败并报告 `unsupported option '-emit-llvm'`。因此本环境必须先
使用 `-c -emit-llvm` 产生 `.bc`，再转换为 `.ll`。

仓库复现入口：

```bash
./tools/llvm_ir/extract_cce_ir.sh \
  benchmarks/matmul/kernels/baseline/matmul.cce
```

## Baseline 结果

O2 baseline 输出：

| 文件 | 大小/行数 | SHA-256 |
| --- | ---: | --- |
| `matmul_O2.bc`（仓库脚本） | 8.1 KiB | `d3b7eb63589f515e413d1d0ab3c746a006322cabaaccab5ad9430ec8cf696403` |
| `matmul_baseline.ll` | 278 行 | `6b15844d99a3f014a36ce376de149fc3ca74c5bb0de2f92215e3556188b1c5d1` |

手工命令使用相对源码路径时 `.bc` 为
`9a0291ae57dbd0ca0db7b1858fbb39fe512439230f6b4d08739010f593e55bd9`；仓库脚本将源码规范化
为绝对路径，因此 bitcode 哈希不同，但转换出的 `.ll` 完全一致。`.bc` 会携带路径相关信息，
跨目录比较时应优先规范化命令并以 `.ll` 内容及源码哈希为依据。

关键 IR 在 149-162 行形成连续序列：

```text
MAD.f162f32
SET.FLAG.IMM(PIPE_M=2, PIPE_V=1)
WAIT.FLAG.IMM(PIPE_M=2, PIPE_V=1)
MOV.L0C32.TO.UB.f2h
VMULS.f16 x 8
```

`MOV.L0C32.TO.UB` 与第一条 `VMULS` 之间没有 `BARRIER(i64 1)`。O2 已把 CCE 中的 8 次
循环完全展开；第一条 `VMULS` 的 UB 目标为偏移 4096，源为偏移 0，后续源偏移依次为
512、1024、1536、2048、2560、3072、3584。

## O0 和 sanitizer 对照

| 配置 | `.ll` 行数 | 观察 |
| --- | ---: | --- |
| O0 | 2982 | 保留循环和大量临时 SSA，只出现一条循环体 `VMULS` |
| O2 | 278 | 常量折叠并展开为 8 条 `VMULS` |
| O2 + sanitizer + `-g` | 855 | 保留 O2 intrinsic，并增加 sanitizer 参数、debug/ASan CCE API 元数据 |

Sanitizer IR 的 SHA-256 为
`663fcd7821dae7a019adbcbf7e152478a7790da6014486f4a63698be0371408e`。其 kernel 增加
`.arg_address_sanitizer_gm_ptr` 参数，`MAD`、matrix copy 和 `VMULS` 带有
`asan.cce.api.name` 与 `asan.stub.mangling.name` 元数据。

## 七个 CCE 的批量结果

全部 baseline/injection CCE 均成功产生 bitcode 和文本 IR。

| Case | M->V SET/WAIT | 函数体 PIPE_V barrier | 关键差异 |
| --- | --- | ---: | --- |
| baseline | 有 | 8 | 基准 |
| cross_core_large_offset | 有 | 8 | GM 写 footprint 改变 |
| cross_core_mod4 | 有 | 8 | block 输出映射改变 |
| l43_no_m_to_v | 有 | 8 | 文件名沿用历史命名，删除的是较早 MTE2->V event |
| l46_rm_barrier_v | 有 | 4 | Vector barrier 删除进入 O2 IR |
| l54_no_v_to_m | 有 | 8 | V->MTE3 event 删除进入 O2 IR |
| l63_no_m_to_v | 无 | 8 | MAD 后的 M->V SET/WAIT 在 O2 IR 中消失 |

IR SHA-256：

| Case | `.ll` SHA-256 |
| --- | --- |
| baseline | `6b15844d99a3f014a36ce376de149fc3ca74c5bb0de2f92215e3556188b1c5d1` |
| cross_core_large_offset | `f696e95469a921d3ff9112910ad2c9c481f43d8845642150b37d7460cd90233d` |
| cross_core_mod4 | `ecfb3585022601b846fa87f88a586ec74781425f736950518b8210722ef26d74` |
| l43_no_m_to_v | `eafcea40e4f0c79888931f8ddfa6c94fa3a170fd6f6b1b367c7ac61f38daf88a` |
| l46_rm_barrier_v | `96e679a467d0b0449262dfa070e80a2ef621fe3560dfd804cf47ea759db04329` |
| l54_no_v_to_m | `5eaa7fe1576ecaf9cb984d824ae5b49bd2c80f39c6f3749b32698f54fe108cb9` |
| l63_no_m_to_v | `51192461807244f6fed820fe43f43936b0498408019fb9086e88054cec6922c8` |

原始本次产物保存在 `/tmp/tbe_cce_ir_probe_20260820/`；它们是可重建材料，不进入 Git。

## 结论和边界

旧式 TBE/CCE 路径可以直接获取 LLVM IR，而且因为 `.cce` 已接近 intrinsic 层，O2 IR 比
MatMulV3 动态 Ascend C 模板更短、更容易审阅。该方法还能验证故障注入是否真正进入编译表示。

但 `.ll` 仍位于 AI Core 机器码生成之前；它不能替代最终 `.o` 反汇编、运行时 kernel 确认和
mssanitizer。对其他算子必须复用其 ATC 产生的真实 CCEC 参数，不能机械套用本次 MatMul 的
`dav-m200` 和 `-mllvm` 选项。
