# MatMul 偶发告警与非确定性分析

## 1. 目的和范围

本文集中记录 MatMul baseline 和六个故障注入 case 在 2026-08-19 复验中出现的偶发告警、hazard 数量波动和数值输出波动，并给出当前成因分析及后续检查计划。

本文只对已经保存的动态实验记录作结论。`mssanitizer` 在一次运行中没有报告 hazard，不等于静态证明内核无竞争；检测到 hazard 也不等于该次运行必然产生数值错误。

## 2. 证据和术语

主要证据是以下五次完整 racecheck 矩阵：

- `artifacts/runs/migrated_matmul_verified_20260819/`
- `artifacts/runs/migrated_matmul_full_20260819/`
- `artifacts/runs/migrated_matmul_final_20260819/`
- `artifacts/runs/migrated_matmul_stable_20260819/`
- `artifacts/runs/docs_workflow_20260819/`

`l46_rm_barrier_v` 另有三次定向重复：

- `artifacts/runs/l46_repeat_1_20260819/`
- `artifacts/runs/l46_repeat_2_20260819/`
- `artifacts/runs/l46_repeat_3_20260819/`

`migration_smoke_20260819` 是路径和 runner 配置检查失败，不属于 case 行为波动，因此不纳入统计。`migration_smoke_fixed_20260819` 仅验证修复后的 baseline 通路，也不纳入五次完整矩阵。

本文区分三类不稳定：

1. **出现性波动**：同一 case 有时报告 hazard，有时不报告。
2. **计数波动**：hazard 类型稳定存在，但报告条数变化。
3. **功能波动**：相同 OM 和确定性输入产生的 mismatch 数量或输出内容变化。

`PASS`/`FAIL` 是 manifest 预期检查结果，不等同于进程成功或失败。早期 manifest 对 hazard 集合或输出要求过严时，runner 返回 0、输出存在且可比较的 case 也可能显示 `FAIL`。

## 3. 完整矩阵观测

每个单元格格式为 `hazard; mismatch/16384`；`none` 表示该次 racecheck 没有解析到 hazard。

| Case | verified | full | final | stable | docs_workflow |
| --- | --- | --- | --- | --- | --- |
| `baseline` | `UB_WAR=2; 0` | `none; 0` | `none; 0` | `none; 0` | `none; 0` |
| `cross_core_mod4` | `GM_WAW=4, UB_WAR=4; 16305` | `GM_WAW=4; 13753` | `GM_WAW=4; 16305` | `GM_WAW=4; 12221` | `GM_WAW=4; 10184` |
| `cross_core_large_offset` | `GM_WAW=7; 15527` | `GM_WAW=7; 15651` | `GM_WAW=7; 15651` | `GM_WAW=7; 15537` | `GM_WAW=7, UB_WAR=2; 15784` |
| `l43_no_m_to_v` | `UB_RAW=55, UB_WAW=8; 0` | `UB_RAW=53, UB_WAW=8; 0` | `UB_RAW=61, UB_WAR=2, UB_WAW=10; 0` | `UB_RAW=63, UB_WAW=8; 0` | `UB_RAW=53, UB_WAR=2, UB_WAW=10; 0` |
| `l46_rm_barrier_v` | `none; 0` | `UB_WAR=1; 0` | `UB_WAR=2; 0` | `none; 0` | `UB_WAR=2; 0` |
| `l54_no_v_to_m` | `UB_RAW=64; 0` | `UB_RAW=64; 0` | `UB_RAW=64; 0` | `UB_RAW=64; 0` | `UB_RAW=64; 0` |
| `l63_no_m_to_v` | `UB_WAR=12; 2035` | `UB_WAR=8; 0` | `UB_WAR=9; 0` | `UB_WAR=2; 2038` | `UB_WAR=8; 2040` |

三次 `l46_rm_barrier_v` 定向重复均为 `none; 0/16384`。合并五次完整矩阵和三次定向重复后，该 case 的八次观测为：五次无 hazard、一次 `UB_WAR=1`、两次 `UB_WAR=2`，输出始终精确匹配。

## 4. 分 case 分析

### 4.1 `baseline`

**已观测事实**

- 五次完整矩阵中一次报告 `UB_WAR=2`，其余四次没有 hazard。
- 所有 baseline 结果均为 `0/16384` mismatch。
- 告警报告涉及 `PIPE_MTE3` 在 baseline CCE line 37 的 UB 读取，以及 `PIPE_V` 在 line 52 的 UB 写入；已见地址包括读偏移 `0xa00` 和写偏移 `0xac0`、`0xae0`。
- 发生告警的 `verified` 运行当时把 baseline 的 `allowed_hazards` 设为空，因此显示 `FAIL`；其 runner 返回值和数值输出均正常。当前 manifest 允许可选 `UB_WAR`，但仍要求输出精确匹配。

**当前分析**

line 37 的 `copy_ubuf_to_cbuf` 读取 A 阶段的 UB 数据，line 52 的 `scatter_vnchwconv_b16` 在后续 B 布局转换阶段写入复用的 UB 地址。line 34--36 建立的是 `V -> MTE3` 依赖，用于启动 line 37；line 37 之后到 line 52 之前没有直接建立 `MTE3 -> V` 的完成依赖。动态调度不同可能使 sanitizer 偶尔观察到前一次读与后一次写重叠。

这支持“存在窄的、调度敏感的 UB 复用窗口”，但现有五次样本不足以区分以下可能性：真实但当前未造成输出错误的 WAR、sanitizer 对异步访问范围的保守报告，或两者共同作用。不能因为当前输出正确就把告警定性为无害，也不能因为一次告警就断言 baseline 已存在功能性错误。

2026-08-20 又完成了 20 次独立 baseline racecheck 压力测试：5 次报告 `UB_WAR`，计数为
`2, 2, 4, 2, 2`，其余 15 次无 hazard；20 次均为 `0/16384` mismatch，输出 SHA-256
完全一致。五次阳性报告都指向 line 37 的 `PIPE_MTE3` 读取和 line 52 的 `PIPE_V` 写入。
这确认了同一指纹的 WAR 具有可复现的出现性波动，但仍需同步诊断版本和未修改 control OM
区分真实依赖缺口与 sanitizer 保守报告。完整结果见
[压力测试记录](../records/2026-08-20-matmul-baseline-stress20.md)。

### 4.2 `cross_core_mod4`

`GM_WAW=4` 在五次完整矩阵中始终存在。该注入把 8 个 block 通过 `block_idx % 4` 映射到 4 个输出区域，因此四组跨核 GM WAW 是结构上确定的。

mismatch 在 `10184--16305` 之间变化。AI Core 之间没有确定的写回完成顺序，同一 GM 区域最后保留哪个 block 的数据取决于当次调度，因此 hazard 拓扑稳定而最终内容不稳定。一次附带的 `UB_WAR=4` 更接近 baseline 内部 UB 复用窗口，与注入的 GM WAW 是不同层次的现象。

### 4.3 `cross_core_large_offset`

`GM_WAW=7` 在五次完整矩阵中始终存在。每个 block 的 GM 写长度扩大后侵入下一个 block 的区域，8 个 block 形成 7 处相邻重叠。

mismatch 在 `15527--15784` 之间变化，符合跨核重叠写入的最终写入者不确定。一次附带 `UB_WAR=2` 可能来自与 baseline 相同的内部 UB 复用窗口。

### 4.4 `l43_no_m_to_v`

五次运行均报告必需的 `UB_RAW` 和 `UB_WAW`，但计数分别在 `53--63` 和 `8--10` 之间变化；两次另有 `UB_WAR=2`。输出始终精确匹配。

该注入删除 line 43--44 的 `MTE2 -> V` 同步。B 数据的 GM-to-UB 搬运与后续向量布局转换及 UB 复用可以并行重叠。实际流水线推进决定 sanitizer 能枚举到多少个访问片段，因此类型稳定而数量变化。当前未观察到数值错误，只说明这几次调度中重叠没有传播为最终可见错误，不否定动态 hazard。

### 4.5 `l46_rm_barrier_v`

这是当前出现性最不稳定的注入。五次完整矩阵报告 `none`、`UB_WAR=1` 或 `UB_WAR=2`；三次额外重复均未报告 hazard，八次输出全部精确匹配。

该 case 删除循环入口 line 46 的 `pipe_barrier(PIPE_V)`，但 line 51 在每次 `scatter_vnchwconv_b16` 前仍保留另一个 V barrier。剩余 barrier 很可能已经提供了足够的 V pipeline 串行化，使删除 line 46 后通常没有可观察效果。少量 WAR 报告与 baseline 的 line 37/line 52 UB 复用告警相似，当前不能证明它们是 line 46 注入独立产生的稳定信号。

因此该 case 适合作为“弱、调度敏感的注入”研究样本，不适合作为要求每次必须检出的确定性正向测试。当前 manifest 不要求 hazard 出现，只允许出现 `UB_WAR`，准确表达了现有证据，但也降低了它作为回归检测样本的强度。

### 4.6 `l54_no_v_to_m`

五次完整矩阵均报告 `UB_RAW=64`，输出均精确匹配。删除 `V -> MTE3` 同步后，向量生产者与 UB-to-L1 消费者之间形成固定范围的重叠；在当前环境和输入下，hazard 类型与数量均稳定。

这是当前最适合用作稳定 racecheck 正向信号的流水线注入 case。输出正确仍不表示 hazard 不存在，只表示已观测运行没有形成最终数值破坏。

### 4.7 `l63_no_m_to_v`

五次运行都报告 `UB_WAR`，计数在 `2--12` 之间；两次输出完全匹配，三次分别产生 `2035`、`2038` 和 `2040` 个 mismatch。

该注入删除 line 63--64 的 `M -> V` 同步。`mad` 完成时间不再显式约束后续 L0C-to-UB 搬运和 `vmuls` 路径。某些调度中矩阵结果在消费前已经完成，输出正确；另一些调度中部分结果尚未就绪，旧值或中间值传播到输出。这是目前最明确的功能非确定性 case。

报告计数变化还表明 sanitizer 捕获到的次生 UB 重叠会随调度改变：部分报告只包含与 baseline 类似的少量 line 37/line 52 重叠，另一些报告包含 line 37 与 line 68 `vmuls` 的多段地址重叠。需要通过逐报告指纹统计进一步拆分主因和次生现象。

## 5. 共性成因

以下前四项由 CCE 依赖关系和现有报告直接支持；第五、六项是待验证假设。

1. MTE2、MTE3、V、MTE1 和 M pipeline 异步推进。删除 event 或 barrier 后，原本的 happens-before 关系不再成立。
2. 多个 AI Core 的完成顺序不确定。跨核 GM 冲突的位置可以固定，但最终写入值不固定。
3. CCE 在不同逻辑阶段复用 UB 地址。缺少跨 pipeline 完成依赖时，即使没有人为注入也可能出现窄的动态 WAR 窗口。
4. sanitizer 按动态执行和访问片段报告 hazard。条数是当次 interleaving 和报告粒度的观测值，不应作为永久精确阈值。
5. **待验证**：sanitizer 插桩可能改变流水线时序，从而放大或缩小 race 窗口。
6. **待验证**：矩阵中的执行顺序、设备队列状态和运行间负载可能影响调度，但现有记录不能把某个运行时因素单独确认为根因。

输入由固定 seed `20260819` 生成，所用 OM 哈希固定，因此 host 输入随机性和模型文件变化不是上述五次矩阵差异的合理解释。

## 6. 下一步检查计划

### P0：定性 baseline 的偶发 `UB_WAR`

1. 在相同环境中单独重复 baseline racecheck 至少 50 次，不与其他 case 混跑。

   ```bash
   cd /root/mini_racy_benchmarks
   source /usr/local/Ascend/cann-9.0.0/set_env.sh
   for i in $(seq -w 1 50); do
     ./scripts/run_case.sh baseline --tool racecheck \
       --run-id "baseline_repeat_${i}_20260819"
   done
   ```

2. 汇总每次 hazard、mismatch、report 中的 pipeline、CCE 行号、block、访问偏移和 serial number。不能只统计总条数；应判断是否始终是 line 37 对 line 52 的同一指纹。
3. 以同一 OM 运行 50 次 `--tool none`，确认无 sanitizer 时输出是否始终一致，并记录输出 SHA-256。该对照只能判断功能稳定性，不能证明无 race。
4. 制作一个仅用于诊断的 baseline 派生 case，在 line 37 完成后、line 52 首次 V 写入前添加明确的 `MTE3 -> V` 依赖；用 CCEC shim 重新编译并回灌为独立 OM，不覆盖精选 baseline。
5. 同时重编译一个“不修改 CCE、仅重新构建”的控制 OM，排除 CANN/CCEC 重建差异。若加同步版本的告警稳定消失而控制版本仍可复现，将明显支持真实调度窗口这一解释。

建议判定：原 baseline 50 次中告警复现且指纹稳定，同时加同步版本 50 次均不出现同类告警，才把该现象提升为“已确认的 baseline 调度敏感 WAR”。如果无法复现，只能记录复现率上界，不能写成“已经证明无竞争”。

### P1：评估注入模型质量

1. `l46_rm_barrier_v` 单独重复至少 50 次，统计 hazard 出现率。另建诊断变体，评估删除 line 51 剩余 barrier 后能否产生稳定、与 baseline 指纹不同的 hazard；这应作为新 case，不应静默替换历史模型。
2. `l63_no_m_to_v` 单独重复至少 30 次，同时记录 hazard 指纹、输出 SHA-256、mismatch 数量和最大绝对误差，分析 `UB_WAR` 计数与功能错误之间是否相关。
3. `l43_no_m_to_v` 重复至少 30 次，确认 `UB_RAW` 和 `UB_WAW` 的出现率，而不是要求计数精确相等。
4. `l54_no_v_to_m` 重复至少 30 次，验证当前 `UB_RAW=64` 是否真能作为稳定环境哨兵；验收重点仍应是类型始终出现，精确数量只作为观测指标。

### P2：隔离运行时因素

1. 分别进行“每个 case 独立进程运行”和“固定顺序完整矩阵运行”，比较 hazard 出现率。
2. 记录每轮 CANN、CCEC、mssanitizer、driver、device、OM SHA-256 和输入 SHA-256，禁止把不同环境的数据直接合并统计。
3. 对关键 case 保存原始 racecheck 日志和结构化地址指纹；后续工具升级时按指纹比较，避免仅比较 hazard 总数。
4. 在 sanitizer 开启和关闭两种模型/运行路径上做功能性 A/B 对照。此项用于评估观测扰动，不用于用无 sanitizer 结果替代 racecheck 结论。

## 7. 当前验收策略

当前 `benchmark.json` 使用以下原则：

- 对结构稳定的故障要求必需 hazard 类型，例如 `GM_WAW`、`UB_RAW`、`UB_WAW`。
- 对已经观察到但不稳定的次生类型放入 `allowed_hazards`，不要求其每次出现。
- 不把动态 hazard 的精确条数作为通过条件。
- 对跨核覆盖 case 要求输出 mismatch；对 baseline 要求精确 match；对调度敏感且可能正确也可能错误的 case 使用 `either`。

这套策略能避免把计数波动误判为工具失败，但不能替代上述根因实验。尤其是 baseline 的可选 `UB_WAR` 和 `l46_rm_barrier_v` 的空必需集合都应在完成 P0/P1 后重新审查。
