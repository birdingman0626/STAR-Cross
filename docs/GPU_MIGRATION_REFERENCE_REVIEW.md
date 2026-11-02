# CPU → GPU 迁移样板与参考资料调研

调研日期：2026-11-01。对象：STAR-Cross 的原生 Windows CUDA 路径。
状态：源码/文档调研；未编译这些第三方项目，未复现其性能或引入依赖。
本报告补充 [全流程 GPU 计划](GPU_FULL_PIPELINE_PLAN.md)，不替代其正确性、内存及完整计算 3 倍门槛。
§1–§9 的范围是开源工程、论文和官方技术资料；§10 另行追加三本 GPU 教材的对照。

## 1. 结论与选择

推荐组合：**GGML 学后端边界，NVBIO/nvBowtie 学比对流水线，mm2-gb/Gunrock 学不规则调度，CUB 学并行基础操作。**
这些项目解决的问题不同，不能任选一个整体套用，也没有一个能直接提供完整 STARsolo GPU 后端。

| 工程 | 已核查内容 | 对本项目的建议 | 当前决定 |
| --- | --- | --- | --- |
| llama.cpp / GGML | GGML backend 源码中的能力查询、缓冲区、异步复制、事件及图分割 | 保留阶段接口和设备所有权，显式暴露同步与回退 | 借鉴设计，不引入张量运行时 |
| mm2-gb | 此前固定源码与 v1 全文交叉审阅；本轮复核官方入口 | 工作量分桶、重任务调度、独立区间证明 | G2 调度参考，不复制评分/参数 |
| NVBIO / nvBowtie | 固定 README、官方模块目录 | seed、score、select、traceback 的阶段划分与队列 | 优先读设计，暂不作为依赖 |
| Gunrock | 固定 README 中 frontier/advance 示例、负载均衡说明 | 自适应任务及 UMI 图遍历组织 | 小型原型验证，不搬入整个图引擎 |
| CCCL / CUB / Thrust | 固定 README 的组件定位及版本规则 | scan、compact、sort/reduce、scratch 复用 | 优先评估现有 Toolkit 所带版本 |
| CUDA Samples | 固定 README 与官方示例分类 | 验证 streams、Graph、事件等独立机制 | 按热点选择微型实验 |

表中“对本项目的建议”是工程推论，尚不是 STAR 上已验证的收益。

## 2. 源码身份与证据范围

本轮通过 GitHub API 解析 revision，再取得该 revision 的指定文件。
原文件、URL、完整 revision 和文件 SHA-256 保存在忽略目录
`knowledge/gpu-migration-review/sources.json`。公开报告只附自己的概括和定位。
固定 revision 是调研身份，不代表推荐直接采用该开发版本。

| 项目 | 固定 revision | 实际取得的文件 |
| --- | --- | --- |
| ggml-org/ggml | `facdc44a67da17df4433adcc468b424f48e9ded2` | `src/ggml-backend.cpp` |
| gunrock/gunrock | `845c062d40afabc72d129634c556d7d72bb7c68c` | `README.md` |
| NVlabs/nvbio | `5916f3ea387d7d0b6a2661be87c9129ac623ac56` | `README.md` |
| NVIDIA/cccl | `1084ffa2da90634ebdc1eea3f93cfbd47db00d7c` | `README.md` |
| NVIDIA/cuda-samples | `5443602d89ed99aede2e4b7bf329daddeadb320e` | `README.md` |
| Minimap2onGPU/mm2-gb | `8a312e92b5e1cd043950b74306ac2727f7040f46` | 复用已有源码/PDF审阅，非本轮重新冻结 |

GGML backend 实现已经读取；其余本轮固定文件主要是 README，不能把模块目录和示例说明称为逐 kernel 审计。
此前推荐的 `test-backend-ops.cpp` 页面没有取得可用正文，本报告不以它作为已核查的测试实现。
官方在线文档使用访问日期定位；实施时还须记录实际 Toolkit/驱动/工具版本。

## 3. llama.cpp / GGML：后端边界和数据生命周期

来源：[固定 backend 源码](https://github.com/ggml-org/ggml/blob/facdc44a67da17df4433adcc468b424f48e9ded2/src/ggml-backend.cpp)。
这里直接审阅 GGML 独立仓库，不假定它与 llama.cpp 某个 release 内置版本完全相同。

已核查的定位：

- `ggml_backend_buft_alloc_buffer`、`get_alignment`、`get_max_size`：分配能力由 buffer type 提供。
- `ggml_backend_dev_supports_op`：后端显式回答是否支持操作。
- `ggml_backend_tensor_copy_async`：尝试后端异步复制；不能处理时同步两端并进行阻塞复制。
- `ggml_backend_event_record/wait`：用事件表达跨执行流依赖。
- `ggml_backend_sched_split_graph`、`ggml_backend_sched_alloc_graph`：分配/执行前有图分割和存储规划环节。

对 STAR 的具体推论：

1. 延伸当前 `SeedBatchExecutor/GpuSeedIndex`，让窗口、拼接和定量消费设备数值记录；
   接口描述输入/输出、容量、完成事件和 index generation，不向 kernel 暴露宿主对象图。
2. 支持能力应细化到模式、索引及数值合同；`required` 无法满足时明确失败。
   CPU 回退既影响覆盖率也影响耗时，应在结果中可见。
3. 给实际同步路径计数。一个叫 `async` 的函数也可能阻塞，不能根据 API 名称判断重叠成功。
4. 只抽取当前阶段需要的边界。STAR 的变长候选、顺序约束和递归状态并非普通张量操作，
   现在引入完整 GGML 图框架会增加表达和维护成本。

第一项验证应是：同一批设备窗口能跨阶段传递，生命周期错误可被检测；
用时间线证明没有隐含的逐批全局同步。此项是本项目拟议验证，不是上游已提供的保证。

## 4. mm2-gb：按依赖和工作量分解任务

来源：[v1 论文](https://doi.org/10.1101/2024.03.23.586366)、
[固定源码](https://github.com/Minimap2onGPU/mm2-gb/tree/8a312e92b5e1cd043950b74306ac2727f7040f46)。
已有本地交叉审阅位于 `knowledge/MM2_GB_REVIEW.md`，本轮复用其明确限定的版本结论。

论文 §3 和 `gpu/plrange.cu`、`plscore.cu`、`plchain.cu` 对应独立分段、
不同规模任务的 kernel 几何以及较重任务的排序调度。可借鉴它处理工作量不均的方式。
但其对照要求匹配 `max-chain-skip`；含 CPU 工作和回退，公开 chaining 指标也不是完整 STARsolo 时间。

对 STAR：先采集每 read/window 的 seeds、hits、有效转移和候选数量，再决定分桶依据。
初版允许不同 reads 改变执行顺序，结果按稳定 ID 关联；单 read 的原决策顺序仍须保留。
同一 read 的 `maxScoreMate` 和 transcript 容量跨窗口变化，不能照搬 minimap2 的独立切分规则。
只有证明不会跨边界产生有效依赖，才能把窗口内部切成独立任务。

验证：原候选及最终输出一致；记录重队列占比、尾部耗时、额外内存和调度成本。
若分桶开销超过收益，保留简单队列。暂不采用上游固定分段阈值或主存倍数。

## 5. NVBIO / nvBowtie：更接近比对器的数据流

来源：[固定 README](https://github.com/NVlabs/nvbio/blob/5916f3ea387d7d0b6a2661be87c9129ac623ac56/README.md)、
[官方模块目录](https://nvlabs.github.io/nvbio/modules.html)。

README 将 nvBowtie 描述为基于该库重构的 Bowtie2 比对器。模块目录包含
SeedHits、Mapping、Scoring Queues、Select、Traceback、Batch Schedulers、Alignment Sinks、
Packed Streams 和 Ping-Pong Queues。这比单独的矩阵 kernel 更适合作为比对流程参考。

可借鉴的具体方案：把候选发现、评分、选择和 traceback 分成明确记录与队列，
让读序列表示和输出 sink 独立于调度。研究哪些字段直到最终输出才需要，避免每个候选
携带完整宿主对象；同时保留 STAR 所需质量、mate、junction 和标签信息。

局限：官方模块页标注生成于 2015 年，固定 README 仍有 CUDA 9/10、`sm_35` 等说明。
这说明文档存在年代差，不能据此证明 CUDA 13、Blackwell 或本机 Windows 构建可用。
Bowtie2 的算法也不能证明 STAR splice-aware scoring 等价。
决定是先参考数据流，暂不承担整库移植；若以后复制实现，再检查具体文件许可证及依赖。

## 6. Gunrock：不规则计算变成有界 frontier

来源：[固定 README](https://github.com/gunrock/gunrock/blob/845c062d40afabc72d129634c556d7d72bb7c68c/README.md)。
其示例将活跃顶点放入 frontier，用 advance 扩展邻居，筛选下一轮工作，并选择负载均衡策略。
README 可见 Windows 构建入口，但本轮未运行，不能当作本机兼容性验证。

对 STAR 的用途是任务组织：未完成 reads、困难窗口续算、UMI 邻居遍历可以维护
current/next 工作集合，减少大量 inactive lanes。控制集合容量，处理尾批和重复访问。
UMI 的方向、计数阈值及 root 顺序必须继续属于算法合同，不能改成无向连通分量。
小 cell/gene 组可能不足以摊薄队列开销，应分别测小组、大组和倾斜分布。

决定：先用现有 CUDA 与基础原语验证所需模式，不默认引入完整 Gunrock 依赖。

## 7. CCCL：优先复用的基础构件

来源：[固定 README](https://github.com/NVIDIA/cccl/blob/1084ffa2da90634ebdc1eea3f93cfbd47db00d7c/README.md)。
CCCL 包括 Thrust、CUB 和 libcudacxx。CUB 提供 device、block、warp 层次的并行操作。
上游也明确指出部分 ABI 会随版本与编译架构变化，因此不应把这些类型暴露成稳定插件 ABI。

| STAR 需求 | 建议评估的模式 | 必须保留的语义 |
| --- | --- | --- |
| 每 read 不同数量的 windows/hits | count → exclusive scan → scatter | 溢出检查、稳定成员顺序 |
| 去除无效候选 | select/compaction | 原相对次序、首次胜出规则 |
| cell/gene/UMI 汇总 | 整数 key 排序、run-length/reduce | 全局跨批同一分子、完整 key |
| 计数与诊断 | block/device reduction | 整数宽度、无溢出 |
| EM 浮点累计 | 单独研究固定次序的计算 | 不能用普通并行 reduction 默许改变结果 |

先检查 Toolkit 中已有版本，复用 scratch、避免每次分配；固定所选版本后做空输入、
超大偏移、重复 key、分段边界和排序稳定性测试。CCCL 不提供业务等价性的自动保证。

## 8. 官方资料如何服务每一步

| 资料 | 应回答的问题 | 在 STAR 的落点 |
| --- | --- | --- |
| [CUDA Best Practices](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html) | 数据传输、布局、同步是否限制收益？ | G0 归因、G1 常驻中间数据 |
| [CUDA Samples](https://github.com/NVIDIA/cuda-samples/tree/5443602d89ed99aede2e4b7bf329daddeadb320e) | streams、事件、Graph 等机制如何独立验证？ | 小型实验后接回现有 executor |
| [Nsight Systems](https://docs.nvidia.com/nsight-systems/UserGuide/index.html) | GPU 空隙由哪个 host 阶段或等待造成？ | NVTX 标记、CPU/GPU时间线、实际重叠 |
| [Nsight Compute](https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html) | kernel 是带宽、延迟、分支还是资源受限？ | 少量代表 kernel 的计数器分析 |
| [Compute Sanitizer](https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html) | 是否越界、未初始化、错误同步或 shared-memory hazard？ | 每次新阶段的小 fixture 检查 |
| [Khronos Vulkan Guide](https://docs.vulkan.org/guide/latest/) | 设备特性、资源与同步能力如何查询？ | CUDA 路线收益成立后的可移植性评估 |

先 Systems 定位关键路径，再 Compute 分析对应 kernel；profile 会引入开销和 replay，
不使用 profile 下 wall time 作最终性能验收。racecheck 主要检查 shared memory，
不能把没有报告错误解释为已证明所有跨 stream/global-memory 竞争都不存在。

Vulkan 阶段必须核查 int64、FP64、subgroup、memory model 和 scan/sort 的实现成本。
本轮尝试的 Vulkan 单个 compute 示例入口没有取得可用正文，因此只引用核验成功的官方 Guide，
不声称完成 Vulkan shader 审计。当前没有理由在 CUDA 常驻链完成前并行维护第二后端。

## 9. 对实施计划的具体增补建议

追加的官方二进制运行证据见 [Parabricks 本机观察](PARABRICKS_LOCAL_GPU_REVIEW.md)。
它支持窗口/拼接设备化的可行方向，也显示大量小 kernel、同步和 pinned allocation；
因此下一轮在已有原型上优先消除重复建窗、复用 arena、验证批次事件交接。
具体执行顺序以 [全流程计划](GPU_FULL_PIPELINE_PLAN.md) 的最新内容为准。

1. **G0：**给每个 buffer 记录 owner、bytes、generation、last consumer；给同步/CPU回退记次数。
   输出完整计算 timer 与时间线，避免把异步接口等同实际重叠。
2. **G1：**先做 CPU 对照的 count/scan/scatter window 记录，再迁入 GPU。
   上下游接口以有界数值记录为单位，消除大规模 hit D2H。
3. **G2：**借鉴 mm2-gb/Gunrock 的负载分组，以稳定 read ID 调度独立 reads；
   同一 read 的有序状态先保持，确认正确后才研究更细粒度并行。
4. **G3/G4：**优先 CUB 整数分组和计数，之后才处理有序 UMI 图、浮点 EM 和随机模拟。
   任何科学方法修复先在 CPU 建立新合同，再迁移 GPU。
5. 每项先判断基础库是否已有足够能力；只实现缺失的 STAR 语义，减少自有通用代码。

建议先实施 G0/G1，随后 G2；与现有全流程计划一致。本调研不改变参数、评分、默认后端，
不提供新的本地速度证据。所有第三方加速数字只能保留原工作负载与硬件范围，
STAR 的完整计算低于 CPU 1/3 仍需其自身的重复对照、完整科学输出和内存验收。

## 10. 教材补充（2026-11-01 追加）

来源是本地 docling 转换稿（忽略目录 `knowledge/docling/books/`），按章节号定位，不复制正文：

- PMPP：Hwu, Kirk, El Hajj, *Programming Massively Parallel Processors*, 5th ed., 2026；
- Handbook：Wilt, *The CUDA Handbook*, 2013；
- Gems 3：Nguyen (ed.), *GPU Gems 3*, 2008。

Handbook 和 Gems 3 早于现代架构。bank 数、L1 配置、SM 1.x/2.x 限制等硬件细节只作历史背景；
保留的是原理，具体数值以当前 CUDA 文档和本机测量为准。

| 主题 | 教材位置 | 对 STAR 的落点 | 决定 |
| --- | --- | --- | --- |
| 浮点结果一致 | Handbook §8.3（舍入 intrinsic、FMA 融合、函数精度表） | 拼接评分中 `ceil(log2(span)*scale-0.5)` | **已应用**，见下文 |
| local memory 与栈 | Handbook §5.4；PMPP §5.6 | 拼接 kernel 的显式 DFS 帧、每线程 `Tr` 槽与指针数组 | 先用 `-Xptxas -v` 记录 local/spill，再决定是否调寄存器上限 |
| 分支发散与任务粒度 | PMPP §4.5、§16.8 | 当前一线程一 read；窗口数与 `nA` 差异很大 | 先按窗口数/`nA` 统计分布，再比较分桶或 warp-per-read |
| 动态规划波前 | PMPP §16.5–16.8（Smith-Waterman、tile 标志同步、DPX） | 短读拼接是种子组合 DFS 与线性延伸，不是完整 DP 矩阵 | 不用于 G2；SpliceGraph/长读 DP 移植时再参考 |
| filter/scan/compaction | PMPP 第 11–12 章；Handbook §13.6；Gems 3 第 39 章 | window filter 已用 CUB 稳定选择；拼接输出用逐窗口原子预留 | 继续用 CUB；需要可复现内存布局时改 count→scan→scatter |
| merge 与 sort | PMPP 第 13–14 章（co-rank、radix sort） | G3/G4 cell/gene/UMI 整数 key 分组 | 先用 CUB radix sort，不自写 |
| frontier 与私有化 | PMPP §18.5–18.7（frontier、privatization、cooperative groups） | 未完成 reads 续算、UMI 邻居遍历 | 与 §6 Gunrock 结论一致，原型时再测启动开销 |
| 拷贝与计算重叠 | Handbook §6.2–6.7、第 11 章 | 拼接/seeding 批次与 CPU 对齐重叠 | **已应用**：时间线显示设备等待在关键路径上，改为 pinned 一次上传 + 每 worker 4 批环形流水（计划 §7.1） |
| 批量的延迟与吞吐 | PMPP §22.5 | `seedBatchSize` 与每 worker 一个 stream | 批大小随 G2 结果重新扫描 |
| 多模式精确匹配 | Gems 3 第 35 章（病毒特征匹配） | STAR 用后缀数组，不是多模式自动机 | 不采用 |

**浮点一致性的本地教训。** Handbook §8.3 说明两点：编译器会把相邻乘加融合为 FMA，
`__dmul_rn` 等 intrinsic 可以阻止；设备数学库函数按 ulp 误差给出精度，不保证与主机库逐位相同。
STAR 拼接评分已用 `__dmul_rn/__dsub_rn` 并以 `-fmad=false` 编译 `gpuStitch.cu`。
即便如此，5M fixture 的设备逐窗口校验在第 253 个 read 发现 `maxScore` 多 1。
该 read 的基因组跨度恰为 64，设备 `log2(64)` 略小于 6，使 `ceil` 落到另一侧。
二的幂是该公式唯一会正好落在取整边界上的跨度，因此设备端对二的幂直接取指数。
修正后，`STAR_STITCH_VERIFY=1` 的 5M 全量逐窗口比对通过。

由此给计划增加一条规则：凡数学库结果进入 `ceil/floor`、比较或计数，
要么证明不会落在边界，要么在设备端使用精确路径或主机预计算阈值表。
单靠 FMA 开关不足以保证与 CPU 相同。G3/G4 的 EM 和 EmptyDrops 统计迁移前也要逐项检查。

**对 §9 的增补：**

1. 每个新 kernel 记录 `-Xptxas -v` 的寄存器、local memory 和 spill，作为调优前基线。
2. 拼接任务先按每 read 窗口数和每窗口 `nA` 统计分布，用数据决定分桶或 warp 粒度。
3. 设备输出用原子预留时，内存次序随运行变化。宿主只能按索引消费；需要逐字节可复现的转储时，
   改用 count→scan→scatter。
