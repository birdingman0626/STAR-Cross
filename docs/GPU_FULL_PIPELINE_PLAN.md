# GPU 全流程实施计划

目标：同输入、同科学输出的完整计算时间低于合理 CPU 配置的 **1/3**。当前没有通过该门槛。
执行入口、参数及限制见 [GPU_BATCHED_SEEDING.md](GPU_BATCHED_SEEDING.md)，
已记录测量见 [GPU_RESULTS.md](GPU_RESULTS.md)，研究依据见
[GPU_MIGRATION_REFERENCE_REVIEW.md](GPU_MIGRATION_REFERENCE_REVIEW.md)。

## 当前状态与下一步

已有设备自适应 seeding、hit 投影/保守过滤、CR4 clipping、窗口收集后的拼接、
whitelist 查询和有界流水线。CPU 仍负责建窗、选择/过滤、注释、BAM 和 Solo 的主要工作。
设备拼接保持 read 内窗口顺序；当前收集和注入路径的重复建窗仍是优化对象。

最近的线程初筛显示：GPU 映射在 8 worker 后扩展有限；各自较好设置约为
CPU 16 worker 13.84 s、GPU 16 worker/batch4096 12.81 s。这是单次映射测量，
不能作为完整计算或 3 倍结论。缩批可降低 CPU align 成本，却增加设备等待，
支持在途批缓存压力的解释，尚未确证因果。

G0 首轮预算（[GPU_RESULTS.md](GPU_RESULTS.md) 2026-11-06）：完整文库 CPU 运行中
映射占 63%，STARsolo 计数 21%，BAM 排序 14%；即使映射耗时为零也只有 2.7 倍，
因此仅加速映射无法达到 3 倍。STARsolo 计数在映射后串行处理各线程文件，
是映射之后的下一关键路径；先在 CPU 上并行化并以完整文库 oracle 验证，再评估迁移。

2026-11-07 决定（P7，项目负责人）：保留整作业 3 倍门槛。P4（`537b407`）后
STARsolo 计数为 337 s（原 601 s），整作业约 2575 s，映射之外约 778 s，以
fa904fe 基线计上限约 3.65 倍；要达到 3 倍（≤946 s），映射需约 11 倍加速。
下一步先在 CPU 上缩短 BAM 坐标排序（392 s）；若排序降至约 100 s，映射约 4 倍即可。
排序之前不再新增 GPU kernel；G0 计时仍是 GPU 的下一步。

下一候选按顺序推进，每项单独冻结、验证并测量：

1. 缩小在途 read 状态，缩短载入到对齐的距离；比较 batch/slot 数和峰值内存。
2. 收集时保存有界窗口和 trInit，注入时复用；随后由常驻 GPU 建窗直接供给拼接。
3. 分析选择、特征归属、结果物化和 Solo 的关键路径，再迁移剩余科学计算。
4. 记录 allocation、launch、等待和传输成本；只优化测得的瓶颈。

沿用现有 executor、GpuSeedIndex 和 qualification 工具，不另建调度或验收框架。

## 计时与验收

| 指标 | 边界 |
| --- | --- |
| whole_job | 进程启动到所有输出关闭，含读取、解压、索引、计算和输出 |
| resident_scientific_compute | 同份未解析/未 clipping 的 FASTQ 已在 host RAM、索引就绪后开始；全部科学结果完成且 worker/device joined 后结束 |
| streaming_job | 有限内存、真实存储下的完成时间和持续吞吐 |

resident 计时包括 read 准备、H2D/D2H、重复传输、调度、窗口/拼接/选择、标签、
注释、barcode/UMI、EM、细胞判定及要求的排序。一次性索引初始化和最终文件编码另报；
不能把结果准备、分区落盘或全局归并改称“导出”以移出计时。

冻结 CPU/CUDA binary、源码与 dirty diff、编译器/依赖/驱动、输入、有效索引、
GTF、whitelist、参数、容量和输出合同。CPU oracle 必须包含两边共享的科学修复。
使用现有 scripts/benchmark_cpu_subset.py、scripts/qualification.py 和阶段计时。

正式验收使用预先约定的 10 对运行、每侧一次同等预热和均衡顺序，保存全部轮次：
完整计算 GPU/CPU 的 paired 95% 区间上界严格小于 1/3，才记 COMPUTE_3X_PASS。
另记 WHOLE_JOB_3X_PASS、ALL_REQUESTED_OUTPUTS_PASS 和 MEMORY_BUDGET_PASS。
初筛、profile 和正式 timed build 分开；kernel/worker 时间不可相加代替 wall time。

8 worker 用于衔接历史证据；产品倍率以开发集选定并冻结的合理 CPU 最佳配置为准。
记录背景负载、温度/频率、GPU 其它进程和存储条件。污染轮次保留，另开独立实验；
不人工扣除其它任务占用，不重启已取消的后台采样，也不占用已耗尽的托管 CI。

## 数据与内存合同

- Genome、有效 SA、prefix、junction/注释和 whitelist 按内容版本常驻。
- 固定数量 slot，记录 generation、read ID、偏移、长度和完成事件；消费结束后复用。
  设备记录使用数值偏移，不能复制宿主 Transcript、容器或 Parameters 指针。
- 保留名字、原始碱基/质量及最终标签所需信息。全局 UMI 裁决后按 read ID 回填。
- 正常路径不下载每个 hit 再上传；调试导出单独启用。科学 CPU fallback 报告数量和耗时。
- 大文库有界分区/续算，不要求全部 FASTQ 同时进 VRAM；不静默截断困难 reads 或候选。
- 报告 RSS、commit、VRAM、pinned、临时磁盘及 I/O；WDDM 超额分页不算显存预算通过。
  历史 5M RSS 约29.29/29.62 GiB、commit约29.69/56.37 GiB，不能只看 RSS 判断内存代价。

## 工作包与退出条件

### G0：计时和关键路径

建立上述三种 timer；记录每批/百万 reads 的 launch、allocation/free、传输字节、
stream/device/slot 等待、fallback、活跃 continuation 和有效状态量。
初始化、稳定批、扩容、尾批分别归因；检查 instrumentation 开关不改科学结果。
退出：完整计算可计时，关键路径及内存预算有执行证据。

### G1：hit → SJ → window 常驻

在 gpuSeedHits.cuh、gpuSeedSearch.cu、SeedBatch.cpp 和现有 anchor/window 遍历上迭代。
先采用 reads 间并行、read 内保序；证明无依赖后再做 count/scan/scatter。
保留 seed 优先级、SA 顺序、首次重复胜出、strand、SJ 拆分、窗口合并和淘汰规则。
atomic 到达顺序不能决定 owner；SA 区间不代表连续基因组坐标。

先消除收集/注入重复建窗，再连接设备窗口；缓存覆盖 anchors、成员、顺序及调用上下文，
不得持有可复用 scratch 指针。超预算分批，保留原算法容量和警告。
覆盖染色体边界、重叠 flank、失效窗口、同 bin 多窗口及不同提交顺序。
退出：逐 read anchors/窗口/顺序与 CPU 一致，完整输出通过，报告 D2H、时间和内存变化。

### G2：拼接、延伸、评分和选择

沿用 StitchCore 显式 DFS 与现有桥接，不新增第二套拼接入口。
read 内保留 include/exclude 和窗口顺序：maxScoreMate 跨窗口更新，
trNtotal 的容量 break 会跳过后续窗口；同分同 gLength 时最早窗口胜出。
不能将窗口视为独立任务后无序归并。

比较粗粒度 per-read kernel 与有界 continuation；按工作量分桶，测寄存器、spill、
scratch 和尾部。状态复制只保留变化字段，但不得让兄弟分支共享可变 exon 数组。
保留 score/tie、motif、indel、mate、MAPQ、secondary/supplementary、RNG 和容量语义。
未知依赖不提前剪枝；不引入 beam search 或未经证明的剩余长度上界。

紧凑结果先供既有 serializer；随后按实际成本迁移 multMapSelect/mappedFilter。
局部模型覆盖 lane/grid 边界、bank、有效前缀、别名和重复目标。
退出：中间候选、完整记录/tags 与 CPU 一致，无丢失困难 reads，报告续算与 fallback。
G1+G2 后重算预算；若保留串行部分已占1/3，先调整架构。

### G3：特征归属、barcode/UMI

保留 Gene/GeneFull/Velocity 的全部适用重叠关系，不能改用最近基因。
whitelist 精确查询和候选枚举先批量化；依赖全库丰度的模糊纠错等待全局统计。
稳定 sort/reduce 或查表的 key 必须含 cell、feature、UMI 和歧义状态。
跨批同一分子由一处裁决，标签回填计入成本。

ACGT UMI 可枚举3*L个一碱基邻居替代全对全，但须保持有向 reachability、
计数阈值、root 顺序及各纠错模式；N 单独处理，不能换成无向连通分量。
退出：改变 batch/slot/分区不改变分子、矩阵轴/计数、Velocity 三层和 BAM tags。

### G4：EM、细胞判定和全局结果

EM 保持 double、迭代/收敛顺序和零支持行为；不偷换 fast-math 或非确定性 atomic sum。
ambient、Good-Turing、模拟和 BH 使用全局数据；同 RNG seed 不保证 CPU/GPU 抽样相同。
ties 和拟合不足等统计问题见 [文献审阅](LITERATURE_ALIGNMENT_REVIEW.md)：
科学修复先独立改 CPU、记录结果迁移，再冻结 oracle，不混入性能改造。

阈值附近数值单独审查；精确 GPU 模式未通过时保留显式 CPU 阶段并计时。
退出：所声明模式的全局结果精确通过；仅部分 CPU 保留时仍标混合实现。

### G5：输入、输出与流水

在既有 executor 上复用有界 slot/arena、stream、event；避免逐 read allocation 和同步。
FASTQ 边界、CRLF、坏/长记录、配对、质量和 clipping 保持语义；syscall 可留 host。
结果编码复用既有 writer；BAM 排序和标签归属属于科学结果准备。
大数据优先用小 fixture 或 NAS 顺序读取，不重新复制整库或自动清理用户数据。

### G6：扩大模式，最后评估 Vulkan

先保持当前 CLI 支持域；two-pass、WASP、chimeric 等逐项建立 oracle 和验收。
CUDA 在完整合同下达到目标后才考虑 Vulkan；后端支持不能靠更换评分或容差获得。

## 每个候选的最小证据

小型反例/穷举 → 100K/1M调试 → 5M回归 → 独立同步采样 → 代表性完整文库。
比较 BAM/header/tags、SJ、raw/filtered/EM、轴、科学统计和三个 Velocity 层；
只按既定规则归一运行元数据。prefix 一致不证明全库细胞判定正确。

保留来源、假说、反例、命令/哈希、中间差分、完整输出、时间/内存及继续或拒绝理由。
上游实现的任务组织只作参考；独立组件通过不能升级为完整独立比对器或最终 BAM 通过。
