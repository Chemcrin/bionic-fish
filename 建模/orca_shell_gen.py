"""
虎鲸仿生鱼 - 缩小版外壳放样 (500×200mm)
CadQuery loft 放样生成流线型外壳 → STEP + STL
"""
import cadquery as cq
import math

# ============================================================
# 整体尺寸控制
# ============================================================
TOTAL_LENGTH   = 480.0    # 总长 (mm)，留 20mm 余量 < 500
MAX_HEIGHT     = 180.0    # 最大高度 (mm) < 200
MAX_WIDTH      = 100.0    # 最大宽度 (mm)

# 外壳参数
WALL_THICK     = 2.0      # 壁厚 (mm)
NOSE_TAPER     = 120.0    # 头部锥段长度
TAIL_TAPER     = 160.0    # 尾部锥段长度
BODY_LENGTH    = TOTAL_LENGTH - NOSE_TAPER - TAIL_TAPER  # 中段长度

# 截面站位 (沿 X 轴，从头到尾)
# X=0 为鱼头最前端
STATIONS = [
    # (X位置, 宽度比例, 高度比例)  — 比例相对于 MAX_WIDTH/MAX_HEIGHT
    (0.0,    0.02,  0.02),   # 鼻尖（几乎一个点）
    (30.0,   0.15,  0.20),   # 鼻部
    (60.0,   0.35,  0.45),   # 鼻-头过渡
    (100.0,  0.60,  0.70),   # 头部
    (140.0,  0.80,  0.90),   # 头-身过渡
    (180.0,  0.95,  0.98),   # 前身
    (240.0,  1.00,  1.00),   # 最宽处（身体中部偏前）
    (300.0,  0.95,  0.95),   # 后身
    (340.0,  0.80,  0.85),   # 腰部
    (370.0,  0.60,  0.70),   # 尾根前
    (400.0,  0.40,  0.55),   # 尾根
    (430.0,  0.22,  0.38),   # 尾柄
    (460.0,  0.12,  0.25),   # 尾柄末端
    (480.0,  0.05,  0.10),   # 尾尖
]

print(f"虎鲸仿生鱼外壳 - 缩小版")
print(f"目标尺寸: {TOTAL_LENGTH}mm × {MAX_HEIGHT}mm × {MAX_WIDTH}mm")
print(f"壁厚: {WALL_THICK}mm\n")

# ============================================================
# 生成每个截面的椭圆轮廓
# ============================================================
print("正在生成截面轮廓...")

sections = []
for i, (x_pos, w_ratio, h_ratio) in enumerate(STATIONS):
    w = max(w_ratio * MAX_WIDTH, 1.0)   # 最小 1mm 防止退化
    h = max(h_ratio * MAX_HEIGHT, 1.0)

    # 在 XZ 平面画椭圆截面，然后移动到 X 位置
    # CadQuery 椭圆：在 XY 平面画，X=宽，Y=高
    section = (
        cq.Workplane("XY")
        .ellipse(w / 2, h / 2)
    )
    sections.append((x_pos, section, w, h))
    print(f"  站位 {i+1}: X={x_pos:.0f}mm, 宽={w:.1f}mm, 高={h:.1f}mm")

# ============================================================
# CadQuery loft 放样
# ============================================================
print("\n正在放样...")

# 使用 Workplane loft
# 需要在同一个 workplane 上操作
wp = cq.Workplane("YZ")

# 逐对连接截面，用 spline loft
# CadQuery 的 loft 需要所有截面在同一个 workplane 上
# 我们用 workaround：先在 XZ 平面画所有截面，然后用 makeLoft

from OCP.BRepOffsetAPI import BRepOffsetAPI_ThruSections
from OCP.gp import gp_Pnt
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeEdge, BRepBuilderAPI_MakeWire
from OCP.GC import GC_MakeArcOfEllipse
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt
from OCP.StlAPI import StlAPI_Writer
from OCP.BRepMesh import BRepMesh_IncrementalMesh
import numpy as np

builder = BRepOffsetAPI_ThruSections(True)  # True = solid

for x_pos, w_ratio, h_ratio in STATIONS:
    w = max(w_ratio * MAX_WIDTH, 1.0)
    h = max(h_ratio * MAX_HEIGHT, 1.0)

    # 创建椭圆截面的 wire
    ax = gp_Ax2(gp_Pnt(x_pos, 0, 0), gp_Dir(1, 0, 0))

    # 用参数方程生成椭圆点，然后连成 wire
    n_pts = 32
    pts = []
    for j in range(n_pts):
        angle = 2 * math.pi * j / n_pts
        px = x_pos
        py = (w / 2) * math.cos(angle)
        pz = (h / 2) * math.sin(angle)
        pts.append(gp_Pnt(px, py, pz))

    # 创建 edges
    edges = []
    for j in range(n_pts):
        p1 = pts[j]
        p2 = pts[(j + 1) % n_pts]
        edge = BRepBuilderAPI_MakeEdge(p1, p2).Edge()
        edges.append(edge)

    # 创建 wire
    wire_builder = BRepBuilderAPI_MakeWire()
    for edge in edges:
        wire_builder.Add(edge)
    wire = wire_builder.Wire()

    builder.AddWire(wire)

builder.Build()
shape = builder.Shape()

print("放样完成！正在导出...")

# ============================================================
# 导出
# ============================================================
from OCP.STEPControl import STEPControl_Writer, STEPControl_AsIs
from OCP.IFSelect import IFSelect_RetDone

# 导出 STEP
step_path = r"C:\Users\CloudyRain\Desktop\待办\仿生鱼工程\建模\虎鲸外壳_500x200.step"
writer = STEPControl_Writer()
writer.Transfer(shape, STEPControl_AsIs)
status = writer.Write(step_path)
if status == IFSelect_RetDone:
    print(f"✅ STEP 已保存: {step_path}")
else:
    print(f"❌ STEP 导出失败")

# 导出 STL via CadQuery
from OCP.BRepMesh import BRepMesh_IncrementalMesh
mesh = BRepMesh_IncrementalMesh(shape, 0.5)  # 精度 0.5mm
mesh.Perform()

stl_path = r"C:\Users\CloudyRain\Desktop\待办\仿生鱼工程\建模\虎鲸外壳_500x200.stl"
stl_writer = StlAPI_Writer()
stl_writer.ASCIIMode = False
stl_writer.Write(shape, stl_path)
print(f"✅ STL 已保存: {stl_path}")

# 也做一个抽壳版本 (薄壁外壳)
print("\n正在生成抽壳版本...")
builder2 = BRepOffsetAPI_ThruSections(True)
for x_pos, w_ratio, h_ratio in STATIONS:
    w = max(w_ratio * MAX_WIDTH, 1.0)
    h = max(h_ratio * MAX_HEIGHT, 1.0)

    n_pts = 32
    pts_outer = []
    pts_inner = []
    for j in range(n_pts):
        angle = 2 * math.pi * j / n_pts
        # 外表面
        outer_w = w / 2
        outer_h = h / 2
        # 内表面（减去壁厚）
        inner_w = max(outer_w - WALL_THICK, 0.5)
        inner_h = max(outer_h - WALL_THICK, 0.5)

        pts_outer.append(gp_Pnt(x_pos, outer_w * math.cos(angle), outer_h * math.sin(angle)))
        pts_inner.append(gp_Pnt(x_pos, inner_w * math.cos(angle), inner_h * math.sin(angle)))

    # 外壁 wire
    edges = []
    for j in range(n_pts):
        edge = BRepBuilderAPI_MakeEdge(pts_outer[j], pts_outer[(j+1) % n_pts]).Edge()
        edges.append(edge)
    wire_builder = BRepBuilderAPI_MakeWire()
    for e in edges:
        wire_builder.Add(e)
    builder2.AddWire(wire_builder.Wire())

builder2.Build()
shell_shape = builder2.Shape()

# 导出壳体
# 导出壳体 STL
mesh2 = BRepMesh_IncrementalMesh(shell_shape, 0.5)
mesh2.Perform()
shell_stl = r"C:\Users\CloudyRain\Desktop\待办\仿生鱼工程\建模\虎鲸外壳_壳体_500x200.stl"
stl_writer2 = StlAPI_Writer()
stl_writer2.ASCIIMode = False
stl_writer2.Write(shell_shape, shell_stl)
print(f"✅ 壳体 STL 已保存: {shell_stl}")

# 尺寸摘要
print(f"\n{'='*40}")
print(f"  虎鲸仿生鱼外壳 - 缩小版")
print(f"  总长: {TOTAL_LENGTH} mm")
print(f"  最大高度: {MAX_HEIGHT} mm")
print(f"  最大宽度: {MAX_WIDTH} mm")
print(f"  壁厚: {WALL_THICK} mm")
print(f"  截面站位数: {len(STATIONS)}")
print(f"{'='*40}")
