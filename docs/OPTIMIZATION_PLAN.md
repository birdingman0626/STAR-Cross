# STAR-Cross CPU / GPU 优化执行计划

日期：2026-10-06。状态：分阶段实施与实测；[本轮结果及保留/拒绝决定](OPTIMIZATION_RESULTS_20261006.md)。目标平台优先 Windows 原生 CPU/CUDA。

本文件统一规定下一轮工作的顺序、交付物和决策条件。科学结果验收沿用[GPU 验收契约](../.agentdocs/workflow/261006-gpu-validation-and-benchmark-plan.md)，本计划不放宽该契约。各阶段按证据进入下一阶段，不要求把所有候选都实现。

2026-10-08：论文与本地教材驱动的查询/索引候选细化见 [LITERATURE_OPTIMIZATION_PLAN.md](LITERATURE_OPTIMIZATION_PLAN.md)。该补充保留本文件的等价性和晋级门槛，先诊断后选择候选；不是已完成实现，也不要求机械执行全部可选路线。

## 1. 目标与已有证据

目标：减少用户实际完成一次 STAR/STARsolo 任务的时间，同时保留比对、定量、细胞判定和 Velocity 结果。分别评估单次冷启动、同进程索引复用及多样本工作流。

| 已有证据 | 可以据此决定的事 | 尚不能据此决定的事 |
| --- | --- | --- |
| 官方 Parabricks 1M RNA：CPU 中位数 21.985 s，GPU 50.937 s | 官方实现没有在该小任务上取得端到端优势 | 不能推断所有数据规模或我们的实现都无效 |
| 官方有效 CUDA trace：内核合计 0.859 s，pinned host allocation API 7.463 s | 调度、初始化和外围阶段值得优先检查 | 无效的 OSRT trace 不能解释具体线程等待原因 |
| 自有常驻 seed 微基准：199,935 queries，未 profiling 的 CPU/GPU 中位数 39.37/8.06 ms，索引 setup 3.776 s | 常驻、批量 seed 搜索值得继续实验 | 4.89 倍局部收益不等于完整 STAR 收益 |
| 自有 Windows CUDA trace：每批内核 0.337–0.351 ms，CPU 字段对照 PASS | 原生 CUDA 搜索可以正确执行，单核函数耗时很小 | 尚未覆盖完整的 post-clipping 自适应请求 |
| 历史单线程采样显示 SA 比较、junction/index 准备较重 | 这些路径是优先调查对象 | 旧采样比例不能代替当前多线程阶段权重 |

证据入口：[原生 seed 与 CPU 审阅](../.agentdocs/workflow/261006-native-seed-gpu-and-cpu-algorithm-review.md)、[官方 CUDA 时间线](../.agentdocs/workflow/261006-parabricks-cuda-timeline.md)、[官方匹配任务 benchmark](../.agentdocs/workflow/261006-parabricks-benchmark-results.md)。本轮开始时重新冻结当前源码及 dirty diff；旧测量只保留原来的适用范围。

## 2. 优先级与依赖

| 阶段 | 工作包 | 进入条件 | 交付物 / 离开条件 |
| --- | --- | --- | --- |
| P0 | B0：基线和完整比较器 | 立即开始 | 当前 CPU 可复现；比较器能发现人为注入的差异 |
| P0 | B1：真实 seed 请求捕获与成本分解 | B0 | 真实请求、操作次数、阶段耗时、批次分布、CPU 回放一致 |
| P1 | C1：搜索暂存空间复用 | B1 确认分配频率 | 单独 patch、完整等价、独立性能测量 |
| P1 | C2：滚动前缀 / 按需缓存 | B1 确认重复前缀工作 | 正反链键精确一致；证明比当前构造更划算 |
| P1 | G1：GPU 回放真实请求 | B1 | 实际查询所有输出字段精确一致；端到端 offload 成本表 |
| P1 | C3：有效索引复用 | B0，且确认跨任务重复准备成本 | 确定可复用边界，构建或复用现有持久化路径并通过失效测试 |
| P2 | G2：CPU 上验证分批调度 | G1 有收益可能，且 C1/C2 对照已冻结 | 新调度器的 CPU 后端与原同步路径完整等价 |
| P2 | G3：接入常驻 GPU 与有界异步执行 | G2 | 完整任务正确性、资源上限、取消/错误/尾批测试通过 |
| P2 | C4：UMI 哈希邻居 | 大组 UMI 确认为显著成本 | 按原模式/顺序精确纠错，真实组收益成立 |
| P3 | Q0：规模曲线、回归与发布判断 | 有通过正确性门槛的候选 | 声明已支持场景、实测收益、CPU/GPU 选择条件 |

逻辑上，C1/C2 与 G1 可独立开展；任何测试不得与影响测量的编译、GPU 任务或大文件哈希同时运行。先做 B0→B1，再根据数据选择下一项。C4、LCP/RMQ、GPU stitching 和压缩不属于无条件实施项。

## 3. B0：冻结基线、补齐比较器

复用 `scripts/benchmark_cpu_subset.py`、现有 expanded scientific comparison、doctest/CTest 和 seed fixture。先盘点已有比较能力，再抽取最小共用函数；不再创建一套行为重复的 benchmark 框架。

1. 保存源码 commit、dirty diff 内容摘要、编译选项、依赖版本、二进制哈希、输入/索引/GTF/whitelist 哈希、全部有效参数及机器资源。生成后冻结二进制，运行中不重建该文件。
2. 对当前支持的原生 CPU profile 重复运行，明确 RNG、read 分配与浮点重复性。固定线程数本身不保证相同线程处理相同 read。
3. 把 BAM、SJ、矩阵、axes、过滤后 barcode、科学统计及 EM 比较纳入同一个版本化验收清单。已有 runner 的 `PASSED_DECLARED_RAW_ARTIFACTS` 不能代表该清单全部通过。
4. 注入缺失层、一个整数改变、次级比对丢失、重复 read、primary/MAPQ/tag 改变、轴错位、NaN 和细胞集合变化；比较器必须失败并输出定位。
5. 所有实验使用新输出目录。运行状态区分 RUNNING / PASS / FAIL / UNVERIFIED；中断或部分输出不能被缓存命中或当作完成。

整数矩阵、SA 返回字段、SJ、BAM 科学字段要求精确一致。浮点比较只使用预先声明且经 CPU 重复性验证的规则；当前相同平台/运算顺序能精确时优先精确，不引入通用百分比豁免。

## 4. B1：捕获真实请求，判断可获益的上限

主要入口：`ReadAlign_oneRead.cpp`、`ReadAlign_mapOneRead.cpp`、`ReadAlign_maxMappableLength2strands.cpp`、`SuffixArrayFuns.cpp`。捕获点放在真实 clipping、mate 组合、qualitySplit 和前缀 lookup 之后、CPU `maxMappableLength()` 调用之前。

请求记录至少包含：格式版本、有效索引 fingerprint/generation、稳定 read/chunk/worker 标识、read generation、请求序号、split/fragment、方向、S/N/初始 L、SA lower/upper、编码后的 read 字节以及 CPU length/lower/upper/multiplicity。重复 QNAME 用稳定内部 ID 区分。记录 CPU shortcut/跳过原因和边界 case 数量，避免只捕获容易搜索的请求。

捕获使用每线程有界缓冲和 chunk 写出，默认关闭；分别支持全量小夹具和按 read ID 确定性抽样。慢 query 的截断不得伪装成完整分布；达到上限时记录 dropped/truncated。捕获和性能计时分开，另测诊断开关自身开销。

输出的诊断包括：

- 每条 read 请求数、依赖轮数、有效前缀宽度、SA 区间大小、比较碱基数、重复请求/前缀比例。
- 每 chunk 每轮可同时提交的请求数、长尾分布和仍活跃的 read 数量。
- 稀疏索引 D 分布、每请求临时分配数、各类 shortcut 及错误数量。
- reference load、junction prepare、clipping、prefix、seed extension、stitching、Solo、排序与压缩/写出阶段成本；独占与包含时间分开。

首先回放实际 CPU 函数，确认捕获/序列化没有改变查询语义。另对非法 bounds、read 生命周期与索引 generation 不匹配做负面测试。

投资判断：若 seeding 在完整任务中比例为 f，理想收益上限为 `1/(1-f+f/s)`，还要加入上传、排队和调度成本。使用当前 profile 的实测 f；不能把历史采样直接代入。若即使理想化估计也达不到目标，就先做 C3 或其他已测热点。

## 5. C1/C2：先验证两项小范围 CPU 优化

### C1：暂存空间复用

当前 `maxMappableLength2strands()` 每次创建三个 vector。D=1 使用固定大小局部存储；D>1 优先复用所属 ReadAlign 的缓冲，容量随需要增长，活跃长度取 `min(pieceLength,D)`。确认重入/递归路径不会共用同一份正在使用的缓冲。不要为此引入新的全局 allocator。

测试空/极短片段、D=1、多种 D、双方向、重复区间及多线程；直接比较返回值与 `storeAligns()` 事件。测分配数、搜索阶段及完整任务时间、峰值 RSS。该改动减少常数开销，不宣称复杂度降低。

### C2：滚动编码与选择性预计算

当前每个查询逐碱基构造最多 k 位前缀，成本 O(Qk)。全宽连续窗口可用 `next=((previous<<2)|base)&mask` 更新；在适用范围内可把前缀生成变为 O(R+Q)。反链沿其读取方向单独推导并对照，不复用一个未经验证的正链公式。

先在捕获请求上比较三种策略：当前直接构造、只缓存实际请求位置、整条有效片段预计算。记录 Q、R、缓存命中率和字节流量。Q 很小时保留直接计算；不得假定 O(R) 总优于 O(Qk)。短前缀、N、fragment spacer、clipping 后偏移、Lind 回退和位宽溢出全部显式处理。read 内容更新或重新拼接时失效，不能仅按指针地址复用。

单元测试枚举短 ACGT 序列/边界并加入 N/spacer、正反链、所有允许 k 和短前缀；用生产原构造生成 oracle。再运行捕获请求逐项对照和完整产物回归。C1 与 C2 分开测量，之后再组合评估。

## 6. G1：真实请求 GPU 回放

复用 `gpuSeedSearch.cu/.h` 与 `star_seed_benchmark`，让相同回放格式可调用生产 CPU 函数或 GPU。保留 synthetic fixture 对四种 strand 组合、packed 边界、sentinel、重复序列和 junction 序列的覆盖。

先保持当前单 kernel 批量设计。每个请求比较 length、完整 lower/upper 与 multiplicity；失败保存最小请求、index generation、CPU/GPU 输出，禁止忽略少量不一致。

扫描批次：1、64、1K、16K、64K、262K；达到显存/host buffer 预算时停止扩容。以真实请求顺序与依赖轮分布测量，不靠重复容易查询扩大吞吐。分别测 setup、read/query packing、H2D、kernel、D2H、commit，以及一次任务总时间。

把 read-pool 上传从每次 search 中拆出，绑定 chunk generation；多轮只上传新增 query，不能继续每轮复制整个 pool。先验证同步实现，再决定是否采用可复用 pinned buffer。记录 pinned bytes；不把整个约 27 GB 索引长期 pin 住作为默认做法。

进入 G2 的条件：真实请求等价通过，实际批次下 GPU 含 packing/copy 的成本优于优化 CPU，并且加上一次索引上传后的目标规模收益有实测支撑。仅 warm microbenchmark 快、实际批次太小或尾部占主导时暂缓调度器改造。

## 7. C3：减少重复索引准备

先检查现有 genomeGenerate、运行时 junction 插入和索引保存能力，优先复用已有格式/入口。区分磁盘缓存、同进程 CPU 索引复用和 GPU 常驻；三者解决不同成本。同进程复用不能代表当前独立 CLI 已经支持跨进程复用。

识别有效索引由哪些内容决定：原始 index、GTF/显式 SJ、sjdbOverhang、相关参数、生成代码版本、index format、two-pass junction 输入及顺序。只发现“没有新 junction”不足以证明可跳过所有准备。先证明索引数组、坐标、SJ 映射及运行行为一致，再确定早退或持久化边界。

缓存采用内容键、临时目录构建、完整 manifest 与原子完成标记；未完成、hash 不符或版本不兼容一律重建。支持并发构建同键时唯一发布，读取者不见半成品。不自动删除现有索引/缓存。

测试首次 miss、重复 hit、GTF/参数/索引变化、旧格式、截断、中断和并发；对相同任务比较完整科学输出。将首次构建与多样本摊销分别计时。跨进程 CUDA 常驻服务只有在复用收益显著且任务确有需求时另立实现项。

## 8. G2/G3：保留自适应语义的完整集成

### G2：先用 CPU 验证调度拆分

将 seed 流程拆为“生成下一请求→执行搜索→提交结果→更新 read 状态”。状态至少覆盖 split、direction、istart、Lmapped、flagDirMap、sparse iDist、最优 length 和待提交 aligns。原 CPU 路径保留作 oracle。

关键设计是跨 read 批量执行独立请求，同一 read 后继请求必须等前序结果更新。保留每 read 的 `storeAligns()` 顺序、worker 归属、RNG 消耗与后续 stitching/selection 次序。

不为每条活跃 read 创建完整重型 ReadAlign 对象。采用有界轻量 seed 状态和 seed-result 缓冲，完成后按原逻辑交回 worker；若无法在不大幅增加内存或改变算法的前提下拆分，就停止此方案并记录原因。

新调度器先全部调用 CPU executor：比较请求事件序列、seed 候选、BAM、矩阵、细胞集合；测调度开销。正确但开销吞掉预期收益时先简化调度，不急于接 GPU。

### G3：接入 GPU，随后测量重叠

有效索引在任务内只上传一次。chunk read pool、query、result 使用有界缓冲；任务结束 flush 尾批，无限等待凑批不可接受。首次实现单提交队列和同步 commit；通过后最多增加双缓冲，以 timeline 证明 H2D/compute/D2H 重叠是否真实存在。

定义 `off / auto / required` 的 seed backend 行为，属于待实现接口，不能冒充现有参数：已声明的 CPU prefix shortcut 始终保留；auto 可根据事先测定的批次阈值分派；required 对“可 offload 的请求”禁止因 GPU 不可用而静默回退，并记录实际 GPU query 数。0 个 eligible query 明确记为不适用。

提交单位必须有序号与所有权，result 只提交一次。故障时仅可重做未提交工作；CUDA context 致命错误或输出已不可靠时终止，不继续发布成功标记。覆盖尾批、空批、OOM、取消、设备缺失、重复/乱序 completion、错误 generation、两次任务复用。

第一阶段支持范围采用当前已验证的一次比对及 scRNA 配置；two-pass、WASP、SpliceGraph、chimeric、paired overlap 等涉及额外重比对的模式逐项验证，未验证模式给出明确支持状态。CPU-only 构建不得新增 CUDA 运行依赖。

## 9. C4 及后续算法候选

UMI：先统计每 cell/gene 内 distinct UMI 的组大小与配对比较次数。对 CR/Directional 的大组，用哈希查找枚举每条 UMI 的 3b 个单碱基变体，邻居发现从最坏 O(U²) 变为期望 O(Ub)，另加原排序成本。小组保留原扫描可避免哈希开销。

将候选映射回原排序位置后按原方向选择父节点，保留 count inequality、首次命中、纠错链与 equal-count tie。CR、Directional、Graph 独立验证；跨 cell/gene 不共享邻居。测试链、环、等计数、阈值上下界、最大编码位宽、UB 输出、多基因冲突与 filtered calls。该 prefix 数据的 UMI 不是主要热点，因此本项按诊断触发。

LCP/RMQ：只有实测大量重复比较且额外索引内存可接受时进入。现有二分已经维护匹配前缀，完整增强索引的理论 O(m+log K) 不能视为一次小改动可得到的收益。

stitching：覆盖计算、邻接片段合并已经线性扫描；FastResetVector 已做稀疏重置。曾因上界不完整移除的剪枝不得重新加入。动态规划/分支限界需要先证明状态、分数上界和候选完备性。

压缩：只有 BAM profile 显示成本明显时比较已有 libdeflate 选项，再考虑 GPU 压缩。matrix-only profile 不承担该开发收益。CUDA Graphs 仅在 launch 开销成为实际热点后试验；当前 seed 每批单核不优先引入。

## 10. 数据与性能测试矩阵

| 层级 | 数据 / 用途 | 必须回答的问题 |
| --- | --- | --- |
| T0 | 手工与随机 synthetic，包含罕见边界 | 每个算法分支是否语义一致？ |
| T1 | 现有真实 100K / 1M paired prefix | 可以快速回归并复现已有对照吗？ |
| T2 | 从真实库确定性抽取多个位置的同步片段，按规模 1M / 5M / 10M pairs 逐步扩大 | 对 reads 分布、批次与规模变化是否稳健？ |
| T3 | 一个完整代表库，随后按实际支持的物种/模式增加库 | 尾部内存、cell calling 与最终吞吐是否满足交付条件？ |

抽样保持 R1/R2 同步和 read ID 一致，gzip 从流式读取获得片段；不按压缩文件任意字节偏移截断。源数据只读。prefix 与低深度抽样不能资格化完整 EmptyDrops/cell calling；该结论需要 T3。

测试 profile：真实 scRNA unsorted BAM + Gene/GeneFull_Ex50pAS/Velocyto 为主；另覆盖 matrix-only、RNA sorted BAM。所有已宣称支持的附加模式按验收契约测试，不能把一个 profile 推广成全部功能。

每个晋级候选至少三组配对实测，交替 CPU/GPU 或旧/新顺序；warm-up 单列，profiling 单列。比较对象是冻结的优化 CPU，同时保留改动前 CPU 以拆分 C1/C2/GPU 收益。原生 Windows 与 WSL 各自比较，不混成一条速度曲线。

记录整个 process 到全部输出关闭的 wall time、各阶段成本、reads/pairs 单位、峰值 host/device/pinned 内存、GPU 与 CPU query 覆盖及失败情况。校验/哈希不计入算法时间，但包含程序本身需要的缓存验证、载入、上传、flush 和输出索引。重叠计时用区间或墙钟核算，不能把 API/kernel/copy 时间直接相加。

冷启动与“新进程但 OS cache 热”的启动分别标识；不能用重开进程冒充冷文件缓存。共享机器不做全局清缓存。记录并避开影响测量的并发负载。

## 11. 验收、停止与交付

沿用现有工程晋级阈值：目标 profile 端到端时间中位数至少减少 10%，每组配对均改善，其他支持 profile 无未解释的超过 5% 中位数退化；所有科学等价检查通过。阈值是项目选择，不是性能预测。

CPU 的小范围低复杂度改动可在明确减少操作/分配且有重复测量收益时单独保留，标为局部优化；不因此宣称达到 GPU 或产品晋级门槛。性能噪声覆盖收益时记为未证实，补测或撤销候选。

停止条件：任一未解释科学差异；实际 query 批次不足；调度/内存开销超过收益；参考常驻不适合预算；加入复杂度却无可靠端到端收益。失败实验保留结论与触发重试条件，失败实现不加入默认路径。

按最小可审阅改动交付：

1. 基线/比较器补齐与真实 query 捕获。
2. CPU 暂存复用。
3. CPU 前缀策略及实测选择。
4. GPU 实际请求回放与 read-pool 生命周期。
5. 有效索引复用（若收益成立，可先于调度改造）。
6. CPU 分批调度与等价测试。
7. GPU 调度接入、资源边界与失败处理。
8. 规模曲线、完整库资格化、支持矩阵和默认行为决策。

每项保留：问题与假设、代码/参数版本、原始结果、复现命令、比较器报告、性能表、接受/拒绝理由。任务状态用 PLANNED / RUNNING / VERIFIED / REJECTED / DEFERRED。当前逐项结果见 [执行记录](OPTIMIZATION_RESULTS_20261006.md)：C1 VERIFIED 并保留；C2 REJECTED 并撤销；有界真实请求回放 VERIFIED，但完整 GPU 接入 DEFERRED；C3 的手动有效索引复用通过本轮完整等价、配对时间及内存验证，自动缓存机制未实现；更大规模/完整库 Q0 待资格化。GPU 默认值未改变，未提交或推送。
