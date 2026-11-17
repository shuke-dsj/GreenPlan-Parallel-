# GreenPlan Parallel

**面向多工厂低碳生产计划的多目标并行优化实验项目。** 使用 C++17 实现统一进化核心，通过 Serial、OpenMP、MPI 和 Windows PPL 后端比较种群评估阶段的并行策略。

项目从《第三代遗传算法并行研究与实现》课程作业演化而来。当前版本重新实现了可运行的生产计划模型、可复现实验入口和自动验证。数据为显式定义的**合成场景**，用于研究真实生产计划问题的结构；尚未接入企业数据或生产系统。

## 业务问题

假设一家制造企业拥有 5 个工厂、8 类产品，需要制定多月产量计划。不同工厂产能不同，部分工厂不能生产某些产品；产品有不同售价、成本、市场需求与单位碳排放。管理者需要在三种目标之间选择：

| 目标 | 业务含义 | 冲突来源 |
|---|---|---|
| 最大化利润 | 销售收入减生产成本 | 过量生产增加成本，市场需求限制销量 |
| 最大化产能利用率 | 实际产量 / 可用产能 | 提高利用率可能导致库存和碳排放上升 |
| 最小化碳排放 | 总产量乘单位排放系数 | 降碳通常要求减少产量或改变产品组合 |

决策变量 `x[factory, month, product]` 表示每个工厂每月各产品的产量。约束包含非负、产品兼容性、工厂月产能；采用投影修复保证可行。输出是一组 Pareto 非支配候选计划的目标值，而不是未经权衡的单一“最优”答案。

模型边界：不含库存结转、交期、整数批次、换线时间和物流；需求为全工厂共享的产品月需求；排放仅计算生产直接排放。这是生产计划原型，尚非完整车间调度系统。

## 技术实现

```mermaid
flowchart LR
    A[合成场景与固定种子] --> B[初始化及约束修复]
    B --> C[统一种群评估接口]
    C --> D[Serial / OpenMP / MPI / PPL]
    D --> E[非支配排序与参考方向选择]
    E --> F[交叉、变异与修复]
    F --> C
    E --> G[Pareto 目标 CSV 与计时 CSV]
```

- **统一核心**：所有后端调用同一个 `evaluate`，进化操作集中执行，因此随机数序列不依赖线程调度。
- **OpenMP**：按个体静态分配任务，每次迭代写独立目标槽位，无共享累加器、无嵌套线程团队。
- **MPI**：根进程维护种群，广播连续决策缓冲区，进程计算本地分区，用 `MPI_Gatherv` 回收不等长结果；支持进程数不能整除种群规模。
- **PPL**：MSVC 下使用 `concurrency::parallel_for`，与其他后端共享评估函数。
- **选择策略**：Pareto 分层 + min-max 归一化 + 28 个 Das-Dennis 参考方向 + 垂直距离关联 + 稀疏方向优先补齐。
- **诚实标注算法范围**：称为 **NSGA-III-inspired**，采用算术交叉和稀疏扰动变异；没有实现标准 NSGA-III 的极值点/截距归一化和完整随机 niching，也没有沿用原作业的 SBX/多项式变异。不能直接称为标准 NSGA-III 复现。

## 快速运行

需要 C++17 编译器和 CMake 3.16+。OpenMP、MPI、PPL 按平台可用性启用，未编译的后端会明确报错。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# Linux / GCC
./build/greenplan --backend serial --population 100 --generations 50 --months 12 --seed 42 --output pareto.csv
./build/greenplan --backend openmp --threads 4 --population 100 --generations 50 --months 12 --seed 42 --output pareto-openmp.csv
```

Windows MSVC 的可执行文件通常位于 `build/Release/greenplan.exe`：

```powershell
.\build\Release\greenplan.exe --backend ppl --population 100 --generations 50 --months 12 --output pareto-ppl.csv
```

MPI 构建与运行：

```bash
cmake -S . -B build-mpi -DENABLE_MPI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-mpi --config Release
mpiexec -n 3 ./build-mpi/greenplan --backend mpi --population 101 --generations 30 --output pareto-mpi.csv
```

`101 / 3` 刻意不能整除，用于演示变长收集。Windows 对应 `build-mpi/Release/greenplan.exe`。使用 MinGW 时若运行时报缺少运行库，可使用 `g++ -std=c++17 -O2 -fopenmp -static src/main.cpp -o greenplan.exe`。

CLI 参数：`--backend`、`--population`、`--generations`、`--months`、`--threads`、`--seed`、`--output`。`--threads` 仅控制 OpenMP，MPI 进程数由 `mpiexec` 决定，PPL 工作线程由运行时调度。输出路径的父目录需事先存在。

## 验证与基准

```bash
ctest --test-dir build -C Release --output-on-failure
python scripts/verify.py build/greenplan
python scripts/verify.py build-mpi/greenplan --backend mpi
python scripts/benchmark.py build/greenplan --output results/timings.csv
```

Windows 调整二进制路径即可。验证覆盖约束修复、修复幂等性、零产量目标、利润/排放解析值、支配关系、选择数量、固定种子重复性、后端逐字节一致性、奇数种群及非法参数。**Linux OpenMP/MPI 与 Windows OpenMP/PPL 已通过 [GitHub Actions 验证](https://github.com/shuke-dsj/GreenPlan-Parallel-/actions/runs/44057207682)**。Linux CI 的 MPI 正确性测试允许超订阅，以便在较少 CPU 槽位上运行 3 进程；这不用于性能基准。

本机实测（2026-11-17，Windows / MinGW GCC 8.1，`-O2 -fopenmp -static`，120 个体、30 代、24 月、种子 42/43/44，各 3 次）：

| 后端 | 平均总时间 | 相对串行加速比 |
|---|---:|---:|
| Serial | 0.05727 s | 1.000 |
| OpenMP 1 线程 | 0.05889 s | 0.973 |
| OpenMP 2 线程 | 0.05808 s | 0.986 |
| OpenMP 4 线程 | 0.05723 s | 1.001 |

数据来自 [原始计时 CSV](docs/benchmark-local.csv)。这些任务耗时很短，结果受系统噪声影响，**不能据此声称显著加速**。串行遗传操作与参考方向选择占据总耗时，评估并行尚未改变整体瓶颈。原论文的 9.13 倍和 MPI 3.01 倍属于旧版本历史报告，未作为本版本成绩。

本机验证 Serial/OpenMP/MPI；PPL 已在 Windows MSVC CI 中通过与串行结果一致性验证。

## 目录

```text
src/main.cpp                 模型、进化核心、后端与 CLI
tests/model_test.cpp         模型与选择不变量
scripts/verify.py            后端一致性验证
scripts/benchmark.py         多种子、多线程原始基准
docs/model.md               业务模型与算法假设
docs/engineering.md         从课程作业到工程项目的变化与限制
docs/resume.md              有证据支持的简历表述
docs/profile-snippet.md     GitHub 主页项目介绍
.github/workflows/ci.yml    Linux / Windows 持续验证
```

## 后续工作

优先通过分析器确认热点，再并行子代生成（按个体索引派生随机流）、改进连续内存布局、扩大评估工作量、分离计算和通信计时。业务方面可接入外部数据文件，加入跨期库存和需求满足率。算法方面应与标准 NSGA-III 实现、随机搜索以及确定性小实例最优解比较 HV/IGD 与可行率。

参考：[NSGA-III 原论文](https://doi.org/10.1109/TEVC.2013.2281535)、[OpenMP](https://www.openmp.org/specifications/)、[MPI Forum](https://www.mpi-forum.org/docs/)、[Microsoft PPL](https://learn.microsoft.com/en-us/cpp/parallel/concrt/parallel-patterns-library-ppl)。
