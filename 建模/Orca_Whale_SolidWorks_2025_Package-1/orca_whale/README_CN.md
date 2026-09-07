# 仿生虎鲸三段式低多边形外壳

本包包含已经生成的 STEP/STL/OBJ、几何校验结果，以及在 Windows 本机驱动 SOLIDWORKS 2025 生成原生文件的 Python 脚本。

## 重要说明

当前 ChatGPT Work 运行环境是 Linux 容器，没有 Windows、SOLIDWORKS 2025 安装、许可证或 COM Server。因此包内的 STEP/STL/OBJ 已在本环境生成并完成网格拓扑检查；真正的 `.SLDPRT/.SLDASM` 不能在这里伪造，必须在已安装并激活 SOLIDWORKS 2025 的 64 位 Windows 10/11 机器执行 `solidworks_2025_build.py` 后生成。

`output/STEP/Whale_Assembly.step` 是脚本直接生成的 AP203 分层装配 STEP。由于本环境没有任何 STEP CAD 内核，它只完成了实体引用完整性与源三角流形检查，仍应在 SOLIDWORKS 的 Import Diagnostics 中回读确认。Windows 脚本会从三个原生零件建立 `Whale.SLDASM`，再由 SOLIDWORKS 自己输出最终 `Whale_Assembly.STEP`。

## V2 参考图改版

本版依据用户提供的“追鲨壹号”草图和实物效果图重构了造型语言，但保留原始 420 mm 尺寸与三段接口。它现在更接近紧凑型水下推进器，而不是写实鱼类：短钝鼻罩、上拱下平的折面舱体、截平尾舱、低后掠控制翼、前后收尖的多片式仿检修盖、外凸卡扣、侧面百叶面板和双尾喷口护环。参考图中的 700 mm 尺度、品牌文字、掀起的大盖板、扎带与贯通水路没有照搬。

仿舱盖、百叶和尾喷口目前都是**不贯穿主壳的视觉/接口预留特征**。双喷口通过尾封口内表面起始的足厚背板与尾段相交，避免竖直 FDM 时在内腔中孤立起印；没有电机、叶轮、流量、密封圈和耐压指标前，不应把它们解释成已经完成的功能水路。

## 设计定义

| 项目 | 数值 |
|---|---:|
| 总长 | 420.00 mm |
| 头段 | X = 0–120 mm |
| 躯干 | X = 120–300 mm |
| 尾段 | X = 300–420 mm |
| 尾舱/喷口 | 主舱封闭于 X = 412 mm；双喷口护环延伸至 X = 420 mm |
| 主体最大直径 | 180.00 mm |
| 主壳法向壁厚 | 1.50 mm |
| 截面边数 | 12 |
| 止口有效插入 | 约 7.2–8.0 mm |
| 止口径向名义间隙 | 0.30 mm |
| 止口/孔标注公差 | ±0.10 mm |
| 装配孔 | 每接口 2 × Ø3.40 mm，沿 X 同轴定位销孔；配 Ø3.0 × 约 30 mm 销 |

主体外/内两层均由真实三角面组成。V2 使用沿 Z 轴上移的 12 边超椭圆截面，形成高拱顶盖、饱满肩线和相对平直的腹部；主壳与阳止口的内层顶点沿面积加权切面法向偏移，并按相邻平面夹角修正距离，而不是简单缩小半径。阴止口则从与阳止口同源的 12 边配合轮廓向外建立足厚壁，避免法向偏置后“参数间隙正确、真实折面卡死”的问题。头段带阳止口，躯干前端带阴止口、后端带阳止口，尾段前端带阴止口。多边形止口本身可防转；两个 Ø3.40 mm 轴向孔设计为装配前预置 Ø3.0 × 约 30 mm 的长定位销，并非装配后可从外部操作的螺钉孔，也不要使用 Ø3.40 mm 实体销。低后掠背鳍、双胸鳍、仿舱盖、百叶肋和尾喷口护环都是封闭实体并与主壳重叠；Windows 脚本会要求 SOLIDWORKS 用 Combine 将每个生产零件合并为单一实体。

## 文件结构

```text
generate_whale_geometry.py      纯 Python 几何、STEP/STL/OBJ 与校验生成器
solidworks_2025_build.py        SOLIDWORKS 2025 COM 自动化脚本
requirements-windows.txt        Windows 端唯一 Python 依赖
run_solidworks_build.ps1        一键运行入口
output/
  STEP/Head.step
  STEP/Body.step
  STEP/Tail.step
  STEP/Whale.step
  STEP/Whale_Assembly.step
  STL/*.stl
  OBJ/*.obj
  Whale_preview.png
  manifest.json
  VALIDATION.txt
native/                         Windows 脚本运行后创建
  Head.SLDPRT
  Body.SLDPRT
  Tail.SLDPRT
  Whale.sldprt
  Whale.SLDASM
  Whale_Assembly.STEP
  solidworks_build_log.json
```

## Windows + SOLIDWORKS 2025 运行

1. 安装并激活 64 位 SOLIDWORKS 2025，至少手动启动一次；确认默认零件和装配体模板已经配置。
2. 安装 64 位 Python 3.11/3.12。
3. 在 PowerShell 进入本目录后执行：

```powershell
py -m pip install -r requirements-windows.txt
py generate_whale_geometry.py --output output
py solidworks_2025_build.py
```

或直接执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\run_solidworks_build.ps1
```

若默认装配模板没有配置：

```powershell
py solidworks_2025_build.py --assembly-template "C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2025\templates\Assembly.asmdot"
```

脚本会检查 SOLIDWORKS 版本、STEP 是否有可选择的实体、`SaveAs3` 的错误码、输出文件是否非空，并写出日志。SOLIDWORKS 2025 的内部主版本通常为 33.x；若不是，脚本会明确警告。

## 关于 `SelectByID2` 与 `SaveAs3`

脚本确实使用 `model.Extension.SelectByID2(...)` 验证每个导入实体和装配组件。不过，SOLIDWORKS 官方说明：在 STEP 导出时选中 face/body 可能只导出选中子集。因此完整模型保存前必须：

1. 激活当前文档；
2. `ClearSelection2(True)`；
3. 调用当前接口 `model.Extension.SaveAs3(...)`。

旧的 `model.SaveAs3(...)` 已过时。把“导出前必须选中模型”机械照搬到完整装配导出，反而可能造成残缺 STEP。

## 几何验证边界

本环境已验证：

- 全长 X 包围盒严格为 0.000–420.000 mm；
- 躯干主体最大横向直径为 180.000 mm；
- 双向的顶点到对侧三角面采样最小距离：Head 1.500/1.523 mm、Body 1.500/1.517 mm、Tail 1.500/1.526 mm；头鼻与尾舱封口的轴向壁厚均为 1.500 mm；
- 120/300 mm 两处止口也按真实折面法向偏置并单独抽检，最薄处分别为 1.560/1.589 mm；前后定位孔轴线的 Y/Z 偏差均为 0.000 mm；
- 止口间隙直接从已生成的阴/阳止口三角网格做 X=120/128/300/308 mm 平面截取并双向量边，不再比较理论轮廓；四个截面的真实总范围为 0.253–0.316 mm，均落在名义 0.30 ± 0.10 mm 范围内，且无边界相交、阳止口全部位于阴止口内部；
- 每个单独壳、止口、孔套与鳍实体均无边界边、无非流形边、无退化三角形，且有正体积；这不等同于相交多实体已经 Boolean 合并；
- 所有 STEP 内部 `#entity` 引用均存在；
- STL 为二进制毫米网格，OBJ 明确标注单位为 mm。

由于无 OCCT/SOLIDWORKS 回读，本环境没有验证 STEP 解析器兼容性、Imported Body 类型、相交多实体的 Boolean Combine 或装配干涉。源 STEP 中每件包含主壳、止口、定位销套、避开孔道的连接肋与鳍等多个相交封闭体；Windows 脚本必须把每件 Combine 为 1 个 solid，失败即停止。首次在 SOLIDWORKS 打开后还应运行 **Tools → Evaluate → Import Diagnostics**，确认所有面正常，再检查止口实测间隙。

## PETG FDM 建议

- 0.4 mm 喷嘴，线宽 0.50 mm，3 道墙正好约 1.50 mm；
- 层高 0.20 mm；
- 先打印接口处 15–20 mm 长的校准切片；
- 止口径向 0.30 mm 是面向 PETG 的稳妥起点。若机器已完成流量、XY compensation 与孔径校准，可通过改动 `FIT_RADIAL_CLEARANCE_MM` 和对应止口截面把试件收紧到 0.20–0.25 mm；
- Ø3.40 mm 定位销孔打印后先用 3.4 mm 铰刀清孔，再配 Ø3.0 mm 定位销；CAD 中的 ±0.10 mm 是尺寸目标而非打印保证，不能代替打印机校准；
- V2 含胸鳍的最大横向跨度约 244 mm；220 mm 平台仍不能平放，建议约 250–260 mm 的有效平台，或将 Body 以 X=120 接口朝下竖直打印；
- 分段建议让接口面贴平台打印；Tail 可让 X=300 接口朝下，尾封口和喷口背板会连续生长，鳍尖仍需局部支撑或把零件旋转 30–45°；
- PETG 大件应使用 brim，并避免把鳍根设计成仅相切面。

## 官方 API 依据

- [SOLIDWORKS 2025 系统要求](https://www.solidworks.com/support/system-requirements)
- [SOLIDWORKS API 的长度以米为基础单位](https://help.solidworks.com/2025/english/api/sldworksapi/Change_Dimension_Example_CSharp.htm)
- [`IModelDocExtension.SelectByID2`](https://help.solidworks.com/2025/English/api/sldworksapi/SOLIDWORKS.Interop.sldworks~SOLIDWORKS.Interop.sldworks.IModelDocExtension~SelectByID2.html)
- [`IModelDocExtension.SaveAs3`](https://help.solidworks.com/2025/English/api/sldworksapi/SolidWorks.Interop.sldworks~SolidWorks.Interop.sldworks.IModelDocExtension~SaveAs3.html)
- [官方 STEP 导入示例](https://help.solidworks.com/2025/english/api/sldworksapi/Import_STEP_File_Example_CSharp.htm)
- [官方装配组件示例](https://help.solidworks.com/2025/English/api/sldworksapi/Add_Component_and_Mate_Example_CSharp.htm)
