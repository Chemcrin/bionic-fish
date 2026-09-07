# -*- coding: utf-8 -*-
"""追鲨一号（ZHUI SHA No.1）外观复刻 — 参数化建模。

依据 sample.zip / 外壳样式 / 建模复刻说明.md：
  * 整机长 ~700 mm、高 ~300 mm（含把手），长:高 ~2.3:1
  * 扁平鱼体 = 上壳 + 下腹壳 + 中央设备舱 + 左右外扩翼面
  * 前脸：盾牌轮廓 + 中央竖脊 + 左右三角格栅（真实开口，带肋条）+ 并列双灯
  * 腹部：浅凹底板 + 纵向导流槽 + 真实进水开口 + 两条纵向滑橇
  * 尾部：圆钝收尾 + 左右推进器护罩 + 中央轴向推进组件（后下方）
  * 顶部：设备舱盖 + 拱形把手 + 相机安装座 + Logo 铭牌 + 螺钉列
所有开口均为真实通孔并带 3.2 mm 壁厚翻边 —— 说明书明确要求不得封死进水面积。
"""
import numpy as np
import geom as G
from geom import (Scene, quads_to_tris, grid_quads, hole_rims, superellipse_ring,
                  pchip, surface_normals, sample_grid, sweep, circle_profile,
                  rect_profile, box, cylinder, annulus_tube, loft_closed,
                  mirror_y, flip)

# ============================================================ 全局参数 =====

L_TARGET = 700.0      # 说明书给定：整机总长 mm
H_TARGET = 300.0      # 说明书给定：整机总高 mm（草图含把手）
THICK = 3.2           # 壳体壁厚（说明书建议 2.5–4 mm，取中值）

NX = 232              # 纵向站数
K = 34                # 每象限截面点数
NS = 4 * K            # 截面总点数 = 136

MAT = dict(shell='shell', panel='panel', grip='grip',
           metal='metal', red='red', light='light')

# ---- 纵向型线控制表（原始单位，最后整体缩放到 700 mm）-------------------
#      鼻尖 x=0 ... 尾尖 x=712
XS   = [0,    14,  35,  70,  105, 154, 210, 280, 350, 420, 490, 560, 616, 658, 700, 708, 712]
WID  = [1.5,  18,  40,  66,  86,  108, 126, 134, 131, 123, 112, 98,  82,  66,  44,  28,  2.0]
ZTOP = [118,  134, 155, 182, 203, 224, 238, 240, 236, 229, 218, 202, 184, 166, 146, 139, 134]
ZWL  = [114,  112, 112, 114, 117, 120, 123, 125, 125, 125, 125, 126, 128, 131, 134, 135, 133]
ZBOT = [110,  92,  76,  60,  50,  41,  35,  34,  34,  36,  40,  50,  66,  90,  120, 128, 131]
NUP  = [2.1,  2.1, 2.15, 2.2, 2.25, 2.3, 2.35, 2.4, 2.4, 2.38, 2.35, 2.3, 2.25, 2.2, 2.15, 2.1, 2.1]
NLO  = [2.5,  2.6, 2.8, 3.1, 3.3, 3.5, 3.6, 3.7, 3.7, 3.6, 3.5, 3.3, 3.1, 2.9, 2.7, 2.6, 2.6]

L_RAW = XS[-1]
f_w, f_top, f_wl, f_bot = (pchip(XS, WID), pchip(XS, ZTOP),
                           pchip(XS, ZWL), pchip(XS, ZBOT))
f_nup, f_nlo = pchip(XS, NUP), pchip(XS, NLO)

X = np.linspace(0.0, L_RAW, NX)


def xi(x):
    """世界 x -> 纵向浮点站号"""
    return np.asarray(x, float) / L_RAW * (NX - 1)


def jf(f):
    """象限分数 f（0=顶中线, 1=右舷, 2=底中线, 3=左舷）-> 截面浮点点号"""
    return np.asarray(f, float) * K


def jcyc(a, b):
    """截面点号的环形距离"""
    d = np.abs(a - b) % NS
    return np.minimum(d, NS - d)


# ============================================================ 壳体主体 =====

def hull_grid():
    """生成 (NX, NS, 3) 主壳点阵，已刻好分割缝。"""
    P = np.zeros((NX, NS, 3))
    for i, x in enumerate(X):
        y, z = superellipse_ring(float(f_w(x)), float(f_top(x)), float(f_wl(x)),
                                 float(f_bot(x)), float(f_nup(x)), float(f_nlo(x)), K)
        P[i, :, 0] = x
        P[i, :, 1] = y
        P[i, :, 2] = z

    # ---- 分割缝（说明书：上壳表面有纵向分割缝、侧向中线附近有装配缝）----
    J = np.arange(NS)[None, :]
    XX = X[:, None]

    def band(fc, halfw, x0, x1, depth):
        jc = jf(fc)
        d = np.minimum(jcyc(J, jc), jcyc(J, NS - jc))       # 左右对称
        prof = np.exp(-(d / halfw) ** 2)
        win = (np.clip((XX - x0) / 22.0, 0, 1) *
               np.clip((x1 - XX) / 22.0, 0, 1))
        return depth * prof * win

    hw = K / 26.0                            # 缝宽随截面分辨率等比缩放
    g = np.zeros((NX, NS))
    g += band(0.24, 1.3 * hw, 118, 604, 1.5)     # 上壳纵向缝
    g += band(0.62, 1.4 * hw, 150, 648, 2.0)     # 侧向中线装配缝
    g += band(1.55, 1.2 * hw, 250, 520, 1.2)     # 腹侧导流棱线
    # 尾部台阶横缝（说明书：上壳与侧板在尾部形成明显台阶和装配缝）
    g += 2.2 * np.exp(-((XX - 596) / 5.0) ** 2) * \
        np.exp(-(np.minimum(jcyc(J, jf(0.95)), jcyc(J, NS - jf(0.95))) / (14.0 * hw)) ** 2)

    N = surface_normals(P)
    return P - N[:, :, :] * g[:, :, None]


def hull_holes(P):
    """构造 keep 掩码：前脸三角格栅 / 腹部进水槽 / 尾部推进器开口。"""
    xc = 0.5 * (X[:-1] + X[1:])[:, None]                  # 四边形中心 x
    jc = (np.arange(NS) + 0.5)[None, :]                   # 四边形中心 j
    keep = np.ones((NX - 1, NS), bool)

    # ---- 1. 前脸左右三角格栅（x 26–136），向后张开成三角 ----------------
    s = np.clip((xc - 26.0) / 110.0, 0, 1)
    inside_x = (xc >= 26) & (xc <= 136)
    jlo = jf(0.60 - 0.44 * s)
    jhi = jf(0.68 + 0.30 * s)
    for sgn in (+1, -1):
        c = jc if sgn > 0 else (NS - jc)
        keep &= ~(inside_x & (c >= jlo) & (c <= jhi))

    # ---- 2. 腹部进水槽：4 条纵向长孔（说明书 476–482 s）-----------------
    for (x0, x1, foff, hw) in ((300, 468, 0.14, 0.058),
                               (330, 440, 0.40, 0.050)):
        t = np.clip((xc - x0) / (x1 - x0), 0, 1)
        taper = np.sin(np.pi * t) ** 0.45                  # 两端收尖
        inx = (xc > x0) & (xc < x1)
        for sgn in (+1, -1):
            ctr = jf(2.0 + sgn * foff)
            keep &= ~(inx & (jcyc(jc, ctr) <= jf(hw) * taper))

    # ---- 3. 尾部下方推进器开口 ----------------------------------------
    inx = (xc > 630) & (xc < 706)
    keep &= ~(inx & (jcyc(jc, jf(2.0)) <= jf(0.44)))
    return keep


def build_hull(sc, P):
    N = surface_normals(P)
    Pin = P - N * THICK
    keep = hull_holes(P)

    Q = grid_quads(NX, NS, wrap_v=True, keep=keep)
    sc.add_quads(P.reshape(-1, 3), Q, 'hull_shell', MAT['shell'], smooth=True)

    Vr, Qr = hole_rims(P, Pin, keep, wrap_v=True)
    sc.add_quads(Vr, Qr, 'hull_openings_rim', MAT['panel'])
    return N, keep


# ============================================================ 前脸细节 =====

def build_face(sc, P, N):
    """中央竖脊 + 格栅肋条（说明书：中央有竖向脊线，格栅内有横向/斜向肋条）"""
    # ---- 中央竖脊：沿顶中线的一条隆起棱 -------------------------------
    s = np.linspace(0.02, 1.0, 34)
    xr = 6 + 132 * s
    ctr = sample_grid(P, xi(xr), np.zeros_like(xr))
    nr = sample_grid(N, xi(xr), np.zeros_like(xr))
    nr /= np.linalg.norm(nr, axis=1, keepdims=True)
    path = ctr + nr * 1.0
    prof = rect_profile(9.0, 7.0, expo=2.6, n=20)
    scale = np.clip(np.sin(np.pi * np.clip((s - 0.02) / 0.98, 0, 1)) ** 0.35, 0.15, 1) * 1.0
    Vv, Ff = sweep(path, prof, up=(0, 1, 0), cap=True, scale=scale)
    sc.add(Vv, Ff, 'face_center_ridge', MAT['shell'], smooth=True)

    # ---- 格栅肋条：每侧 3 根，平行于开口内缘 --------------------------
    for sgn in (+1, -1):
        for r in (1, 2, 3):
            f = r / 4.0
            # 起点收在 0.30：开口前段太窄，肋条会穿出壳面
            ss = np.linspace(0.30, 0.955, 30)
            xr = 26 + 110 * ss
            jlo = 0.60 - 0.44 * ss
            jhi = 0.68 + 0.30 * ss
            jj = jf(jlo + f * (jhi - jlo))
            jj = jj if sgn > 0 else (NS - jj)
            pos = sample_grid(P, xi(xr), jj)
            nn = sample_grid(N, xi(xr), jj)
            nn /= np.linalg.norm(nn, axis=1, keepdims=True)
            Vv, Ff = sweep(pos - nn * 7.5, rect_profile(2.6, 3.6, expo=3.0, n=14),
                           up=(0, 0, 1), cap=True)
            sc.add(Vv, Ff, 'face_grille_rib_%s%d' % ('R' if sgn > 0 else 'L', r),
                   MAT['panel'], smooth=True)


def build_lights(sc):
    """前下方并列双灯 —— 视觉上的"眼睛"（说明书：两枚圆形白色灯）"""
    # 灯轴压在机腹前缘之下，确保不被壳体埋住（说明书：装在底部前缘的短支架上）
    for sgn in (+1, -1):
        y = 40.0 * sgn
        z = 34.0
        # 镜片朝前（-X），筒体在后 —— 顺序搞反的话正面只能看到一块黑饼
        Vv, Ff = cylinder((38, y, z), (84, y, z), 22.0, n=40)
        sc.add(Vv, Ff, 'light_housing', MAT['shell'], smooth=True)
        Vv, Ff = annulus_tube((31, y, z), (41, y, z), 22.5, 16.5, n=40)
        sc.add(Vv, Ff, 'light_bezel', MAT['metal'], smooth=True)
        Vv, Ff = cylinder((33.5, y, z), (39, y, z), 16.5, n=40)
        sc.add(Vv, Ff, 'light_lens', MAT['light'], smooth=True)
        # 短支架：灯座 -> 机腹 / 滑橇前端
        Vv, Ff = box((50, y - 6, 32), (80, y + 6, 74))
        sc.add(Vv, Ff, 'light_bracket', MAT['panel'])


# ============================================================ 侧翼 =========

def build_wings(sc):
    """左右三角翼面：前缘较直、后缘向尾部收拢、翼面略向下倾。"""
    NU, NC = 30, 44
    c = np.linspace(0.0, 1.0, NC // 2)
    for sgn in (+1, -1):
        secs = []
        for u in np.linspace(0.0, 1.0, NU):
            xle = 175 + 155 * u            # 前缘较直，向后掠
            xte = 458 + 28 * u             # 后缘向尾部收拢
            y = (64 + 150 * u) * sgn
            T = (22 - 15 * u) * (1 - u ** 8) ** 0.35
            xs = xle + (xte - xle) * c
            zc = f_wl(np.clip(xs, 0, L_RAW)) - 18.0 * u ** 1.4      # 翼面略向下倾
            h = 0.5 * T * (1 - (2 * c - 1) ** 2) ** 0.55
            up = np.stack([xs, np.full_like(xs, y), zc + h], -1)
            dn = np.stack([xs, np.full_like(xs, y), zc - h], -1)
            secs.append(np.vstack([up, dn[::-1]]))
        Vv, Ff = loft_closed(np.array(secs))
        if sgn < 0:
            Ff = flip(Ff)
        sc.add(Vv, Ff, 'wing_%s' % ('R' if sgn > 0 else 'L'), MAT['shell'], smooth=True)


# ============================================================ 推进系统 =====

def _shroud(sc, axis_y, axis_z, x0, x1, r_out, r_in, nbar, tag):
    """圆筒护罩 + 外缘纵向格栅 + 红色桨毂 + 支撑辐条"""
    Vv, Ff = annulus_tube((x0, axis_y, axis_z), (x1, axis_y, axis_z), r_out, r_in, n=48)
    sc.add(Vv, Ff, tag + '_shroud', MAT['shell'], smooth=True)

    # 外缘纵向格栅（说明书：护罩外缘有多根纵向格栅）
    xb0 = x0 + 0.42 * (x1 - x0)
    for a in np.linspace(0, 2 * np.pi, nbar, endpoint=False):
        yy = axis_y + np.cos(a) * (r_out + 1.6)
        zz = axis_z + np.sin(a) * (r_out + 1.6)
        Vv, Ff = sweep(np.array([[xb0, yy, zz], [(xb0 + x1) / 2, yy, zz], [x1 + 2, yy, zz]]),
                       rect_profile(4.5, 3.0, expo=3.0, n=12), up=(0, 0, 1), cap=True)
        sc.add(Vv, Ff, tag + '_grille_bar', MAT['panel'], smooth=True)

    # 红色桨毂 + 桨叶（说明书：内部能看到红色部件/桨毂）
    hx0, hx1 = x0 + 0.30 * (x1 - x0), x0 + 0.62 * (x1 - x0)
    rh = r_in * 0.34
    Vv, Ff = cylinder((hx0, axis_y, axis_z), (hx1, axis_y, axis_z), rh, n=28)
    sc.add(Vv, Ff, tag + '_hub', MAT['red'], smooth=True)
    hm = 0.5 * (hx0 + hx1)
    for a in np.linspace(0, 2 * np.pi, 4, endpoint=False):
        ca, sa = np.cos(a), np.sin(a)
        p0 = np.array([hm - 7, axis_y + ca * rh * 0.8, axis_z + sa * rh * 0.8])
        p1 = np.array([hm + 7, axis_y + ca * r_in * 0.96, axis_z + sa * r_in * 0.96])
        Vv, Ff = sweep(np.array([p0, (p0 + p1) / 2, p1]),
                       rect_profile(2.6, 11.0, expo=2.4, n=12), up=(1, 0, 0), cap=True)
        sc.add(Vv, Ff, tag + '_blade', MAT['red'], smooth=True)
    # 支撑辐条
    for a in np.linspace(0.4, 2 * np.pi + 0.4, 3, endpoint=False):
        ca, sa = np.cos(a), np.sin(a)
        p0 = np.array([hx1 + 4, axis_y + ca * rh, axis_z + sa * rh])
        p1 = np.array([hx1 + 4, axis_y + ca * r_in, axis_z + sa * r_in])
        Vv, Ff = sweep(np.array([p0, (p0 + p1) / 2, p1]),
                       rect_profile(3.0, 3.0, expo=3.0, n=10), up=(1, 0, 0), cap=True)
        sc.add(Vv, Ff, tag + '_strut', MAT['metal'], smooth=True)


def build_thrusters(sc):
    # ---- 尾部左右推进器筒体/护罩 --------------------------------------
    for sgn in (+1, -1):
        _shroud(sc, 92.0 * sgn, 122.0, 588, 700, 50.0, 41.0, 10,
                'tail_%s' % ('R' if sgn > 0 else 'L'))

    # ---- 中央轴向推进组件（说明书：轴线与机身纵轴重合，位于后下方）----
    AZ = 88.0
    Vv, Ff = cylinder((516, 0, AZ), (606, 0, AZ), 36.0, n=40)      # 电机/电池壳
    sc.add(Vv, Ff, 'core_motor_can', MAT['panel'], smooth=True)
    for a in np.linspace(np.pi / 4, 2 * np.pi + np.pi / 4, 4, endpoint=False):
        yy, zz = np.cos(a) * 33.0, AZ + np.sin(a) * 33.0           # 开放式支撑框架
        Vv, Ff = sweep(np.array([[604, yy, zz], [625, yy, zz], [648, yy, zz]]),
                       rect_profile(4.0, 4.0, expo=3.0, n=10), up=(0, 0, 1), cap=True)
        sc.add(Vv, Ff, 'core_frame_strut', MAT['metal'], smooth=True)
    _shroud(sc, 0.0, AZ, 646, 706, 47.0, 39.0, 12, 'core')


# ============================================================ 面板 / 附件 ==

def patch_grids(P, N, fi0, fi1, fj0, fj1, nu, nv, h, lift=0.0, feather=0.0):
    """在壳面上取一块贴合的面板。feather>0 时四周高度平滑收敛，读起来像模出来的
    隆起而不是贴上去的方块（舱盖需要清晰台阶，所以 feather=0）。"""
    fi = np.linspace(fi0, fi1, nu)
    fj = np.linspace(fj0, fj1, nv)
    FI, FJ = np.meshgrid(fi, fj, indexing='ij')
    S = sample_grid(P, FI.ravel(), FJ.ravel()).reshape(nu, nv, 3)
    Ns = sample_grid(N, FI.ravel(), FJ.ravel()).reshape(nu, nv, 3)
    Ns /= np.linalg.norm(Ns, axis=2, keepdims=True)
    hh = np.full((nu, nv), float(h))
    if feather > 0:
        u = np.linspace(0, 1, nu)
        v = np.linspace(0, 1, nv)
        wu = np.clip(np.minimum(u, 1 - u) / feather, 0, 1)
        wv = np.clip(np.minimum(v, 1 - v) / feather, 0, 1)
        w = wu[:, None] * wv[None, :]
        hh = h * (3 * w ** 2 - 2 * w ** 3)
    return S + Ns * lift, S + Ns * (lift + hh[:, :, None])


def slab(A, B):
    """A=贴壳内层, B=抬起外层 -> 闭合薄板实体。"""
    nu, nv, _ = A.shape
    n0 = nu * nv
    V = np.vstack([B.reshape(-1, 3), A.reshape(-1, 3)])
    Q = list(grid_quads(nu, nv))                               # 外表面
    Q += list(grid_quads(nu, nv)[:, ::-1] + n0)                # 内表面（翻面）
    im, jm = nu - 1, nv - 1

    def idx(arr_off, i, j):
        return arr_off + i * nv + j
    for i in range(nu - 1):                                     # j=0 / j=jm 侧壁
        Q.append([idx(n0, i, 0), idx(n0, i + 1, 0), idx(0, i + 1, 0), idx(0, i, 0)])
        Q.append([idx(0, i, jm), idx(0, i + 1, jm), idx(n0, i + 1, jm), idx(n0, i, jm)])
    for j in range(nv - 1):                                     # i=0 / i=im 侧壁
        Q.append([idx(0, 0, j), idx(0, 0, j + 1), idx(n0, 0, j + 1), idx(n0, 0, j)])
        Q.append([idx(n0, im, j), idx(n0, im, j + 1), idx(0, im, j + 1), idx(0, im, j)])
    return V, quads_to_tris(np.array(Q, int))


def prism(center, e1, e2, nrm, poly, h0, h1):
    """poly 在 (e1,e2) 平面内按 CCW（相对 nrm）给出。"""
    poly = np.asarray(poly, float)
    c0 = np.asarray(center, float) + np.asarray(nrm, float) * h0
    c1 = np.asarray(center, float) + np.asarray(nrm, float) * h1
    e1, e2 = np.asarray(e1, float), np.asarray(e2, float)
    Vb = c0 + poly[:, 0:1] * e1 + poly[:, 1:2] * e2
    Vt = c1 + poly[:, 0:1] * e1 + poly[:, 1:2] * e2
    n = len(poly)
    V = np.vstack([Vb, Vt, c0[None, :], c1[None, :]])
    F = []
    for j in range(n):
        j1 = (j + 1) % n
        F += [[j, j1, n + j1], [j, n + j1, n + j]]
        F += [[2 * n, j1, j], [2 * n + 1, n + j, n + j1]]
    return V, np.asarray(F, int)


def screw(sc, P, N, fi, fj, r=4.2, h=3.4, group='screw'):
    p = sample_grid(P, [fi], [fj])[0]
    n = sample_grid(N, [fi], [fj])[0]
    n /= np.linalg.norm(n)
    Vv, Ff = cylinder(p - n * 1.5, p + n * h, r, n=14)
    sc.add(Vv, Ff, group, MAT['metal'], smooth=True)


def build_topside(sc, P, N):
    # ---- 顶部设备/电池舱盖（说明书：顶部中央有略高的设备舱盖）----------
    A, B = patch_grids(P, N, xi(258), xi(470), jf(-0.30), jf(0.30), 26, 22, 5.0)
    Vv, Ff = slab(A, B)
    sc.add(Vv, Ff, 'top_hatch', MAT['panel'], smooth=False)
    for t in np.linspace(0.06, 0.94, 5):
        for s in (-0.255, 0.255):
            screw(sc, P, N, xi(258 + 212 * t), jf(s), 4.0, 7.0, 'top_hatch_screw')

    # ---- 前上方 Logo 铭牌（对应 464 s 画面里的八边形红标铭牌）---------
    ctr = sample_grid(P, [xi(172)], [0.0])[0]
    nn = sample_grid(N, [xi(172)], [0.0])[0]
    nn /= np.linalg.norm(nn)
    e2 = np.array([1.0, 0, 0]) - nn * np.dot(np.array([1.0, 0, 0]), nn)
    e2 /= np.linalg.norm(e2)
    e1 = np.cross(e2, nn)
    a = np.linspace(0, 2 * np.pi, 8, endpoint=False) + np.pi / 8
    oct8 = np.stack([np.cos(a) * 34, np.sin(a) * 50], -1)
    Vv, Ff = prism(ctr, e1, e2, nn, oct8, 0.0, 5.0)
    sc.add(Vv, Ff, 'logo_plate', MAT['panel'])
    Vv, Ff = prism(ctr, e1, e2, nn, oct8 * 0.62, 5.0, 6.6)
    sc.add(Vv, Ff, 'logo_badge', MAT['red'])

    # ---- 顶部拱形把手（说明书：可抓握的拱形/枪柄式把手，覆防滑材料）----
    hp = np.array([[330, 0, 242], [352, 0, 266], [382, 0, 288],
                   [414, 0, 298], [446, 0, 288], [472, 0, 262], [490, 0, 224]], float)
    t = np.linspace(0, 1, len(hp))
    tt = np.linspace(0, 1, 40)
    path = np.stack([pchip(t, hp[:, k])(tt) for k in range(3)], -1)
    Vv, Ff = sweep(path, rect_profile(15.0, 17.0, expo=3.4, n=22), up=(0, 1, 0), cap=True)
    sc.add(Vv, Ff, 'grab_handle', MAT['grip'], smooth=True)

    # ---- 左右手部接触区（实测画面显示双手从机身两侧握持）--------------
    for s in (+1, -1):
        A, B = patch_grids(P, N, xi(372), xi(482), jf(s * 0.58), jf(s * 0.92),
                           18, 12, 7.0, feather=0.34)
        Vv, Ff = slab(A, B)
        if s < 0:
            Ff = flip(Ff)
        sc.add(Vv, Ff, 'side_grip_pad', MAT['grip'], smooth=True)

    # ---- 尾部侧板台阶（说明书：上壳与侧板在尾部形成明显台阶）----------
    for s in (+1, -1):
        A, B = patch_grids(P, N, xi(598), xi(692), jf(s * 0.50), jf(s * 1.08),
                           16, 14, 4.5, feather=0.16)
        Vv, Ff = slab(A, B)
        if s < 0:
            Ff = flip(Ff)
        sc.add(Vv, Ff, 'tail_side_panel', MAT['panel'])

    # ---- 顶部相机安装座（说明书：平板底座 + 短立柱 + 前后调节铰链）----
    A, B = patch_grids(P, N, xi(198), xi(268), jf(-0.20), jf(0.20), 14, 12, 8.0)
    Vv, Ff = slab(A, B)
    sc.add(Vv, Ff, 'cam_base_plate', MAT['panel'])
    ztop = float(f_top(233))
    Vv, Ff = box((224, -13, ztop + 5), (244, 13, ztop + 34))         # 短立柱
    sc.add(Vv, Ff, 'cam_post', MAT['metal'])
    for s in (+1, -1):                                               # 前后调节铰链
        Vv, Ff = cylinder((234, 13 * s, ztop + 34), (234, 20 * s, ztop + 34), 13.0, n=22)
        sc.add(Vv, Ff, 'cam_hinge', MAT['metal'], smooth=True)
        Vv, Ff = cylinder((234, 19 * s, ztop + 34), (234, 22 * s, ztop + 34), 5.5, n=14)
        sc.add(Vv, Ff, 'cam_hinge_knob', MAT['metal'], smooth=True)
    ang = np.deg2rad(-9.0)                                           # 托板，镜头朝前
    e2 = np.array([np.cos(ang), 0, np.sin(ang)])
    e1 = np.array([0.0, 1.0, 0.0])
    nn = np.cross(e1, e2)
    ctr_t = np.array([230, 0, ztop + 41.0])
    Vv, Ff = prism(ctr_t, e1, e2, nn,
                   np.array([[-24, -27], [24, -27], [24, 27], [-24, 27]]), 0.0, 6.0)
    sc.add(Vv, Ff, 'cam_tray', MAT['panel'])
    Vv, Ff = prism(ctr_t + nn * 6.0, e1, e2, nn,                     # 定位卡槽
                   np.array([[-24, -27], [-17, -27], [-17, 27], [-24, 27]]), 0.0, 7.0)
    sc.add(Vv, Ff, 'cam_tray_lip', MAT['metal'])
    Vv, Ff = prism(ctr_t + nn * 6.0, e1, e2, nn,
                   np.array([[17, -27], [24, -27], [24, 27], [17, 27]]), 0.0, 7.0)
    sc.add(Vv, Ff, 'cam_tray_lip', MAT['metal'])

    # ---- 线缆出线口（说明书：电池/控制器盒体带线缆出线孔）--------------
    for s in (+1, -1):
        p = sample_grid(P, [xi(500)], [jf(s * 0.34)])[0]
        n = sample_grid(N, [xi(500)], [jf(s * 0.34)])[0]
        n /= np.linalg.norm(n)
        Vv, Ff = cylinder(p - n * 2, p + n * 9, 9.0, n=18)
        sc.add(Vv, Ff, 'cable_gland', MAT['metal'], smooth=True)
        Vv, Ff = cylinder(p + n * 9, p + n * 13, 6.0, n=16)
        sc.add(Vv, Ff, 'cable_gland_cap', MAT['grip'], smooth=True)

    # ---- 外露螺钉列（说明书：外壳连接处多颗沉头螺钉，侧向中线一列）----
    for s in (+1, -1):
        for x in np.linspace(482, 668, 10):
            screw(sc, P, N, xi(x), jf(s * 0.38), 4.2, 3.2, 'seam_screw_row')
        for x in (300, 366, 432):
            screw(sc, P, N, xi(x), jf(s * 0.70), 7.0, 5.0, 'side_fastener')


def build_bottom(sc, P, N):
    """底部纵向滑橇/加强梁：兼作离地支撑与导流边，前端与灯支架相连。"""
    for s in (+1, -1):
        xs = np.linspace(70, 574, 48)
        zb = f_bot(xs)
        secs = []
        for x, zt in zip(xs, zb):
            fade = np.clip((x - 70) / 46.0, 0, 1) * np.clip((574 - x) / 76.0, 0, 1)
            # 前端向内收，与灯支架汇合
            y = (42.0 + 34.0 * np.clip((x - 76) / 130.0, 0, 1)) * s
            top = zt + 6
            bot = 8 + (top - 8) * (1 - fade)
            hw = 9.0
            secs.append(np.array([[x, y - hw, top], [x, y + hw, top],
                                  [x, y + hw * 0.7, bot], [x, y - hw * 0.7, bot]]))
        # 截面绕序与 s 无关（未做镜像），因此两侧都不需要翻面
        Vv, Ff = loft_closed(np.array(secs))
        sc.add(Vv, Ff, 'bottom_skid', MAT['panel'])


# ============================================================ 组装 =========

def build():
    sc = Scene()
    P = hull_grid()
    N, keep = build_hull(sc, P)
    build_face(sc, P, N)
    build_lights(sc)
    build_wings(sc)
    build_thrusters(sc)
    build_topside(sc, P, N)
    build_bottom(sc, P, N)

    # 归零 + 等比缩放到说明书给定的 700 mm 总长
    lo, hi = sc.bbox()
    sc.apply(lambda V: V - np.array([lo[0], 0.0, lo[2]]))
    s = L_TARGET / (hi[0] - lo[0])
    sc.apply(lambda V: V * s)
    return sc, s


MTL = """# 追鲨一号 外观材质
newmtl shell
Kd 0.086 0.094 0.110
Ks 0.055 0.055 0.060
Ns 22
newmtl panel
Kd 0.149 0.165 0.188
Ks 0.180 0.180 0.190
Ns 60
newmtl grip
Kd 0.204 0.220 0.243
Ks 0.030 0.030 0.030
Ns 8
newmtl metal
Kd 0.463 0.486 0.533
Ks 0.450 0.450 0.460
Ns 120
newmtl red
Kd 0.702 0.122 0.165
Ks 0.220 0.180 0.180
Ns 70
newmtl light
Kd 0.933 0.949 0.973
Ks 0.700 0.700 0.700
Ns 160
"""

RGB = dict(shell=(0.086, 0.094, 0.110), panel=(0.149, 0.165, 0.188),
           grip=(0.204, 0.220, 0.243), metal=(0.463, 0.486, 0.533),
           red=(0.702, 0.122, 0.165), light=(0.933, 0.949, 0.973))
