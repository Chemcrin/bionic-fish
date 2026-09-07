# 追鲨一号外壳复刻 v1

本目录是追鲨一号外观渲染图对应的独立建模工作包。外形参数来自外部
`sampleout.zip` 中已经验证过的 Python 网格模型，坐标单位为 mm：

- X：鼻尖到尾部，整机长度 700 mm
- Y：右舷为正，最大翼展约 420.8 mm
- Z：向上，包含把手总高约 299.7 mm
- 壳体壁厚假设：3.2 mm

## 当前产出

`source/` 保留参数化几何和渲染脚本；`out/` 由 `build_segments.py` 生成：

- `zhuisha_no1_v1.stl/.obj/.mtl`：完整外观网格
- `head_shell_mesh.stl`：0–180 mm
- `body_shell_mesh.stl`：180–520 mm
- `tail_shell_mesh.stl`：520–710 mm
- `manifest.json`：尺寸、三角面数和分段记录
- `Zhuisha_No1_Assembly.step`：完整面片化 STEP 参考装配
- `zhuisha_no1_v1.step`：完整外壳 STEP
- `head_shell.step` / `body_shell.step` / `tail_shell.step`：分段 STEP

运行：

```powershell
python build_segments.py
python mesh_to_step.py
```

## SolidWorks 2025

`sw_macro/Import_Zhuisha_No1_ascii.bas` 是 SW 2025 VBA 宏。宏会把 STL 导入
并另存为 `Whale.sldprt`、`Head.SLDPRT`、`Body.SLDPRT`、`Tail.SLDPRT`，再尝试
生成 `Zhuisha_No1_Assembly.SLDASM` 和 STEP 装配体。

当前机器上的 SolidWorks 2025 启动报错 `0xc0000142`，所以 STEP 由
`mesh_to_step.py` 直接写出，没有依赖 SW。宏仍可在 SolidWorks 能正常启动、且
启用 STL 导入后运行，用于另存为 SLDPRT 和生成 SW 装配体。

## 建模边界

这些 STEP/STL 是按照渲染图 v1 做的外观和装配参考；STEP 采用每个源三角面
对应一个平面面的 faceted B-REP，不是参数化 NURBS，也不保证导入后自动成为
水密实体。后续制造级工作需要在 SolidWorks 中完成：

1. 将网格转换或重建为实体；
2. 对头/中/尾接口增加止口、密封槽、螺钉孔和装配间隙；
3. 对前脸格栅、腹部进水槽和尾部推进器开口做实体布尔检查；
4. 再导出最终 STEP 和打印分件。

旧提示词中的 420 mm（0–120/120–300/300–420）是另一条虎鲸规格，未用于本追鲨一号模型。
