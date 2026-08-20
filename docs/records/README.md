# Model Build Records

本目录保存进入版本控制的模型生成摘要。原始 ATC、CCEC、设备和 sanitizer 日志保留在 Git 忽略的 `artifacts/` 中；摘要必须足以定位原始材料并判断模型是否可以提升。

- [2026-06-09 MatMul 0909_retry](2026-06-09-matmul-0909-retry.md)：当前七个精选 OM 的历史来源和 2026-08-19 复验。
- [2026-08-19 Add/Softmax/GEMM migration](2026-08-19-add-softmax-gemm-migration.md)：三个旧工程的 case 选择、runner 迁移、哈希、验证结果和已知非终止行为。
- [2026-08-20 MatMul baseline stress20](2026-08-20-matmul-baseline-stress20.md)：20 次 baseline racecheck 的告警出现率、稳定指纹和数值一致性。
- [2026-08-20 MatMulV3 local probe](2026-08-20-matmul-v3-local-probe.md)：动态内核两个 tiling key 的优化 IR、同 PIPE_V barrier 检查和运行时验证边界。
- [2026-08-20 TBE/CCE LLVM IR probe](2026-08-20-tbe-cce-llvm-ir-probe.md)：旧式 CCE 的 bitcode 两步提取、O0/O2/sanitizer 对照和七个 MatMul case 验证。
- [记录模板](TEMPLATE.md)：新基准、故障 case 或 CANN 迁移应复制并填写此模板。

生成记录与对应 CCE、OM、manifest、`SHA256SUMS` 和 changelog 应在同一次提交中更新。
