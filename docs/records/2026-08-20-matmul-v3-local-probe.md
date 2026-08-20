# 2026-08-20 MatMulV3 Local Kernel Probe

## 目的

在提交算子社区 issue 前，检查 Ascend310P3 的 `MatMulV3` 是否生成与旧内置 `MatMul`
baseline 相同的 `L0C -> UB` 后紧接 `VMULS`、中间缺少 `PIPE_V` completion barrier 的
指令序列。

## 环境

| 项目 | 值 |
| --- | --- |
| 日期 | 2026-08-20 |
| SoC | Ascend310P3 |
| CANN | 9.0.0，`/usr/local/Ascend/cann-9.0.0` |
| 编译入口 | `bin/asc_opc` |
| 内核实现 | `opp/built-in/op_impl/ai_core/tbe/impl/ops_nn/dynamic/mat_mul_v3.py` |
| 开源对照 | `cann/ops-nn` tag `v9.0.0`，`matmul/mat_mul_v3` |
| 格式/类型 | X1 ND FP16，X2 FRACTAL_NZ FP16，Y ND FP16 |
| 实现模式 | `high_performance`，dynamic |

## 完整复现

```bash
cd /root/mini_racy_benchmarks
./benchmarks/matmul_v3/scripts/run_probe.sh
```

核心编译命令由脚本固定为：

```bash
/usr/local/Ascend/cann-9.0.0/bin/asc_opc \
  /usr/local/Ascend/cann-9.0.0/opp/built-in/op_impl/ai_core/tbe/impl/ops_nn/dynamic/mat_mul_v3.py \
  --main_func=mat_mul_v3 \
  --input_param=benchmarks/matmul_v3/config/kernel_compile.json \
  --soc_version=Ascend310P3 \
  --output=artifacts/matmul_v3_probe/kernel \
  --impl_mode=high_performance,optional \
  --op_mode=dynamic \
  --op_debug_config=dump_cce \
  --debug_dir=artifacts/matmul_v3_probe/debug
```

`dump_cce` 对 Ascend C 动态内核产生预处理 `.i` 和实际 `.mk`，而非旧 TBE 风格的短 `.cce`。
`extract_optimized_ir.sh` 从 `.mk` 中提取 tiling key `0`、`65536` 的真实 `bisheng` 命令，仅把
输出切换为 `-emit-llvm`，再通过 CANN 自带 `llvm-link -S` 得到可检查的优化 `.ll`。

## 编译产物

开源 `ops-nn v9.0.0` 构建得到：

| 文件 | SHA-256 |
| --- | --- |
| `MatMulV3_ND_NZ_ND_ND_FP16_FP16_FP16_FP16_high_performance.o` | `33b9d590af4bfcea04eec52182f50f381c5884fa6158e66bdff449bf8f1fa89b` |
| 同名 `.json` | `80dc4f67018fa51458014687aac2e259d9cbf1316ddc005d3ea1aea2f6ac6f31` |

安装包内置同名 `.o` 的 SHA-256 为
`73996035b0882ddb00685cb889d06fba9e7addd281fb2906eda19e708da42b83`，同名 `.json` 为
`f5698c445f98d0cbfad88836079e093934c366bfa0f1d6bc642bf0dc6a3de120`，与开源重编译产物
不相同。公开源码和安装包中的 `ascendc/mat_mul_v3` 目录内容一致；安装包动态 Python 额外为 Ascend310P 设置
`-mllvm -cce-aicore-jump-expand=true`。因此 issue 中应同时注明源码版本和实测二进制来源，
不能把两个 `.o` 当作逐字节相同。

本地探针使用安装目录源码重新生成的 `.o` 为
`686875e6e3c8ad3f57fa4330ae3ca00fe3ce81e3b882c47069de8a1720136513`，`.json` 为
`4f009809571f1183c022af3bf16b71fbdb8a19430a413e52d74568d0981dd15f`。

## 静态检查结果

两个优化 IR 均找到以下模式：

```text
llvm.hivm.SET.FLAG.REG(i64 2, i64 1, ...)
llvm.hivm.WAIT.FLAG.REG(i64 2, i64 1, ...)
...
llvm.hivm.MOV.L0C32.TO.UB.f2h(... addrspace(6) ..., ... addrspace(5) ...)
... no llvm.hivm.BARRIER(i64 1) ...
llvm.hivm.VMULS.f16(... addrspace(6) ..., ... addrspace(6) ...)
```

手工抽查结果：

| tiling key | 搬运行附近 | 首个后续 VMULS 行附近 | 区间内 `BARRIER(i64 1)` |
| ---: | ---: | ---: | --- |
| `0` | 3711 | 3882 | 无 |
| `65536` | 2772 | 2943 | 无 |

搬运之前还存在 `llvm.hivm.MAD.f162f32`：key `0` 在 3416 行附近写 L0C `%1910`，3711
行附近的搬运从同一 `%1910` 读取；key `65536` 在 2477 行附近写 `%1201`，2772 行附近
的搬运从同一 `%1201` 读取。自动检查器会验证这个 SSA 指针关系，而不是只依赖指令距离。

`65536` IR 中该模板片段还出现于多个实例区间；检查器会逐一报告，不只匹配首处。

结论：`MatMulV3` 的已编译动态内核模板中确实保留了与旧 MatMul baseline 相同的
“M->V 同步、L0C32->UB、无 V barrier、VMULS”结构。这可以作为向算子仓库提问和请求确认
的依据。

## 运行时验证状态

本次没有把普通 `aclnnMatmul` 的结果算作 MatMulV3 证据：mssanitizer 日志显示该调用实际
执行 `MatMulV2_ND_ND_FP16...`。

随后尝试官方 `mat_mul_v3/examples/test_aclnn_matmul_weight_nz_at.cpp` 路径，包括
`aclnnTransMatmulWeight`、`aclnnMatmulWeightNz`。未修改的官方示例在
`aclnnMatmulWeightNzGetWorkspaceSize` 阶段返回 `161002`；显式
`aclnnNpuFormatCastCalculateSizeAndFormat` 也返回 `161002`。因此当前环境尚不能把目标
MatMulV3 内核送入 racecheck。

这不会推翻静态 IR 结果，但限制了结论：目前不能声称该序列在给定 shape 下必然产生运行时
竞争、数值错误或 sanitizer 漏报。

## 后续检查

1. 在能成功运行 `aclnnMatmulWeightNz` 的 Ascend310P3/CANN 环境中记录实际 kernel name、
   tiling key 和 mssanitizer 原始日志。
2. 取得目标 shape 的 tiling data，求值 IR 中 UB 地址表达式，确认 L0C32->UB 写区间与
   VMULS 源/目标区间是否发生 RAW/WAW 重叠。
3. 在算子仓库 issue 中先请求维护者确认同 PIPE_V 指令是否由硬件保证顺序完成；若不保证，
   再构造加入 `PipeBarrier<PIPE_V>()` 的对照内核。
4. 只有在目标 MatMulV3 确实执行且 racecheck 仍为 0 时，再向 mssanitizer 报告漏报。
