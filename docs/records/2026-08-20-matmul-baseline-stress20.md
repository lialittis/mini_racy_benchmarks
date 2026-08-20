# 2026-08-20 MatMul Baseline Racecheck Stress Test

## 目的

对当前精选 MatMul baseline OM 串行执行 20 次 `mssanitizer --tool=racecheck`，验证此前偶发
`UB_WAR` 是否可复现，并区分 sanitizer 告警波动与数值输出波动。

## 固定输入

- Benchmark: `matmul_fp16_16x64x1024`
- Operator: built-in `MatMul`
- Device: 0
- Tool: `mssanitizer 26.0.0-3f55c5f8ac51bff79176f4e59b16f8e77ad2a5ab`
- Baseline OM SHA-256: `a8c55e13289a42331e75cbe7b1cf7580157c353e73ed5ae2a6c28bb53380c637`
- Baseline CCE SHA-256: `e2c2a39b3fadf1ccb136e0ca5119a234a806987321c51b878b9c1478bee02170`
- `input_0.bin` SHA-256: `af41edb543a836c432dddaf2ef140571996a9a2f2d945d09cad1849fee62cbfa`
- `input_1.bin` SHA-256: `84313169705fec800791c83cf63bc4de2503b722f4f6e7dcc176989238d381af`

## 命令

```bash
cd /root/mini_racy_benchmarks
for i in $(seq -w 1 20); do
  ./scripts/run_case.sh baseline --tool racecheck \
    --run-id "matmul_baseline_stress20_20260820_${i}"
done
```

每次运行的原始材料位于：

```text
artifacts/runs/matmul_baseline_stress20_20260820_<01..20>/
```

每个目录包含 `baseline/racecheck.log`、`baseline/summary.json`、输出文件和 matrix summary。

## 结果

| Run | `UB_WAR` count | Return code | Timeout | Mismatch | PASS |
| ---: | ---: | ---: | --- | ---: | --- |
| 01 | 0 | 0 | no | 0/16384 | yes |
| 02 | 0 | 0 | no | 0/16384 | yes |
| 03 | 0 | 0 | no | 0/16384 | yes |
| 04 | 0 | 0 | no | 0/16384 | yes |
| 05 | 0 | 0 | no | 0/16384 | yes |
| 06 | 2 | 0 | no | 0/16384 | yes |
| 07 | 0 | 0 | no | 0/16384 | yes |
| 08 | 2 | 0 | no | 0/16384 | yes |
| 09 | 0 | 0 | no | 0/16384 | yes |
| 10 | 0 | 0 | no | 0/16384 | yes |
| 11 | 4 | 0 | no | 0/16384 | yes |
| 12 | 0 | 0 | no | 0/16384 | yes |
| 13 | 0 | 0 | no | 0/16384 | yes |
| 14 | 0 | 0 | no | 0/16384 | yes |
| 15 | 2 | 0 | no | 0/16384 | yes |
| 16 | 0 | 0 | no | 0/16384 | yes |
| 17 | 2 | 0 | no | 0/16384 | yes |
| 18 | 0 | 0 | no | 0/16384 | yes |
| 19 | 0 | 0 | no | 0/16384 | yes |
| 20 | 0 | 0 | no | 0/16384 | yes |

汇总：

- 20/20 次执行成功，无 timeout。
- 5/20 次报告 `UB_WAR`，本组样本中的出现率为 25%；不能把该比例直接外推为长期概率。
- 告警计数分布为：15 次 0、4 次 2、1 次 4，共报告 12 条 WAR。
- 20/20 次均为 `0/16384` mismatch，`max_abs_error=0`。
- 20 个输出文件的 SHA-256 均为
  `69c3bb21512fb81e53ebe11f6e965e1ab6ca1e6b809bc791a9b66667c077d87d`。

## Hazard 指纹

五次阳性运行均指向同一依赖对：

- Kernel: `te_matmul_c57d0289eba01acf11db0c8c7ce2e1441bae77ea3034f8d4cdb84d8b29725064__kernel0`
- Block: 0
- Reader: `PIPE_MTE3`, baseline CCE line 37:5, `WAR()+0xa00`, PC `0x840`, serial 26
- Writer: `PIPE_V`, baseline CCE line 52:7
- Writer PC/serial: `0xf60`/36，部分片段为 `0x1494`/39
- Writer offset 随运行变化，已见 `0x0`、`0xb00`、`0xb20`、`0xb80`、`0xba0`、`0xbc0` 和 `0xbe0`

因此，变化的是动态调度下被报告的写片段和数量，而不是 CCE 行号、pipeline 对或读取位置。

## 结论和后续检查

本次压力测试确认 baseline 的 `UB_WAR` 是可复现但非每次出现的调度敏感现象，不再只是一次孤立
告警。同时，当前证据仍只显示 sanitizer 告警波动，没有显示功能输出波动。

稳定的 line 37/line 52 指纹增强了“UB 复用窗口缺少直接 `MTE3 -> V` 完成依赖”的解释，但尚未
排除 sanitizer 插桩或访问范围建模带来的保守报告。保持 manifest 的当前策略：baseline 不要求
hazard 必须出现，允许可选 `UB_WAR`，且始终要求输出精确匹配。

下一步应完成以下对照：

1. 再执行 30 次独立 racecheck，使同环境样本达到原计划的 50 次。
2. 对同一 OM 执行至少 50 次 `--tool none` 并核对输出 SHA-256。
3. 新建诊断 case，在 line 37 之后、line 52 之前增加明确的 `MTE3 -> V` 依赖。
4. 同时重建一个不修改 CCE 的 sanitizer control OM。只有同步版本稳定消除该指纹、control 仍能
   复现时，才将其定性为已确认的 baseline WAR。
