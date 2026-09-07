# 仿生鱼工程 - 更新日志

> 自动维护，每天由星尘光检查项目更新情况并记录。
> 若某天漏检，次日会自动补写遗漏的更新内容。
> 项目开源地址：[Chemcrin/bionic-fish: CTGU 天问 自然与设计 仿生鱼设计与构想](https://github.com/Chemcrin/bionic-fish)

---

## v0.1.0 — 项目初始化（2026-05-25）

- 创建仿生鱼工程项目，初始化 Git 仓库
- 添加 README.md，明确项目定位：面向水下任务演示的仿生虎鲸（体长 50-60cm）
- 添加 .gitignore、LICENSE、需求文档
- 添加初始项目计划书（v1）

## v0.1.1 — 项目计划迭代 & 阶段A硬件清单（2026-05-29）

- 更新项目计划书，新增阶段A硬件采购清单
- 明确硬件平台：STM32F103ZET6 + ESP32 无线中继

## v0.1.2 — 通信方案升级 & 虎鲸定型（2026-05-30）

- v3 版本：通信方案从有线改为 ESP32 无线中继（2.4G 背鳍天线）
- v3.1 版本：确定仿生虎鲸外形（50-60cm）、防水电机曲柄摇杆推进、舵机齿轮组联动背鳍与尾腰转向、腹鳍独立控制
- 采购清单迭代至 v4 版本

## v0.1.3 — 采购清单补充 & 清理（2026-06-04）

- 补充采购清单 v3、项目计划书 v3.2
- 清理冗余整理版文件

---

## v0.1.4 — 项目同步与采购清单更新（2026-06-17）

- 项目同步，更新采购清单
- 里程碑：四六级备考期后首次恢复提交

---

## v0.1.5 — 机构件建模 & HarmonyOS 上位机开发（2026-06-25）

### 本周进展（6月22日 — 6月25日）

**一、曲柄摇杆机构 — 参数化设计与导出 ✅**

基于 CADQuery 建立完整的曲柄摇杆机构模型，关键参数已标定并写入 `parameters.txt`：
- 固定轴距 O₁-O₂：44 mm
- 曲柄半径 O₁-A：10 mm
- 连杆长度 A-B：35 mm
- 摇杆长度 O₂-B：26 mm
- 装配曲柄角：135°，对应摇杆输出角 74.59°
- 销孔直径：Φ3.2 mm，尾轴间隙孔：Φ5.3 mm

共导出 7 个机构件 STEP 文件（含序号），可直接用于 3D 打印或 CNC 加工：
1. `00_crank_rocker_assembly.step` — 整机装配总图
2. `01_frame_tail_motor_support.step` — 机架/尾部/电机支撑座
3. `02_motor_crank.step` — 电机曲柄
4. `03_connecting_rod.step` — 连杆
5. `04_tail_rocker.step` — 尾部摇杆
6. `05_tail_shaft.step` — 尾轴
7. `06_tail_output_horn.step` — 尾部输出摇臂
8. `07_m3_joint_pin.step` — M3 关节销轴（×4）

Python 参数化生成脚本：`crank_rocker_cadquery.py`

**二、U型件建模与装配 🔧**
- 根据曲柄摇杆机构接口，完成 U 型件建模（U 间距 14 mm，竖直杆长 15 mm，腿宽 15 mm，厚 2 mm，顶部半圆 R7.5 mm，中心 Φ4 通孔）
- 导出 U 型件 STEP + STL，可在 SolidWorks 中与曲柄摇杆配合验证

**三、支撑结构与外壁建模 🖨️**
- `尾部支撑_v2.step / .stl` — 尾部支撑迭代至 v2 版
- `右支撑.SLDPRT / .stl` — 右侧支撑件（SolidWorks）
- `测试装配.SLDASM` — SolidWorks 装配验证

**四、虎鲸外壳建模 🐋**
- `虎鲸外壳_500x200.step / .stl` — 外壳主体（500×200 比例虎鲸外形）
- `虎鲸外壳_壳体_500x200.stl` — 壳体镂空版本（可直接切片打印）

**五、HarmonyOS 上位机开发 📱**
- 完成仿生鱼 HarmonyOS 上位机工程源码（ArkTS / Stage 模型），位于 `BionicFishHarmony/`
- **通信层**：`FishSocketClient.ets` 封装官方 `@kit.NetworkKit` WebSocket，连接 ESP32-S3 网关（`ws://192.168.4.1/ws`）
- **协议层**：`FishProtocol.ets` 定义 JSON 帧规范，支持控制帧（throttle/yaw/pitch/mode）、参数帧（tail_amp_limit/servo_trim等）、心跳帧与 ACK 应答
- **UI 层**：`Index.ets` 主控制台，包含深色水下沉浸风 UI、连接管理、方向控制、模式切换、参数调节、遥测显示、固定急停入口
- **视觉设计**：ArkUI 原生毛玻璃（`backgroundBlurStyle`）、连续脉冲动画、大触控面积、沉浸深色光感
- 暂未打包为 `.hap`，需在 DevEco Studio + HarmonyOS SDK API 12+ 环境中构建
- README.md 已同步更新构建步骤与兼容提醒

### 待办
- 曲柄摇杆机构运动学仿真（角度、位移、急回特性分析）
- 各零件干涉检查与配合优化
- 装配体爆炸图与 BOM 表输出
- 采购清单与实际到货零件核对
- 鸿蒙上位机 `.hap` 打包与真机调试
- ESP32 WebSocket 网关固件联调

---

## v0.1.6 — 实物图片 & 尾部支撑建模更新（2026-07-06）

- 📸 新增实物图片，展示项目实际制作进度
- 更新尾部支撑建模至新版本

---

---

## v0.2.0 — 追鲨一号外壳复刻 & STM32 拓展板设计（2026-08-22 ~ 09-02）

> 8 月下旬项目恢复活跃，三大方向并行推进：追鲨一号外壳复刻、SolidWorks 结构建模迭代、STM32 拓展板设计。

### 一、PCB 参考图整理（8/22）📷

- 裁剪 ESP / MCU / 电源 / 稳压 / 开关 / USB / 串口等模块参考图（`crop_*.png` 共 7 张）
- 8/26 补充 PCB 参考图：`pcb_esp.png`、`pcb_mid.png`

### 二、追鲨一号外壳复刻 v1（8/25-8/26）🐋

- 视频参考帧提取：`video_extract_probe.py`、`extract_keyframes.py`（8/25 晚）
- 参考图整理：`curate_refs.py` + `shark_chaser_modeling_refs/`
- `mesh_to_step.py` 网格→STEP 转换，生成三段式外壳复刻（`建模/追鲨一号外壳复刻_v1/out/`）：
  - `head_shell.step`（头部）、`body_shell.step`（身体）、`tail_shell.step`（尾部）
  - `Zhuisha_No1_Assembly.step`（总装） + `zhuisha_no1_v1.step`
- 配套 README 说明复刻流程

### 三、SolidWorks 结构建模迭代（8/23 ~ 9/1）🔧

- 尾部支撑迭代至 **V4R**（8/23）
- **鱼体第二关节** draft 建模（8/23），后续导出 `draft2.STEP / .stl`
- **虎鲸三视图设计稿**（8/28），8/30 导出主视图 / 俯视图 / 左视图
- 新增零件：`地瓜！？.SLDPRT`（8/30）
- 快拆结构方案：`防水盒快拆.SLDPRT`（8/31）、`快拆架.SLDPRT`（9/1）
- 迭代更新：U型件、右支撑V2、测试装配、摇杆主体V1、摇杆底座等

### 四、STM32 拓展板设计（8/26、9/2）🖥️

- 嘉立创 EDA 工程：`STM32拓展板_2026-08-26.epro2`（8/26）
- STM32 天河星核心板方案：`STM32天河星.zip`
- 拓展板 3D 模型：`3D_32拓展_2026-08-26.step`
- 天河星 logo 概念设计：`tianhexing-logo-concept.svg / .png`
- 9/2：解析 epro2 工程（`_epro2_extract/`、`_parsed_components.txt`、`_records.txt`），完成 **引脚分配文档** `STM32拓展板_2026-08-26_引脚分配.md`

### 待办 📋

- 追鲨一号外壳 STEP 转入 SolidWorks 细化，切片打印验证
- 快拆架 + 防水盒快拆装配验证
- STM32 拓展板引脚分配最终核对 → 打样
- ✅ 9/7 全量分批提交（8 个 commit）并推送 GitHub
- 遗留待办：曲柄摇杆运动学仿真、装配干涉检查、爆炸图/BOM、`.hap` 打包、ESP32 网关联调

---

## v0.3.0 — 完整交付包 & Android 上位机（2026-09-07）📦

- **仿生鱼完整项目交付包 v0.3.0**（原交付包目录升级命名）：
  - `Android上位机/`：Kotlin + Jetpack Compose 控制端完整源码、Gradle wrapper、调试 APK（0.2.0 / versionCode 2）、模拟器测试报告与三套真实渲染截图
  - `STM32控制工程/`：STM32F103C8T6 HAL/CubeMX 风格源码、CubeMX 配置与协议测试
  - `交付说明.md`：最终硬件与协议口径（42 步进电机 + TB6612 双 H 桥；N20 已移除；ESP-01S 仅作 Wi-Fi 模块等）+ v0.3.0 版本声明
  - `原始资料/`：STM32 拓展板引脚分配原件
- Android 质量验证：31 个 JVM 单元测试全部通过；13/13 Compose instrumentation 测试通过（Android 15 / API 35 AOSP ATD 模拟器）
- 讲演实物图归档（`讲演实物图.zip`）；根目录补充快拆架模型；`.gitignore` 增加 SolidWorks `~$` 锁文件规则
- ✅ 推送 GitHub 并发布 v0.3.0 Release（交付包 zip 作为发布资产）

## 待观察 📋

- ✅ 8/22-9/2 项目恢复活跃：外壳复刻、结构建模、STM32 拓展板三大方向推进（详见 v0.2.0）
- ✅ 8/22-9/7 全部变更已提交并推送（v0.3.0 Release 已发布）
- 遗留待办：曲柄摇杆运动学仿真、装配干涉检查、爆炸图/BOM、`.hap` 打包、ESP32 网关联调

---

*本日志由星尘光自动维护，每日更新。*
*最后检查：2026-09-07 08:30 CST*
