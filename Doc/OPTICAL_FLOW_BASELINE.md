# 【已过时，仅供参考】光流开发基线与资源核对

> **已过时，仅供参考；当前状态、需求、配置和验收结论以 [工程 README](E:/工创赛/迁移野火板/README.md) 为准。**
> 归档标记：2026-10-22。以下保留原阶段目标、交付与验证记录，不代表当前仍待执行、未完成或已经实物验收；正文及嵌套文档中的旧交接指令不再直接执行。与 README 冲突时，以 README 为准。历史版本的测试证据及引用资料仅用于追溯。

2026-10-21，独立 worktree `E:/工创赛/迁移野火板-光流`，分支 `feature/pmw3901-flow`。起点 `19a888b`；接手时主目录 main 干净（仅未跟踪阶段任务书），未复制或覆盖电机源码。开发期间已观察到 main 前进到 `da96ca9`，集成前再次读取、合并，最终在本分支回归，不修改 main。

现有资源保留：UART5 PC12/PD2、USART3 PB10/PB11、扫码触发 PB12、USART1 PA9/PA10、触摸 PH4/PH5/PD11/PD13、LTDC/FMC、SPI5 PF6..9。SPI4 PE5/PE6 是 LTDC G0/G1；SPI2 PB14/PB15 接 USB，不采用。

采用 SPI2 AF5：PB13 SCK=J7-54，PC2 MISO=J7-16，PC3 MOSI=J7-52，PE3 CS=J7-49。核对 V1 核心板原理图第 1/2 页及本工程 BSP；PC2/PC3/PE3 没有当前外设占用。PCLK1=45MHz，/64=703125Hz，Mode 3，MSB first，软件 CS。模块 VCC=3.3V（J45-1），GND=J45-5；原理图 VCC 直接供 VDDIO，禁止接 5V。RESET 板载 4.7k 上拉、不接 MCU，MOT 轮询首版悬空，VREG 不接。按丝印核对实物，编号仅对应资料版本。

资料：用户目录只有两份 PDF 与两张 WEBP，没有代码；芯片节选 11 页最后优化序列截断。Bitcraze Arduino 驱动固定提交 `d322d98d6f61757352d11d922cd194539e165231`（MIT）作为完整优化序列来源；Crazyflie `137cc93f85692de1ee2ede3ded38247ca130f9db` 仅核对 burst 字段顺序，不搬入 LGPL 平台代码。Arduino 的 4MHz 不采用；采用本地 2MHz 上限内的 703.125kHz。Arduino 100ms 序列等待保持，不混用 Crazyflie 的 10ms 和寄存器差异。软件结论均非实物接线或运行证明。

最终集成：已于 `78ecf68` 合入 `da96ca9`，共享代码改动在该版本上完成。实物图片 MIS/MOS 输入/输出文字与原理图缩写存在歧义，最终接线必须以 PMW 芯片16=MOSI输入/18=MISO输出及实物走线确认，详见 OPTICAL_FLOW.md，不能仅凭该基线表接实物。
