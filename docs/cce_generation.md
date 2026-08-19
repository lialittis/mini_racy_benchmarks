# 基准 CCE 生成

本文说明如何从 MatMul 单算子定义生成基准 CCE。输入是 `benchmarks/matmul/config/operator.json`，目标硬件是 Ascend 310P3。

## 产物关系

```text
operator.json
    |
    v
ATC -> TBE -> CCEC
    |          |-- te_matmul_<hash>.cce   AI Core 源码
    |          |-- te_matmul_<hash>.o     AI Core 二进制
    |          |-- te_matmul_<hash>.json  内核元数据
    |          `-- *_loc.json             可选的源码位置映射
    `------------- 0_MatMul_*.om          单算子离线模型
```

CCE 是编译中间源码，不是 mssanitizer 的输出。mssanitizer 消费带有 sanitizer 调试信息的 OM，并利用 CCE 路径映射报告源码位置。

## 前置检查

```bash
cd /root/mini_racy_benchmarks
export CANN_ROOT=${CANN_ROOT:-/usr/local/Ascend/cann-9.0.0}
source "$CANN_ROOT/set_env.sh"

command -v atc
command -v ccec
npu-smi info
python3 --version
```

检查 `operator.json` 的算子类型、dtype、format 和 shape 是否与 runner 一致：

```text
A: [16, 64],   FP16, ND
B: [64, 1024], FP16, ND
C: [16, 1024], FP16, ND
operator: MatMul
soc_version: Ascend310P3
```

## 导出未启用 TBE Debug 的基准 CCE

当目标是审阅 TBE 生成的源码或建立不含 `pipe_all` debug 行为的基准时，使用 `--op_debug_level=4`。该级别保留 `.o`、`.json` 并导出 `.cce`。

```bash
cd /root/mini_racy_benchmarks
set -o pipefail

export CANN_ROOT=${CANN_ROOT:-/usr/local/Ascend/cann-9.0.0}
export RUN_ID=matmul_cce_release_YYYYMMDD_HHMMSS
export OUT="$PWD/artifacts/model_builds/$RUN_ID"

source "$CANN_ROOT/set_env.sh"
mkdir -p "$OUT/models" "$OUT/debug"

atc \
  --singleop="$PWD/benchmarks/matmul/config/operator.json" \
  --soc_version=Ascend310P3 \
  --output="$OUT/models" \
  --op_debug_level=4 \
  --debug_dir="$OUT/debug" \
  --op_compiler_cache_mode=disable \
  2>&1 | tee "$OUT/atc.log"
```

`RUN_ID` 必须替换为本次实验的稳定名称，不要原样使用占位符。禁用编译缓存是为了确保 CCEC 真正执行，并让生成记录对应本次输入。

查找产物：

```bash
find "$OUT" -type f \
  \( -name 'te_matmul_*.cce' -o -name 'te_matmul_*.o' \
     -o -name 'te_matmul_*.json' -o -name '*.om' \) \
  -print | sort
```

ATC 9.0 会在 `debug/kernel_meta/kernel_meta_<id>/kernel_meta/` 下创建内核文件，随机 `<id>` 不能作为稳定标识。稳定标识应由输入配置哈希、CCE/OM SHA-256、CANN 版本和生成记录共同组成。

## 生成可供 mssanitizer 使用的基准 OM

本 benchmark 的精选 OM 使用 sanitizer 调试构建。为保持与历史模型一致，使用 `--op_debug_level=2`；当前 CANN 9.0 的实际 CCEC 命令应包含 `--cce-enable-sanitizer`，OM 中应包含 `.SanitizerFileMapping`。

```bash
export RUN_ID=matmul_baseline_sanitizer_YYYYMMDD_HHMMSS
export OUT="$PWD/artifacts/model_builds/$RUN_ID"
mkdir -p "$OUT/models" "$OUT/debug"

atc \
  --singleop="$PWD/benchmarks/matmul/config/operator.json" \
  --soc_version=Ascend310P3 \
  --output="$OUT/models" \
  --op_debug_level=2 \
  --debug_dir="$OUT/debug" \
  --op_compiler_cache_mode=disable \
  2>&1 | tee "$OUT/atc.log"
```

级别差异：

| 级别 | CCE | `.o`/JSON | 主要用途 |
| ---: | --- | --- | --- |
| `0` | 不保证保留 | 不保证保留 | 普通构建 |
| `1` | 保留 | 映射 JSON | TBE `pipe_all` debug |
| `2` | 保留 | 映射 JSON，CCEC `-O0-g` 路径 | 本项目 sanitizer 构建与源码定位 |
| `3` | 不导出 CCE | 保留 | 保留内核二进制和元数据 |
| `4` | 保留 | 保留 | 不启用 debug 的源码审阅 |

`op_debug_level` 控制 TBE debug 和中间文件保留，不应被当作 sanitizer 开关。2026-08-19 的本机 CANN 9.0 实测中，level 4 OM 仍包含 `.SanitizerFileMapping` 和 `__sanitizer_report_*` 符号。反过来也不能仅凭 level 4 推断 OM 一定可用：必须检查真实 CCEC 参数、OM 符号并实际运行 mssanitizer。

## 接受基准 CCE 前的检查

```bash
mapfile -t CCES < <(find "$OUT/debug" -type f -name 'te_matmul_*.cce' | sort)
mapfile -t OMS < <(find "$OUT/models" -maxdepth 1 -type f -name '*.om' | sort)

test "${#CCES[@]}" -eq 1
test "${#OMS[@]}" -eq 1
CCE=${CCES[0]}
OM=${OMS[0]}

test -s "$CCE"
test -s "$OM"
rg 'extern "C".*te_matmul_.*__kernel0' "$CCE"
rg 'aicore arch: Ascend310P3' "$CCE"
sha256sum "$CCE" "$OM"
strings "$OM" | rg 'te_matmul_|SanitizerFileMapping'
```

如果数量断言失败，不要猜测某个文件。先检查 ATC 日志和单算子配置，确认为什么产生多个 kernel。

只有在完成基准数值测试和 racecheck 后，才把选中的源码保存为 `benchmarks/matmul/kernels/baseline/matmul.cce`。模型提升和记录规则见[模型生成与测试](model_generation_and_testing.md)。

## 常见问题

- CCE 没有生成：确认 `op_debug_level` 是 `1`、`2` 或 `4`，并搜索 `debug_dir` 的所有子目录。
- CCEC 没有执行：关闭 `op_compiler_cache`，使用新的 `RUN_ID`，并检查 ATC 日志。
- kernel hash 改变：CANN、OPP、shape、dtype、format、编译选项变化都可能改变 hash；不要在脚本中写死 hash。
- mssanitizer 无源码行号：检查 OM 是否含 `.SanitizerFileMapping`，并保留本次 `debug_dir` 原始材料。
- 回溯显示旧绝对路径：OM 嵌入了构建时路径；这不改变执行结果，但生成记录必须说明来源。
