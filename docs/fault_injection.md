# 故障注入与 OM 回灌

本文说明如何从基准 CCE 制作故障版本、让 `ccec` 重新编译修改后的源码，并由 ATC 将新内核封装进单算子 OM。

## 核心原则

不要直接对 `.om` 做二进制搜索替换。OM 除了内核二进制，还包含 kernel 名称、参数、workspace、调试映射和图元数据，直接改字节容易造成长度、偏移或元数据不一致。

本项目采用编译时拦截：

```text
ATC 生成临时 CCE
    |
    v
PATH 中的 tools/ccec_inject/ccec
    |-- 识别 te_matmul_<64hex>.cce
    |-- 用选定的注入 CCE 覆盖临时源码
    |-- 将源码中的 kernel stem 改回 ATC 期望的 stem
    `-- exec 真实 ccec，原样保留 ATC 参数
    |
    v
ATC 收集新 .o 和元数据并生成 OM
```

这里的“回灌/重新打包”发生在 ATC 构建过程中，不是事后修改 OM。`ccec_inject_wrapper.sh` 只替换源码并记录命令，不自行添加或删除编译参数。

## 制作注入源码

从版本化基准复制一份，并为 case 使用稳定文件名：

```bash
cd /root/mini_racy_benchmarks
cp benchmarks/matmul/kernels/baseline/matmul.cce \
  benchmarks/matmul/kernels/injections/te_matmul_<case-id>.cce
```

只修改与故障模型有关的最小指令集合。完成后必须审阅完整 diff：

```bash
diff -u \
  benchmarks/matmul/kernels/baseline/matmul.cce \
  benchmarks/matmul/kernels/injections/te_matmul_<case-id>.cce
```

当前六个 case 的修改点如下：

| Case | 基准行为 | 注入修改 | 目标现象 |
| --- | --- | --- | --- |
| `cross_core_mod4` | 每个 block 写独立输出区 | 输出地址中的 `block_idx` 改为 `block_idx % 4` | block 0/4、1/5、2/6、3/7 发生 GM WAW |
| `cross_core_large_offset` | `copy_ubuf_to_gm` 写入长度为 `8` | 写入长度改为 `16` | 相邻 block 的 GM 写区重叠 |
| `l43_no_m_to_v` | `PIPE_MTE2 -> PIPE_V` set/wait | 注释掉这对 event | Vector 可能在 GM-to-UB 完成前读取/写入 UB |
| `l46_rm_barrier_v` | 循环头有 `pipe_barrier(PIPE_V)` | 注释掉该 barrier | 调度敏感的 Vector 复用风险 |
| `l54_no_v_to_m` | `PIPE_V -> PIPE_MTE3` set/wait | 注释掉这对 event | MTE3 可能在 Vector 完成前读取 UB |
| `l63_no_m_to_v` | `PIPE_M -> PIPE_V` set/wait | 注释掉这对 event | Vector 可能在 Matrix 写回完成前访问数据 |

`l43_no_m_to_v` 是历史文件名，实际删除的是 MTE2 到 Vector 同步。不要依据文件名猜测 pipeline，必须根据 diff 和报告中的读写管线确认。

## 构建一个注入 OM

以下命令可直接从项目根目录执行。示例构建 `cross_core_mod4`：

```bash
cd /root/mini_racy_benchmarks
set -o pipefail

export CANN_ROOT=${CANN_ROOT:-/usr/local/Ascend/cann-9.0.0}
export CASE=cross_core_mod4
export RUN_ID=matmul_${CASE}_YYYYMMDD_HHMMSS
export OUT="$PWD/artifacts/model_builds/$RUN_ID"
export INJECT_CCE="$PWD/benchmarks/matmul/kernels/injections/te_matmul_${CASE}.cce"

source "$CANN_ROOT/set_env.sh"
mkdir -p "$OUT/models" "$OUT/debug"

PATH="$PWD/tools/ccec_inject:$PATH" \
CCEC_REAL="$CANN_ROOT/bin/ccec" \
CCEC_INJECT_SOURCE="$INJECT_CCE" \
CCEC_INJECT_TARGET_REGEX='^te_matmul_[0-9a-f]+\.cce$' \
CCEC_INJECT_LOG="$OUT/ccec_inject.log" \
atc \
  --singleop="$PWD/benchmarks/matmul/config/operator.json" \
  --soc_version=Ascend310P3 \
  --output="$OUT/models" \
  --op_debug_level=2 \
  --debug_dir="$OUT/debug" \
  --op_compiler_cache_mode=disable \
  2>&1 | tee "$OUT/atc.log"
```

为什么使用 level 2：当前精选模型是带 sanitizer 调试映射的实验模型，mssanitizer 依赖这些信息。若目标只是导出未启用 debug 的基准源码，使用[基准 CCE 生成](cce_generation.md)中的 level 4 流程。

## CCEC 实际做了什么

不要手写一套脱离 ATC 的固定参数。ATC 根据 CANN、SoC 和算子生成正确的命令，shim 原样转交。CANN 9.0 在本项目验证时产生的代表性 O2 命令为：

```bash
ccec -c -O2 <atc-generated>.cce \
  --cce-aicore-arch=dav-m200 \
  --cce-aicore-only \
  -o <atc-generated>.o \
  -mllvm -cce-aicore-fp-ceiling=2 \
  -mllvm -cce-aicore-record-overflow=false \
  --cce-auto-sync=off \
  -mllvm -cce-aicore-jump-expand=true \
  -mllvm -cce-aicore-mask-opt=false \
  --cce-enable-sanitizer \
  -g -mllvm -cce-aicore-long-call
```

同一次 level 2 ATC 构建可能调用 CCEC 多次，例如先 O0 调试编译、再 O2 最终编译。shim 必须在每次目标调用中注入，不能假设只调用一次。

## 构建后验证

先检查拦截确实发生：

```bash
rg -n 'incoming_args|injected|final_command|skipped|ERROR' "$OUT/ccec_inject.log"
```

接受条件：

- 至少出现一条针对 `te_matmul_<64hex>.cce` 的 `injected`。
- 每次目标编译之后都调用真实 `CCEC_REAL`。
- 最终编译参数包含 `--cce-enable-sanitizer`。
- 日志没有 `ERROR`，也没有目标 MatMul CCE 被 `skipped`。
- `models/` 中恰好有一个非空 OM。

继续检查 OM：

```bash
OM=$(find "$OUT/models" -maxdepth 1 -type f -name '*.om')
test "$(printf '%s\n' "$OM" | sed '/^$/d' | wc -l)" -eq 1
test -s "$OM"
strings "$OM" | rg 'te_matmul_|SanitizerFileMapping|__sanitizer_report'
sha256sum "$INJECT_CCE" "$OM"
```

注入源码的 case 文件名通常不会原样出现在 OM 中。shim 会把函数名恢复成 ATC 生成的 `te_matmul_<hash>`，以保持 OM 元数据一致。因此，`strings OM | grep <case-id>` 不是有效的注入证明；应组合使用 CCEC 日志、OM 哈希、数值差异和 racecheck 报告。

## 测试临时模型

不要先覆盖精选模型。把临时 OM 交给 runner：

```bash
mkdir -p "$OUT/run/result"
chmod 750 "$OUT/run" "$OUT/run/result"

mssanitizer --tool=racecheck \
  --log-file="$OUT/run/racecheck.log" -- \
  "$PWD/build/bin/mrb_matmul_runner" \
  --model-dir "$OUT/models" \
  --input-dir "$PWD/artifacts/data/matmul_fp16_16x64x1024" \
  --output-dir "$OUT/run/result" \
  --acl-config "$PWD/benchmarks/matmul/config/acl.json" \
  --device 0 \
  >"$OUT/run/console.log" 2>&1
```

输入不存在时先运行：

```bash
./scripts/build.sh
python3 tools/bench.py prepare
```

检查目标 hazard 和程序错误：

```bash
rg -n 'Potential (RAW|WAR|WAW) hazard|failed|ERROR' \
  "$OUT/run/racecheck.log" "$OUT/run/console.log"
```

一次运行返回码为零并不等于模型正确。还必须检查输出存在、元素数正确、hazard 类型符合 `benchmark.json`，并按 case 的 `output_policy` 与 NumPy 参考值比较。完整门槛见[模型生成与测试](model_generation_and_testing.md)。

## 提升为精选模型

验证通过后才执行：

```bash
CASE=cross_core_mod4
OM=$(find "$OUT/models" -maxdepth 1 -type f -name '*.om')
cp "$OM" "benchmarks/matmul/models/$CASE/"

cd benchmarks/matmul/models
find . -mindepth 2 -maxdepth 2 -type f -name '*.om' -print0 \
  | sort -z \
  | xargs -0 sha256sum \
  | sed 's#  \./#  #' \
  > SHA256SUMS
cd ../../..
```

同时必须更新：

1. `benchmarks/matmul/benchmark.json` 中的 case、CCE、预期 hazard 和数值策略。
2. `benchmarks/matmul/models/SHA256SUMS`。
3. `docs/records/<date>-<operator>-<run-id>.md` 生成记录。
4. `CHANGELOG.md`。
5. 完整七 case 矩阵结果。

## 失败排查

- shim 未执行：确认 `PATH` 的第一个 `ccec` 是 `tools/ccec_inject/ccec`，并关闭 operator cache。
- 所有 candidate 都 skipped：检查 `CCEC_INJECT_TARGET_REGEX` 与 ATC 生成 basename。
- CCEC 编译失败：先确认注入 CCE 能匹配当前 ATC 生成函数签名和参数；查看 `final_command` 的第一个错误。
- OM 能运行但 racecheck 无报告：确认真实 CCEC 命令包含 `--cce-enable-sanitizer`，OM 含 `.SanitizerFileMapping`，再检查 case 是否本身调度敏感。
- mssanitizer 超时：hazard 数为零只表示超时前没有报告，不能判定为 clean；保留 console 和内部日志。
- 只有 OM 字节变化：OM 包含时间、路径等元数据，`cmp` 不足以证明内核修改生效。
