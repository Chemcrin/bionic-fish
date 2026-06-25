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

*本日志由星尘光自动维护，每日更新。*
