# 模型生成与完整测试

本文给出从环境记录到模型提升的完整流程。日常复现实验可以直接使用仓库中的精选 OM；只有重新生成 CCE 或修改故障时才需要执行模型构建部分。

## 1. 记录环境

每次模型构建开始前创建独立 `RUN_ID`，并保存环境快照：

```bash
cd /root/mini_racy_benchmarks
set -euo pipefail

export CANN_ROOT=${CANN_ROOT:-/usr/local/Ascend/cann-9.0.0}
export RUN_ID=matmul_models_YYYYMMDD_HHMMSS
export OUT="$PWD/artifacts/model_builds/$RUN_ID"

source "$CANN_ROOT/set_env.sh"
mkdir -p "$OUT"

{
  date -Is
  uname -a
  npu-smi info
  sed -n '1,40p' "$CANN_ROOT/opp/version.info"
  ccec --version | head -n 1
  mssanitizer --version
  python3 --version
  python3 -c 'import numpy; print("numpy", numpy.__version__)'
  cmake --version | head -n 1
  g++ --version | head -n 1
  git status --short --branch
  git rev-parse HEAD 2>/dev/null || true
} > "$OUT/environment.txt" 2>&1
```

记录至少应包括 CANN/OPP、CCEC、mssanitizer revision、SoC、driver、CPU 架构、Python/NumPy、Git commit 和工作区状态。`atc --version` 在当前 CANN 9.0 不可用，以 `opp/version.info`、安装路径和 OM 内嵌命令共同标识版本。

## 2. 构建 ACL runner 和输入

```bash
./scripts/build.sh
python3 tools/bench.py list
python3 tools/bench.py prepare --force
```

确定性输入写入：

```text
artifacts/data/matmul_fp16_16x64x1024/
├── input_0.bin
├── input_1.bin
└── metadata.json
```

`metadata.json` 保存 seed 和输入 SHA-256。参考输出由 `tools/bench.py` 使用 NumPy FP16 MatMul 计算。

## 3. 生成基准 sanitizer OM

```bash
BASE="$OUT/baseline"
mkdir -p "$BASE/models" "$BASE/debug"

atc \
  --singleop="$PWD/benchmarks/matmul/config/operator.json" \
  --soc_version=Ascend310P3 \
  --output="$BASE/models" \
  --op_debug_level=2 \
  --debug_dir="$BASE/debug" \
  --op_compiler_cache_mode=disable \
  >"$BASE/atc.log" 2>&1
```

单独导出未启用 debug 的 CCE 时改用 level 4，详见[基准 CCE 生成](cce_generation.md)。用于本 benchmark racecheck 的基准 OM 必须经过 sanitizer 符号和实际运行检查。

## 4. 生成全部注入 OM

以下循环逐 case 建立隔离目录。它使用仓库内 `tools/ccec_inject/ccec` 拦截 ATC 发起的 CCEC 编译。

```bash
for CASE in \
  cross_core_mod4 \
  cross_core_large_offset \
  l43_no_m_to_v \
  l46_rm_barrier_v \
  l54_no_v_to_m \
  l63_no_m_to_v
do
  CASE_OUT="$OUT/$CASE"
  INJECT_CCE="$PWD/benchmarks/matmul/kernels/injections/te_matmul_${CASE}.cce"
  mkdir -p "$CASE_OUT/models" "$CASE_OUT/debug"

  PATH="$PWD/tools/ccec_inject:$PATH" \
  CCEC_REAL="$CANN_ROOT/bin/ccec" \
  CCEC_INJECT_SOURCE="$INJECT_CCE" \
  CCEC_INJECT_TARGET_REGEX='^te_matmul_[0-9a-f]+\.cce$' \
  CCEC_INJECT_LOG="$CASE_OUT/ccec_inject.log" \
  atc \
    --singleop="$PWD/benchmarks/matmul/config/operator.json" \
    --soc_version=Ascend310P3 \
    --output="$CASE_OUT/models" \
    --op_debug_level=2 \
    --debug_dir="$CASE_OUT/debug" \
    --op_compiler_cache_mode=disable \
    >"$CASE_OUT/atc.log" 2>&1
done
```

对每个 case 检查：

```bash
for CASE in cross_core_mod4 cross_core_large_offset l43_no_m_to_v \
            l46_rm_barrier_v l54_no_v_to_m l63_no_m_to_v
do
  CASE_OUT="$OUT/$CASE"
  test "$(find "$CASE_OUT/models" -maxdepth 1 -name '*.om' | wc -l)" -eq 1
  rg -q 'injected .* into .*te_matmul_[0-9a-f]{64}\.cce' \
    "$CASE_OUT/ccec_inject.log"
  rg -q -- '--cce-enable-sanitizer' "$CASE_OUT/ccec_inject.log"
done
```

完整原理、单 case 命令和失败排查见[故障注入与 OM 回灌](fault_injection.md)。

## 5. 测试临时模型

仓库编排器按 `benchmark.json` 查找精选模型。临时模型在提升前使用 runner 的参数化接口测试。以 `cross_core_mod4` 为例：

```bash
CASE=cross_core_mod4
CASE_OUT="$OUT/$CASE"
mkdir -p "$CASE_OUT/run/result"
chmod 750 "$CASE_OUT/run" "$CASE_OUT/run/result"

mssanitizer --tool=racecheck \
  --log-file="$CASE_OUT/run/racecheck.log" -- \
  "$PWD/build/bin/mrb_matmul_runner" \
  --model-dir "$CASE_OUT/models" \
  --input-dir "$PWD/artifacts/data/matmul_fp16_16x64x1024" \
  --output-dir "$CASE_OUT/run/result" \
  --acl-config "$PWD/benchmarks/matmul/config/acl.json" \
  --device 0 \
  >"$CASE_OUT/run/console.log" 2>&1
```

验证四个维度：

1. runner 和 mssanitizer 正常结束，console 不含 ACL、设备或模型加载失败。
2. `result/output_0.bin` 存在且为 `16 * 1024 * sizeof(float16) = 32768` 字节。
3. racecheck 的 hazard 类型满足 manifest 的 `required_hazards`，且不超出 `allowed_hazards`。
4. 与 NumPy 的逐元素比较满足 `output_policy`：`match`、`mismatch` 或 `either`。

hazard 次数和数值 mismatch 数可能因调度变化而波动，不能作为跨 CANN 版本固定常量。case 的必需类型才是硬门槛。

## 6. 提升模型并更新清单

先保留本次原始构建目录，然后将唯一 OM 放入对应 case 目录。不要混入 ATC 日志或 kernel_meta。

```bash
cp "$OUT/baseline/models/"*.om benchmarks/matmul/models/baseline/

for CASE in cross_core_mod4 cross_core_large_offset l43_no_m_to_v \
            l46_rm_barrier_v l54_no_v_to_m l63_no_m_to_v
do
  cp "$OUT/$CASE/models/"*.om "benchmarks/matmul/models/$CASE/"
done

cd benchmarks/matmul/models
find . -mindepth 2 -maxdepth 2 -type f -name '*.om' -print0 \
  | sort -z | xargs -0 sha256sum | sed 's#  \./#  #' > SHA256SUMS
sha256sum -c SHA256SUMS
cd ../../..
```

为每次提升新建 `docs/records/<date>-matmul-<run-id>.md`，可复制[记录模板](records/TEMPLATE.md)。必须记录输入配置哈希、七个 CCE/OM 哈希、完整命令、环境、CCEC 注入日志位置和验证报告。

## 7. 完整回归测试

精选模型更新后执行：

```bash
./scripts/build.sh
python3 tools/bench.py prepare --force

./scripts/run_case.sh baseline --tool none --run-id baseline_direct_<RUN_ID>
./scripts/run_case.sh baseline --tool racecheck --run-id baseline_racecheck_<RUN_ID>
./scripts/run_case.sh cross_core_mod4 --tool racecheck --run-id smoke_<RUN_ID>
./scripts/run_matrix.sh --tool racecheck --run-id matrix_<RUN_ID>
```

主要输出：

```text
artifacts/runs/matrix_<RUN_ID>/
├── <case>/
│   ├── console.log
│   ├── racecheck.log
│   ├── result/output_0.bin
│   └── summary.json
├── matrix_summary.json
└── matrix_summary.md
```

检查总结果和所有 case：

```bash
python3 - "$PWD/artifacts/runs/matrix_<RUN_ID>/matrix_summary.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    report = json.load(stream)

assert report["passed"], "matrix failed"
for case in report["cases"]:
    assert case["passed"], case["case"]
    print(case["case"], case["hazards"], case["output"])
PY
```

## 8. 提交前审计

```bash
git status --short
git diff --check
git diff -- README.md docs benchmarks/matmul CHANGELOG.md

cd benchmarks/matmul/models
sha256sum -c SHA256SUMS
```

提交内容应包含 CCE、精选 OM、manifest、`SHA256SUMS`、生成记录和 changelog。不要提交 `artifacts/`、`build/`、临时 kernel_meta 或设备日志。

## 通过标准

一次模型更新只有同时满足以下条件才算完成：

- ATC、CCEC 和环境信息可追溯。
- 注入 diff 只包含预期故障。
- CCEC shim 日志证明目标源码被替换并用真实编译器编译。
- OM 含 sanitizer 映射并能在 Ascend 310P3 上加载。
- baseline 数值精确匹配，且没有超出允许集合的 hazard。
- 每个注入 case 满足 manifest 的 hazard 和数值策略。
- 七 case matrix 总状态为 PASS。
- OM 哈希和生成记录已更新。
