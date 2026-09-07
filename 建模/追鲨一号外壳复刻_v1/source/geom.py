# -*- coding: utf-8 -*-
"""追鲨一号 — 通用网格构建工具（仅依赖 numpy / scipy）。

坐标系（单位 mm）：
    +X = 机身纵轴向后（x=0 为鼻尖）
    +Y = 右舷（模型左右对称）
    +Z = 向上（z=0 为滑橇触地面）
面片绕序：从物体外部看为逆时针（CCW），法线朝外。
"""
import numpy as np


# ============================================================== 场景 ========

class Scene:
    """累积网格块。每块带 group / material / smooth 标记，可导出 OBJ+MTL / STL。"""

    def __init__(self):
        self.blocks = []

    def add(self, V, F, group, mat, smooth=False):
        V = np.asarray(V, float).reshape(-1, 3)
        F = np.asarray(F, int).reshape(-1, 3)
        if len(F) == 0:
            return
        # 丢弃退化三角形（鼻尖 / 尾尖收敛处会产生）
        a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
        area = np.linalg.norm(np.cross(b - a, c - a), axis=1) * 0.5
        F = F[area > 1e-9]
        if len(F) == 0:
            return
        self.blocks.append(dict(V=V, F=F, group=group, mat=mat, smooth=smooth))

    def add_quads(self, V, Q, group, mat, smooth=False):
        self.add(V, quads_to_tris(Q), group, mat, smooth)

    # ---------------------------------------------------------- 变换 ------
    def apply(self, fn):
        for b in self.blocks:
            b['V'] = fn(b['V'])

    def bbox(self):
        lo = np.min(np.array([b['V'].min(0) for b in self.blocks]), axis=0)
        hi = np.max(np.array([b['V'].max(0) for b in self.blocks]), axis=0)
        return lo, hi

    def stats(self):
        nv = sum(len(b['V']) for b in self.blocks)
        nf = sum(len(b['F']) for b in self.blocks)
        return nv, nf

    def merged(self):
        """返回 (V, F, 每面材质索引, 材质名列表)"""
        Vs, Fs, Ms, off = [], [], [], 0
        names = []
        for b in self.blocks:
            if b['mat'] not in names:
                names.append(b['mat'])
            Vs.append(b['V'])
            Fs.append(b['F'] + off)
            Ms.append(np.full(len(b['F']), names.index(b['mat']), int))
            off += len(b['V'])
        return np.vstack(Vs), np.vstack(Fs), np.concatenate(Ms), names

    # ---------------------------------------------------------- 导出 ------
    def write_obj(self, path, mtllib=None):
        out = []
        if mtllib:
            out.append('mtllib ' + mtllib)
        off, noff = 1, 1
        for b in self.blocks:
            V, F = b['V'], b['F']
            out.append('')
            out.append('g ' + b['group'])
            out.append('usemtl ' + b['mat'])
            for v in V:
                out.append('v %.4f %.4f %.4f' % (v[0], v[1], v[2]))
            if b['smooth']:
                N = vertex_normals(V, F)
                for n in N:
                    out.append('vn %.5f %.5f %.5f' % (n[0], n[1], n[2]))
                out.append('s 1')
                for f in F:
                    i, j, k = f + off
                    a, c, d = f + noff
                    out.append('f %d//%d %d//%d %d//%d' % (i, a, j, c, k, d))
                noff += len(V)
            else:
                out.append('s off')
                for f in F:
                    i, j, k = f + off
                    out.append('f %d %d %d' % (i, j, k))
            off += len(V)
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write('\n'.join(out) + '\n')

    def write_stl(self, path):
        V, F, _, _ = self.merged()
        a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
        n = np.cross(b - a, c - a)
        ln = np.linalg.norm(n, axis=1, keepdims=True)
        n = n / np.where(ln == 0, 1, ln)
        rec = np.zeros(len(F), dtype=np.dtype([
            ('n', '<f4', 3), ('a', '<f4', 3), ('b', '<f4', 3),
            ('c', '<f4', 3), ('att', '<u2')]))
        rec['n'], rec['a'], rec['b'], rec['c'] = n, a, b, c
        with open(path, 'wb') as fh:
            fh.write(b'ZHUISHA-No1 exterior shell'.ljust(80, b' '))
            fh.write(np.uint32(len(F)).tobytes())
            fh.write(rec.tobytes())


def vertex_normals(V, F):
    N = np.zeros_like(V)
    a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    fn = np.cross(b - a, c - a)          # 面积加权
    for k in range(3):
        np.add.at(N, F[:, k], fn)
    ln = np.linalg.norm(N, axis=1, keepdims=True)
    return N / np.where(ln < 1e-12, 1, ln)


def signed_volume(V, F):
    a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    return float(np.einsum('ij,ij->i', a, np.cross(b, c)).sum() / 6.0)


# ============================================================ 基础工具 =====

def quads_to_tris(Q):
    Q = np.asarray(Q, int).reshape(-1, 4)
    return np.vstack([Q[:, [0, 1, 2]], Q[:, [0, 2, 3]]])


def grid_quads(nu, nv, wrap_v=False, keep=None):
    """(nu,nv) 网格的四边形索引；顶点索引 = i*nv + j。

    绕序 [a, d, c, b]：i 增大方向 x j 增大方向 = 外法线。
    keep: (nu-1, nq) 的 bool，False 处挖空。
    """
    nq = nv if wrap_v else nv - 1
    i = np.arange(nu - 1)[:, None]
    j = np.arange(nq)[None, :]
    j1 = (j + 1) % nv
    a = i * nv + j
    b = i * nv + j1
    c = (i + 1) * nv + j1
    d = (i + 1) * nv + j
    Q = np.stack([a, d, c, b], -1).reshape(-1, 4)
    if keep is not None:
        Q = Q[np.asarray(keep, bool).reshape(-1)]
    return Q


def hole_rims(P, Pin, keep, wrap_v=True):
    """为 keep=False 的挖空区生成壳体壁厚边框（外皮 -> 内皮）。

    P / Pin : (nu, nv, 3) 外表面与内表面点阵
    返回 (V, Q)，法线朝孔内 —— 从外面看进去正好看到这圈料厚。
    """
    nu, nv, _ = P.shape
    nq = nv if wrap_v else nv - 1
    keep = np.asarray(keep, bool)
    Vout = P.reshape(-1, 3)
    Vin = Pin.reshape(-1, 3)
    V = np.vstack([Vout, Vin])
    n0 = len(Vout)

    def kept(i, j):
        if i < 0 or i >= nu - 1:
            return False
        if wrap_v:
            j %= nq
        elif j < 0 or j >= nq:
            return False
        return bool(keep[i, j])

    quads = []
    for i, j in np.argwhere(~keep):
        j1 = (j + 1) % nv
        a, b = i * nv + j, i * nv + j1
        c, d = (i + 1) * nv + j1, (i + 1) * nv + j
        # 被挖掉的四边形，其 CCW 外向边序为 a->d, d->c, c->b, b->a
        for (va, vb, ni, nj) in ((a, d, i, j - 1), (d, c, i + 1, j),
                                 (c, b, i, j + 1), (b, a, i - 1, j)):
            if kept(ni, nj):
                quads.append([va, vb, vb + n0, va + n0])
    return V, np.asarray(quads, int).reshape(-1, 4)


# ============================================================ 截面 / 放样 ==

def superellipse_ring(w, ztop, zwl, zbot, nup, nlo, k):
    """一个封闭截面环，共 4k 个点。

    j=0   顶部中线      j=k   右舷最宽点 (y=+w, z=zwl)
    j=2k  底部中线      j=3k  左舷最宽点
    """
    a = np.linspace(0.0, np.pi / 2, k + 1)[:-1]          # k 个
    hu, hl = ztop - zwl, zwl - zbot
    ca, sa = np.cos(a), np.sin(a)

    ar = a[::-1]                                          # 顶 -> 右舷
    q1y = w * np.cos(ar) ** (2.0 / nup)
    q1z = zwl + hu * np.sin(ar) ** (2.0 / nup)
    q2y = w * ca ** (2.0 / nlo)                           # 右舷 -> 底
    q2z = zwl - hl * sa ** (2.0 / nlo)
    q3y, q3z = -q2y[::-1], q2z[::-1]                      # 底 -> 左舷
    q4y, q4z = -q1y[::-1], q1z[::-1]                      # 左舷 -> 顶

    return (np.concatenate([q1y, q2y, q3y, q4y]),
            np.concatenate([q1z, q2z, q3z, q4z]))


def pchip(xs, ys):
    from scipy.interpolate import PchipInterpolator
    return PchipInterpolator(np.asarray(xs, float), np.asarray(ys, float))


def surface_normals(P, wrap_v=True):
    """(nu,nv,3) 点阵的外法线（中心差分）。"""
    du = np.gradient(P, axis=0)
    if wrap_v:
        dv = (np.roll(P, -1, axis=1) - np.roll(P, 1, axis=1)) * 0.5
    else:
        dv = np.gradient(P, axis=1)
    N = np.cross(du, dv)
    ln = np.linalg.norm(N, axis=2, keepdims=True)
    return N / np.where(ln < 1e-12, 1, ln)


def sample_grid(P, fi, fj, wrap_v=True):
    """双线性采样 (nu,nv,3) 点阵；fi/fj 为浮点索引。"""
    nu, nv, _ = P.shape
    fi = np.clip(np.atleast_1d(np.asarray(fi, float)), 0, nu - 1.0001)
    fj = np.atleast_1d(np.asarray(fj, float))
    i0 = fi.astype(int)
    ti = (fi - i0)[:, None]
    if wrap_v:
        fj = fj % nv
        j0 = fj.astype(int)
        j1 = (j0 + 1) % nv
    else:
        fj = np.clip(fj, 0, nv - 1.0001)
        j0 = fj.astype(int)
        j1 = j0 + 1
    tj = (fj - j0)[:, None]
    i1 = np.minimum(i0 + 1, nu - 1)
    return ((P[i0, j0] * (1 - tj) + P[i0, j1] * tj) * (1 - ti) +
            (P[i1, j0] * (1 - tj) + P[i1, j1] * tj) * ti)


# ============================================================ 扫掠 / 体素 ==

def frames(path, up=(0, 0, 1)):
    """沿折线的平行传输标架，返回 (T, U, W)。"""
    P = np.asarray(path, float)
    T = np.gradient(P, axis=0)
    T /= np.linalg.norm(T, axis=1, keepdims=True)
    up = np.asarray(up, float)
    U = np.zeros_like(T)
    ref = up if abs(np.dot(T[0], up)) < 0.95 else np.array([1.0, 0, 0])
    u = np.cross(ref, T[0])
    u /= np.linalg.norm(u)
    for i in range(len(T)):
        if i:                                   # 平行传输，避免截面扭转
            u = u - np.dot(u, T[i]) * T[i]
            n = np.linalg.norm(u)
            u = u / n if n > 1e-9 else np.cross(np.array([0, 0, 1.0]), T[i])
        U[i] = u
    W = np.cross(T, U)
    return T, U, W


def sweep(path, profile, up=(0, 0, 1), cap=True, scale=None):
    """把 2D 闭合截面 profile[(u,w)] 沿 path 扫掠。scale: 每站缩放系数。"""
    P = np.asarray(path, float)
    prof = np.asarray(profile, float)
    T, U, W = frames(P, up)
    s = np.ones(len(P)) if scale is None else np.asarray(scale, float)
    pts = (P[:, None, :]
           + (prof[None, :, 0, None] * s[:, None, None]) * U[:, None, :]
           + (prof[None, :, 1, None] * s[:, None, None]) * W[:, None, :])
    nu, nv = pts.shape[0], pts.shape[1]
    V = pts.reshape(-1, 3)
    F = list(quads_to_tris(grid_quads(nu, nv, wrap_v=True)))
    if cap:
        for end, base in ((0, 0), (nu - 1, (nu - 1) * nv)):
            ctr = pts[end].mean(0)
            ci = len(V)
            V = np.vstack([V, ctr])
            for j in range(nv):
                j1 = (j + 1) % nv
                F.append([ci, base + j1, base + j] if end == 0
                         else [ci, base + j, base + j1])
    return V, np.asarray(F, int)


def circle_profile(r, n=32, expo=2.0):
    a = np.linspace(0, 2 * np.pi, n, endpoint=False)
    ca, sa = np.cos(a), np.sin(a)
    return np.stack([np.sign(ca) * np.abs(ca) ** (2 / expo) * r,
                     np.sign(sa) * np.abs(sa) ** (2 / expo) * r], -1)


def rect_profile(a, b, expo=8.0, n=28):
    """圆角矩形截面（超椭圆近似），半宽 a、半高 b。"""
    return circle_profile(1.0, n, expo) * np.array([a, b])


def box(lo, hi):
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    V = np.array([[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0],
                  [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]], float)
    Q = np.array([[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4],
                  [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]], int)
    return V, quads_to_tris(Q)


def cylinder(p0, p1, r, n=32, cap=True, expo=2.0):
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    path = np.stack([p0, p0 * 0.5 + p1 * 0.5, p1])
    return sweep(path, circle_profile(r, n, expo), cap=cap)


def annulus_tube(p0, p1, r_out, r_in, n=40):
    """带壁厚的圆筒（护罩）：外壁 + 内壁 + 前后环形端面。"""
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    ang = np.linspace(0, 2 * np.pi, n, endpoint=False)
    d = p1 - p0
    L = np.linalg.norm(d)
    t = d / L
    ref = np.array([0, 0, 1.0]) if abs(t[2]) < 0.9 else np.array([1.0, 0, 0])
    u = np.cross(ref, t)
    u /= np.linalg.norm(u)
    w = np.cross(t, u)
    ring = np.cos(ang)[:, None] * u + np.sin(ang)[:, None] * w
    V = np.vstack([p0 + ring * r_out, p1 + ring * r_out,
                   p0 + ring * r_in, p1 + ring * r_in])
    F = []
    for j in range(n):
        j1 = (j + 1) % n
        F += [[j, n + j, n + j1], [j, n + j1, j1]]                      # 外壁
        F += [[2 * n + j, 3 * n + j1, 3 * n + j],
              [2 * n + j, 2 * n + j1, 3 * n + j1]]                      # 内壁
        F += [[j, j1, 2 * n + j1], [j, 2 * n + j1, 2 * n + j]]          # 前环面
        F += [[n + j, 3 * n + j, 3 * n + j1],
              [n + j, 3 * n + j1, n + j1]]                              # 后环面
    return V, np.asarray(F, int)


def loft_closed(sections):
    """把一串等点数的闭合截面 (ns, npts, 3) 放样成闭合壳体（含前后端盖）。"""
    S = np.asarray(sections, float)
    nu, nv, _ = S.shape
    V = S.reshape(-1, 3)
    F = list(quads_to_tris(grid_quads(nu, nv, wrap_v=True)))
    for end, base in ((0, 0), (nu - 1, (nu - 1) * nv)):
        ctr = S[end].mean(0)
        ci = len(V)
        V = np.vstack([V, ctr])
        for j in range(nv):
            j1 = (j + 1) % nv
            F.append([ci, base + j1, base + j] if end == 0
                     else [ci, base + j, base + j1])
    return V, np.asarray(F, int)


def mirror_y(V):
    """左右镜像（绕 XZ 平面）。"""
    V = np.array(V, float, copy=True)
    V[:, 1] *= -1
    return V


def flip(F):
    return np.asarray(F, int)[:, [0, 2, 1]]
