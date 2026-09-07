# -*- coding: utf-8 -*-
"""极简软件光栅化渲染器（numpy + PIL），用来出预览图核对外形比例。

不依赖任何 3D 引擎：正交/透视投影 + z-buffer + Lambert 着色 + 边缘高光，
足够验证型线、开口位置和各部件的相对比例。
"""
import numpy as np
from PIL import Image


def look_at(eye, target, up=(0, 0, 1)):
    eye = np.asarray(eye, float)
    target = np.asarray(target, float)
    f = target - eye
    f /= np.linalg.norm(f)
    up = np.asarray(up, float)
    r = np.cross(f, up)
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    return eye, np.stack([r, u, -f])          # 行向量 = 相机基


def render(V, F, face_rgb, eye, target, up=(0, 0, 1), size=(1500, 1050),
           mode='ortho', scale=1.0, fov=32.0, bg=(0.055, 0.058, 0.066),
           key=(-0.45, -0.72, 0.52), fill=(0.75, 0.30, 0.15),
           kick=(0.62, 0.55, -0.30), rim_boost=0.20):
    W, H = size
    eye, M = look_at(eye, target, up)
    C = (V - eye) @ M.T                        # 相机空间：-z 为前方
    depth = -C[:, 2]

    if mode == 'ortho':
        px = C[:, 0] * scale + W * 0.5
        py = -C[:, 1] * scale + H * 0.5
    else:
        k = (H * 0.5) / np.tan(np.deg2rad(fov) * 0.5)
        d = np.maximum(depth, 1e-3)
        px = C[:, 0] / d * k + W * 0.5
        py = -C[:, 1] / d * k + H * 0.5

    # 面法线与着色（双面光照，避免朝向问题掩盖几何错误）
    a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    fn = np.cross(b - a, c - a)
    ln = np.linalg.norm(fn, axis=1, keepdims=True)
    fn = fn / np.where(ln < 1e-12, 1, ln)
    view = (a + b + c) / 3.0 - eye
    view /= np.linalg.norm(view, axis=1, keepdims=True)
    fn = np.where((np.einsum('ij,ij->i', fn, view) > 0)[:, None], -fn, fn)

    kd = np.asarray(key, float); kd /= np.linalg.norm(kd)
    fd = np.asarray(fill, float); fd /= np.linalg.norm(fd)
    kk = np.asarray(kick, float); kk /= np.linalg.norm(kk)
    sky = 0.22 * np.clip(fn[:, 2], 0, 1)                    # 顶光，让上壳读得出来
    lam = (1.30 * np.clip(fn @ -kd, 0, 1) + 0.30 * np.clip(fn @ -fd, 0, 1)
           + sky + 0.16)
    # 红色侧逆光 —— 复刻说明里提到的红色棚拍灯，只用来提亮边缘倒角
    red_kick = 0.14 * np.clip(fn @ -kk, 0, 1) ** 3.0
    rim = rim_boost * (1.0 - np.abs(np.einsum('ij,ij->i', fn, view))) ** 3.0
    shade = (np.clip(lam, 0, 2)[:, None] * face_rgb
             + red_kick[:, None] * np.array([0.44, 0.10, 0.12])
             + rim[:, None] * np.array([0.34, 0.30, 0.32]))
    shade = np.clip(shade, 0, 1)

    img = np.tile(np.asarray(bg, np.float32), (H, W, 1))
    zbuf = np.full((H, W), np.inf, np.float32)

    fz = depth[F]
    fx, fy = px[F], py[F]
    x0 = np.floor(fx.min(1)).astype(int); x1 = np.ceil(fx.max(1)).astype(int)
    y0 = np.floor(fy.min(1)).astype(int); y1 = np.ceil(fy.max(1)).astype(int)
    vis = (x1 >= 0) & (x0 < W) & (y1 >= 0) & (y0 < H) & (fz.min(1) > 0)
    order = np.argsort(-fz.max(1))             # 远 -> 近，减少 z 冲突
    order = order[vis[order]]

    for t in order:
        ax, ay = fx[t, 0], fy[t, 0]
        bx, by = fx[t, 1], fy[t, 1]
        cx, cy = fx[t, 2], fy[t, 2]
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        ix0, ix1 = max(x0[t], 0), min(x1[t] + 1, W)
        iy0, iy1 = max(y0[t], 0), min(y1[t] + 1, H)
        if ix0 >= ix1 or iy0 >= iy1:
            continue
        xs = np.arange(ix0, ix1) + 0.5
        ys = np.arange(iy0, iy1) + 0.5
        gx, gy = np.meshgrid(xs, ys)
        w0 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / den
        w1 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / den
        w2 = 1.0 - w0 - w1
        m = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
        if not m.any():
            continue
        z = w0 * fz[t, 0] + w1 * fz[t, 1] + w2 * fz[t, 2]
        sub = zbuf[iy0:iy1, ix0:ix1]
        hit = m & (z < sub)
        if not hit.any():
            continue
        sub[hit] = z[hit]
        img[iy0:iy1, ix0:ix1][hit] = shade[t]

    return Image.fromarray((np.clip(img, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8))


VIEWS = {
    'side':  dict(dir=(0, -1, 0), up=(0, 0, 1)),
    'top':   dict(dir=(0, 0, 1), up=(-1, 0, 0)),
    'front': dict(dir=(-1, 0, 0), up=(0, 0, 1)),
    'rear':  dict(dir=(1, 0, 0), up=(0, 0, 1)),
    'iso':   dict(dir=(-0.62, -0.72, 0.31), up=(0, 0, 1)),
    'iso2':  dict(dir=(0.55, -0.70, 0.45), up=(0, 0, 1)),
    'low':   dict(dir=(-0.70, -0.55, -0.45), up=(0, 0, 1)),
}


def shot(V, F, rgb, view, size=(1500, 1050), margin=1.10, persp=False):
    cfg = VIEWS[view]
    d = np.asarray(cfg['dir'], float)
    d /= np.linalg.norm(d)
    ctr = (V.min(0) + V.max(0)) * 0.5
    rad = np.linalg.norm(V.max(0) - V.min(0)) * 0.5
    if persp:
        eye = ctr + d * rad * 3.4
        return render(V, F, rgb, eye, ctr, cfg['up'], size, mode='persp',
                      fov=30.0)
    eye = ctr + d * rad * 3.0
    _, M = look_at(eye, ctr, cfg['up'])
    C = (V - eye) @ M.T
    sx = size[0] / (C[:, 0].max() - C[:, 0].min()) / margin
    sy = size[1] / (C[:, 1].max() - C[:, 1].min()) / margin
    return render(V, F, rgb, eye, ctr, cfg['up'], size, mode='ortho',
                  scale=min(sx, sy))


def contact_sheet(images, cols=2, pad=10, bg=(14, 15, 17)):
    w = max(im.width for im in images)
    h = max(im.height for im in images)
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w + (cols + 1) * pad,
                              rows * h + (rows + 1) * pad), bg)
    for i, im in enumerate(images):
        r, c = divmod(i, cols)
        sheet.paste(im, (pad + c * (w + pad), pad + r * (h + pad)))
    return sheet
