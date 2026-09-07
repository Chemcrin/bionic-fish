# -*- coding: utf-8 -*-
"""构建追鲨一号外观模型，导出 OBJ/MTL/STL 并渲染预览图。

    python build.py            # 全量：导出 + 六视图预览
    python build.py --no-render
"""
import os
import sys
import time
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import geom as G           # noqa: E402
import model as M          # noqa: E402

OUT = os.path.abspath(os.path.join(HERE, '..', 'out'))


def main(do_render=True):
    os.makedirs(OUT, exist_ok=True)
    t0 = time.time()
    sc, scale = M.build()
    nv, nf = sc.stats()
    lo, hi = sc.bbox()
    dim = hi - lo

    print('[build] %.2fs  顶点 %d  三角面 %d  (缩放系数 %.5f)'
          % (time.time() - t0, nv, nf, scale))
    print('[bbox ] 长 X = %.1f mm   宽 Y = %.1f mm   高 Z = %.1f mm'
          % (dim[0], dim[1], dim[2]))
    print('[ratio] 长:高 = %.2f : 1     长:宽 = %.2f : 1'
          % (dim[0] / dim[2], dim[0] / dim[1]))

    # 主壳封闭性自检：有符号体积为正 => 面片朝外
    hull = sc.blocks[0]
    print('[check] 主壳有符号体积 = %.3f L (>0 表示法线朝外)'
          % (G.signed_volume(hull['V'], hull['F']) / 1e6))

    obj = os.path.join(OUT, 'zhuisha_no1.obj')
    sc.write_obj(obj, mtllib='zhuisha_no1.mtl')
    with open(os.path.join(OUT, 'zhuisha_no1.mtl'), 'w', encoding='utf-8') as fh:
        fh.write(M.MTL)
    sc.write_stl(os.path.join(OUT, 'zhuisha_no1.stl'))
    print('[write] %s  (%.1f MB)' % (obj, os.path.getsize(obj) / 1e6))

    groups = {}
    for b in sc.blocks:
        groups.setdefault(b['group'], 0)
        groups[b['group']] += len(b['F'])
    print('[parts] %d 个命名部件组' % len(groups))

    if not do_render:
        return
    import render as R
    V, F, mi, names = sc.merged()
    rgb = np.array([M.RGB[n] for n in names])[mi]
    imgs = []
    for view in ('iso', 'side', 'top', 'front', 'low', 'iso2'):
        t = time.time()
        im = R.shot(V, F, rgb, view, size=(1180, 830), persp=(view in ('iso', 'iso2')))
        im.save(os.path.join(OUT, 'view_%s.png' % view))
        imgs.append(im)
        print('[render] %-6s %.1fs' % (view, time.time() - t))
    R.contact_sheet(imgs, cols=2).save(os.path.join(OUT, 'preview_sheet.png'))
    print('[render] preview_sheet.png')


if __name__ == '__main__':
    main('--no-render' not in sys.argv)
