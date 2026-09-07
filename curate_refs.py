from pathlib import Path
import shutil

src = Path("shark_chaser_modeling_refs")
dst = src / "00_curated_modeling_set"
dst.mkdir(exist_ok=True)

chosen = {
    "01_product_and_structure": [120, 124, 126, 128, 136, 166, 170, 174, 176, 178, 180, 182, 184, 186, 188, 190, 194, 200, 202, 204, 206, 208],
    "02_design_and_build": [354, 358, 360, 362, 366, 370, 374, 378, 380, 382, 384, 388, 390, 398, 400, 404, 410, 412, 418, 422, 426, 430, 434, 440, 444, 448, 452, 456, 458, 464, 466, 470, 476, 478, 480, 482, 486, 488],
    "03_pool_testing": [526, 528, 532, 534, 536, 538, 552, 554, 556, 558, 560, 564, 568, 572, 584, 586, 588, 612, 618, 620, 622, 624, 626, 628, 630, 632, 646, 648, 650, 654, 658, 662, 666, 670, 674, 676, 678],
    "04_shark_chase_footage": [714, 716, 718, 730, 752, 754, 756, 764, 770, 772, 774, 778, 784, 786, 802, 804, 808, 810, 812, 820],
}

labels = {
    "01_product_and_structure": "成品外观、结构草图、电池/控制器与推进器参数",
    "02_design_and_build": "CAD正视/俯视/侧视、加工装配、成品棚拍、灯与相机安装",
    "03_pool_testing": "操控面板、握持方式、水面/水下姿态、灯光与推进效果",
    "04_shark_chase_footage": "最终成品、水下跟拍、自然水域与夜间实拍",
}

manifest = ["追鲨一号建模参考图精选集", "时间点为视频内秒数；图片保留 640x480 原始帧。", ""]
for group, times in chosen.items():
    manifest.append(f"[{group}] {labels[group]}")
    for t in times:
        source = src / group / f"{t:06.1f}s.jpg"
        if not source.exists():
            continue
        target = dst / f"{group}_{t:06.1f}s.jpg"
        shutil.copy2(source, target)
        manifest.append(f"{target.name}\t视频时间 {t:.1f}s")
    manifest.append("")
(dst / "选图清单.txt").write_text("\n".join(manifest), encoding="utf-8")
print(f"curated={len(list(dst.glob('*.jpg')))}")
