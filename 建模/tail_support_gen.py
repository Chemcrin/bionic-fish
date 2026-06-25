"""
仿生鱼 - 尾部支撑 (Tail Support Bracket)
CadQuery 参数化模型 → STEP + STL
"""
import cadquery as cq

# ============================================================
# 可调参数
# ============================================================

# 尾轴
TAIL_SHAFT_DIA      = 8.0    # 尾轴直径
SLEEVE_OD           = 12.0   # 轴套外径
SLEEVE_LENGTH       = 20.0   # 轴套长度
BEARING_BORE_DIA    = 12.2   # 轴承安装孔径 (SLEEVE_OD + 0.2 配合间隙)

# 支撑座主体
BRACKET_W           = 30.0   # 宽 (X 方向)
BRACKET_D           = 25.0   # 深 (Y 方向，轴向)
BRACKET_H           = 35.0   # 高 (Z 方向)
FILLET_R            = 3.0    # 主体圆角

# 安装法兰
FLANGE_TOTAL_W      = 50.0   # 法兰总宽
FLANGE_H            = 8.0    # 法兰高度
FLANGE_D            = 25.0   # 法兰深度 (同主体)
FLANGE_T            = 3.0    # 法兰厚度
MOUNT_HOLE_DIA      = 3.2    # M3 安装孔
MOUNT_HOLE_SPACING  = 20.0   # 孔距中心偏移

# 连杆过孔
LINK_HOLE_DIA       = 6.0    # 连杆孔径
LINK_HOLE_Y_OFF     = 0.0    # 相对尾轴中心的 Z 偏移

# 卡簧槽
CIRCLIP_W           = 1.8    # 槽宽
CIRCLIP_DIA         = 11.0   # 槽底径

# ============================================================
# 建模过程
# ============================================================

print("正在建模...")

# 1. 支撑座主体
body = (
    cq.Workplane("XY")
    .box(BRACKET_W, BRACKET_D, BRACKET_H)
)

# 2. 主体四角倒圆角
body = (
    body
    .edges("|Z")
    .fillet(FILLET_R)
)

# 3. 轴承安装孔 (Y 方向贯通)
body = (
    body
    .faces(">Y").workplane()
    .circle(BEARING_BORE_DIA / 2)
    .cutThruAll()
)

# 4. 左右安装法兰 (一体式)
flange = (
    cq.Workplane("XY")
    .box(FLANGE_TOTAL_W, FLANGE_D, FLANGE_T)
    .translate((0, 0, -BRACKET_H / 2 + FLANGE_T / 2))
)

# 5. 法兰上打安装孔
flange = (
    flange
    .faces(">Z").workplane()
    .pushPoints([
        (-MOUNT_HOLE_SPACING, 0),
        ( MOUNT_HOLE_SPACING, 0),
    ])
    .circle(MOUNT_HOLE_DIA / 2)
    .cutThruAll()
)

# 6. 合并
assembly = body.union(flange)

# 7. 连杆过孔 (从前面穿到后面)
assembly = (
    assembly
    .faces(">Y").workplane()
    .center(0, LINK_HOLE_Y_OFF)
    .circle(LINK_HOLE_DIA / 2)
    .cutThruAll()
)

# 8. 卡簧槽 (轴承位两侧轴向限位)
# 在轴承孔内壁做环形槽
groove = (
    cq.Workplane("XZ")
    .circle(CIRCLIP_DIA / 2)
    .circle(CIRCLIP_DIA / 2 - 1.5)
    .extrude(CIRCLIP_W)
    .translate((0, BRACKET_D / 2 - 5.0 - CIRCLIP_W / 2, 0))
)
assembly = assembly.cut(groove)

groove2 = groove.translate((0, -(BRACKET_D - 10.0 - CIRCLIP_W), 0))
assembly = assembly.cut(groove2)

# ============================================================
# 导出
# ============================================================
step_path = r"C:\Users\CloudyRain\Desktop\待办\仿生鱼工程\建模\尾部支撑_v2.step"
stl_path  = r"C:\Users\CloudyRain\Desktop\待办\仿生鱼工程\建模\尾部支撑_v2.stl"

print(f"正在导出 STEP...")
cq.exporters.export(assembly, step_path)
print(f"✅ STEP 已保存: {step_path}")

print(f"正在导出 STL...")
cq.exporters.export(assembly, stl_path)
print(f"✅ STL 已保存: {stl_path}")

# 打印尺寸摘要
print(f"\n=== 尺寸摘要 ===")
print(f"主体: {BRACKET_W} x {BRACKET_D} x {BRACKET_H} mm")
print(f"法兰总宽: {FLANGE_TOTAL_W} mm")
print(f"轴承孔径: {BEARING_BORE_DIA} mm")
print(f"安装孔距: {MOUNT_HOLE_SPACING * 2} mm (M{MOUNT_HOLE_DIA:.0f})")
print(f"连杆孔径: {LINK_HOLE_DIA} mm")
print(f"卡簧槽宽: {CIRCLIP_W} mm")
