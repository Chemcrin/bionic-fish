import cv2
from pathlib import Path
import numpy as np
import math

downloads = Path(r"D:\JiJiDown\Download")
videos = list(downloads.glob("*.mp4"))
if len(videos) != 1:
    raise RuntimeError(f"Expected exactly one MP4 in {downloads}, found {len(videos)}")
video = str(videos[0])
root = Path("shark_chaser_modeling_refs")
root.mkdir(exist_ok=True)
segments = {"01_product_and_structure": (110, 210), "02_design_and_build": (330, 510), "03_pool_testing": (510, 700), "04_shark_chase_footage": (700, 835)}
targets = {name: [start + 2 * i for i in range(int((end - start) / 2) + 1)] for name, (start, end) in segments.items()}
for name in segments:
    (root / name).mkdir(exist_ok=True)
cap = cv2.VideoCapture(video)
fps = cap.get(cv2.CAP_PROP_FPS)
indices = {name: 0 for name in segments}
saved = {name: [] for name in segments}
frame_no = 0
while True:
    ok, frame = cap.read()
    if not ok: break
    sec = frame_no / fps
    for name, times in targets.items():
        index = indices[name]
        if index < len(times) and sec >= times[index]:
            timestamp = times[index]
            cv2.imwrite(str(root / name / f"{timestamp:06.1f}s.jpg"), frame, [int(cv2.IMWRITE_JPEG_QUALITY), 95])
            saved[name].append((timestamp, frame.copy()))
            indices[name] += 1
    frame_no += 1
cap.release()
for name, items in saved.items():
    thumbs = []
    for timestamp, frame in items:
        thumb = cv2.resize(frame, (240, 180))
        cv2.putText(thumb, f"{timestamp:05.1f}s", (5, 17), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1, cv2.LINE_AA)
        thumbs.append(thumb)
    columns = 6; rows = math.ceil(len(thumbs) / columns)
    sheet = 255 * np.ones((rows * 180, columns * 240, 3), dtype=np.uint8)
    for i, thumb in enumerate(thumbs):
        row, column = divmod(i, columns)
        sheet[row * 180:(row + 1) * 180, column * 240:(column + 1) * 240] = thumb
    cv2.imwrite(str(root / f"{name}_contact_sheet.jpg"), sheet)
    print(f"{name}: {len(items)} frames")
(root / "README.txt").write_text("Frames extracted every two seconds. Contact sheets are for browsing; individual JPG files retain 640x480 source resolution.\n", encoding="ascii")
