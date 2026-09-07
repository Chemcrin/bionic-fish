import cv2
from pathlib import Path
import math
import numpy as np

VIDEO = r"D:\JiJiDown\Download\为了追上鲨鱼，我手搓了一台潜水推进器... - 1.为了追上鲨鱼，我手搓了一台潜水推进器...(Av117076731695533,P1).mp4"
OUT = Path("video_probe")
OUT.mkdir(exist_ok=True)

cap = cv2.VideoCapture(VIDEO)
fps = cap.get(cv2.CAP_PROP_FPS)
frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
duration = frames / fps
times = [i * 10 for i in range(math.ceil(duration / 10))]
thumbs = []
for idx, sec in enumerate(times):
    cap.set(cv2.CAP_PROP_POS_MSEC, sec * 1000)
    ok, frame = cap.read()
    if not ok:
        continue
    frame = cv2.resize(frame, (320, 240))
    cv2.putText(frame, f"{sec:05.1f}s", (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0,255,255), 2, cv2.LINE_AA)
    thumbs.append(frame)
    cv2.imwrite(str(OUT / f"sample_{idx:03d}_{sec:05.1f}s.jpg"), frame)
cap.release()

cols = 5
rows = math.ceil(len(thumbs) / cols)
sheet = 255 * np.ones((rows * 240, cols * 320, 3), dtype='uint8')
for i, img in enumerate(thumbs):
    y, x = divmod(i, cols)
    sheet[y*240:(y+1)*240, x*320:(x+1)*320] = img
cv2.imwrite(str(OUT / "contact_sheet_10s.jpg"), sheet)
print(f"duration={duration:.2f}s frames={frames} fps={fps:.3f} samples={len(thumbs)}")

segments = {
    "a_intro": (110, 210),
    "b_design": (330, 510),
    "c_test": (510, 700),
    "d_chase": (700, 835),
}
for name, (start, end) in segments.items():
    seg_dir = OUT / name
    seg_dir.mkdir(exist_ok=True)
    imgs = []
    t = start
    while t <= end:
        cap = cv2.VideoCapture(VIDEO)
        cap.set(cv2.CAP_PROP_POS_MSEC, t * 1000)
        ok, frame = cap.read()
        cap.release()
        if ok:
            frame = cv2.resize(frame, (240, 180))
            cv2.putText(frame, f"{t:05.1f}s", (5, 17), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0,255,255), 1, cv2.LINE_AA)
            imgs.append(frame)
            cv2.imwrite(str(seg_dir / f"{name}_{t:06.1f}s.jpg"), frame)
        t += 2
    cols = 6
    rows = math.ceil(len(imgs) / cols)
    montage = 255 * np.ones((rows * 180, cols * 240, 3), dtype='uint8')
    for i, img in enumerate(imgs):
        y, x = divmod(i, cols)
        montage[y*180:(y+1)*180, x*240:(x+1)*240] = img
    cv2.imwrite(str(OUT / f"{name}_contact_2s.jpg"), montage)
