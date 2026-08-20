# msSanitizer MatMul Baseline UB WAR Issue Record

## 状态

- Upstream project: <https://gitcode.com/Ascend/mssanitizer>
- Upstream issue: <https://gitcode.com/Ascend/mssanitizer/issues/191>
- Reference style: <https://gitcode.com/Ascend/mssanitizer/issues/113>
- Type: Bug Report
- Label: `bug`
- Status: Submitted / Open (`TODO`)
- Submitted at: `2026-08-20 21:11:40 +08:00`
- Last checked: `2026-08-20`
- Maintainer response: 目前只有自动受理回复，尚无技术结论
- Reproducer repository: <https://github.com/lialittis/mini_racy_benchmarks>
- Reproducer commit: `8c78f11b0bad5e0f05d48f9de61752100124c990`
- Local experiment record:
  [`docs/records/2026-08-20-matmul-baseline-stress20.md`](../../docs/records/2026-08-20-matmul-baseline-stress20.md)

## 当前结论

当前 issue 无需为了可复现性强制更新。其引用的 commit 已公开可访问，并包含
baseline OM、CCE、输入生成、构建和测试脚本，足以按 issue 中的命令独立复现。
仓库后续提交增加了分析和开源贡献记录，但不影响该复现 commit 的有效性。

更新为仓库最新 commit 或补充全部原始日志都属于增强项。仅在维护者要求更多证据、
现有 commit 无法复现，或 issue 中的统计和路径需要纠正时再更新。

## 已提交标题

```text
[Bug]: racecheck 偶发报告 MatMul 中静态不重叠 UB 地址的 WAR（20 次复现 5 次）
```

## 已提交内容与本地补充

以下内容保留已提交 issue 的核心复现信息；接口参考和附件清单同时作为仓库内的
后续跟踪材料。

### 环境信息

- OS: Ubuntu 22.04.5 LTS
- Hardware: Ascend 310P3
- `npu-smi`: 25.6.rc1.b010
- CANN: 9.0.0
- CCEC: clang 15.0.5, `clang-5c68a1cb1231`
- msSanitizer: `26.0.0-3f55c5f8ac51bff79176f4e59b16f8e77ad2a5ab`
- msopscommon: `99539636ec8d920ae8151e9b7007c7d3a056c3e7`
- 调用方式: AscendCL 单算子 OM
- SoC: Ascend310P3

### 问题描述

使用 `mssanitizer --tool=racecheck` 检测内置 MatMul 算子的 sanitizer baseline OM
时，工具偶发报告 CCE line 37 与 line 52 之间存在 UB WAR。

但根据 CCE 源码的静态地址布局，两条指令访问的 UB 地址范围并不重叠。因此怀疑
`scatter_vnchwconv_b16` 的 VA 地址解析、config 解码或源码映射存在概率性误报。

### 重现步骤

```bash
git clone https://github.com/lialittis/mini_racy_benchmarks.git
cd mini_racy_benchmarks
git checkout 8c78f11b0bad5e0f05d48f9de61752100124c990

source /usr/local/Ascend/cann-9.0.0/set_env.sh
./scripts/build.sh

for i in $(seq -w 1 20); do
  ./scripts/run_case.sh baseline --tool racecheck \
    --run-id "matmul_baseline_stress20_${i}"
done
```

### 实际结果

20 次执行均成功，输出均为 `0/16384` mismatch，20 个输出文件的 SHA-256 均为：

```text
69c3bb21512fb81e53ebe11f6e965e1ab6ca1e6b809bc791a9b66667c077d87d
```

其中 5/20 次报告 `UB_WAR`：

- 4 次报告 2 条。
- 1 次报告 4 条。
- 其余 15 次没有报告 hazard。

代表性报告如下，已将真实绝对路径替换为 `<project-root>`：

```text
====== ERROR: Potential WAR hazard detected at UB in
te_matmul_c57d0289eba01acf11db0c8c7ce2e1441bae77ea3034f8d4cdb84d8b29725064__kernel0:
======    PIPE_MTE3 Read at WAR()+0xa00 in block 0 (aicore)
======    at pc current 0x840 (serialNo:26)
======    #0 <project-root>/matmul.cce:37:5
======    PIPE_V Write at WAR()+0xb00 in block 0 (aicore)
======    at pc current 0xf60 (serialNo:36)
======    #0 <project-root>/matmul.cce:52:7
```

不同运行中，`PIPE_V` 写地址还出现过：

```text
0x0, 0xac0, 0xae0, 0xb20, 0xb80, 0xba0, 0xbc0, 0xbe0
```

此前另一组五次矩阵测试中也有一次报告相同的 kernel、block、pipeline、CCE
line、reader PC 和 serialNo；当时的 writer 地址为 `0xac0` 和 `0xae0`。

### CCE 地址分析

line 37 的源缓冲区定义和搬运为：

```cpp
__ubuf__ half* tensor_a_zz_fract_k =
    (__ubuf__ half *)get_imm(2560);  // 0x0a00

copy_ubuf_to_cbuf(
    tensor_a_zz_fract_k_local_L1,
    tensor_a_zz_fract_k,
    0, 1, 64, 0, 0);
```

搬运长度为 `64 * 32B = 0x800B`，因此读取范围为：

```text
[0x0a00, 0x1200)
```

line 52 的目标缓冲区和地址寄存器定义为：

```cpp
__ubuf__ half* tensor_b_zn_fract =
    (__ubuf__ half *)get_imm(23040);  // 0x5a00

VA0 = tensor_b_zn_fract + i0 * 2048 half;
scatter_vnchwconv_b16(VA0, VA2, 0x0800000000010010);
```

根据 `scatter_vnchwconv_b16(dst, src, ...)` 的接口语义，line 52 的目标地址从
`0x5a00`、`0x6a00`、`0x7a00` 和 `0x8a00` 开始，与 line 37 的读取范围不重叠。

接口参考：
<https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/83RC1alpha001/API/cceintrinsicapi/cceapi_0059.html>

### 预期结果

对于静态不重叠的 UB 地址，不应报告 WAR；或者报告应展示 line 52 实际解析到的
目标地址，以便判断为何该地址被认为与 `[0x0a00, 0x1200)` 重叠。

### 希望确认

1. racecheck 是否会对 VA 地址做归一化，报告地址是否等于实际 UB 地址？
2. `scatter_vnchwconv_b16` 的 config 重载是否可能丢失目标地址高位？
3. 是否可能将其他 `PIPE_V` 写操作的调试位置映射到 line 52？
4. 该现象是否属于已知限制？

### 可选后续补充

当前 issue 未附全部原始日志。只有在维护者请求或独立复现失败时，才需要补充以下
经过脱敏的材料：

- `benchmarks/matmul/kernels/baseline/matmul.cce`
- 阳性日志：run 06、08、11、15、17 的 `racecheck.log` 和 `summary.json`
- 阴性对照：run 01 的 `racecheck.log` 和 `summary.json`
- 20 次结果汇总表
- baseline OM、CCE、输入文件和输出文件的 SHA-256

## 提交后跟踪

- [x] Issue 已提交：<https://gitcode.com/Ascend/mssanitizer/issues/191>。
- [x] Reproducer commit 已推送并确认无需登录即可访问。
- [x] Issue 正文已包含环境、复现命令、20 次统计和 CCE 地址分析。
- [x] 展示日志中的本地绝对路径已替换为 `<project-root>`。
- [x] 保留了 PC、serialNo、block、CCE 行号和 UB 地址等诊断字段。
- [ ] 等待维护者的技术回复或复现结果。
- [ ] 仅在维护者要求时补充脱敏后的原始日志和哈希汇总。
- [ ] 状态、结论或解决版本发生变化后更新本文。
