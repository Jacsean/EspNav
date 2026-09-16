#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""gen_icon.py — 生成 EspNav 控制器 App 图标（导航主题）

设计：深绿渐变底 + 亮绿路线折线 + 白色导航箭头 + 起点定位点
输出：
  - 传统图标 mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png（含背景）
  - 圆形图标 ic_launcher_round.png（同内容，由系统裁剪）
  - 自适应图标前景 mipmap-*/ic_launcher_foreground.png（透明底，缩放到安全区）
用法：python tools/gen_icon.py
"""
import os
from PIL import Image, ImageDraw

RES = 'app/src/main/res'
BASE = 512                      # 设计基准尺寸
BG_TOP = (11, 61, 15)           # #0B3D0F
BG_BOT = (30, 122, 46)          # #1E7A2E
ROUTE = (0, 230, 118)           # #00E676 亮绿路线
WHITE = (255, 255, 255)

# 传统图标各密度尺寸
DENSITIES = {
    'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192,
}
# 自适应图标前景（108dp）
FG_DENSITIES = {
    'mdpi': 108, 'hdpi': 162, 'xhdpi': 216, 'xxhdpi': 324, 'xxxhdpi': 432,
}


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def rounded_mask(size, radius):
    m = Image.new('L', (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def draw_route_and_arrow(d, s, scale, offset):
    """在画布上画路线折线与导航箭头；scale/offset 控制缩放与居中（用于前景图安全区）"""
    def P(x, y):
        return (offset + x * scale, offset + y * scale)

    pts = [P(96, 416), P(176, 336), P(150, 262), P(232, 196), P(330, 150)]
    w = max(3, int(30 * scale))
    d.line(pts, fill=ROUTE, width=w, joint='curve')
    # 拐点补圆，保证折角圆润（兼容不支持 joint 的 PIL）
    for p in pts[1:-1]:
        r = w // 2
        d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=ROUTE)

    # 起点定位点（白环 + 亮绿心）
    cx, cy = pts[0]
    r1 = max(4, int(26 * scale))
    r2 = max(2, int(13 * scale))
    d.ellipse([cx - r1, cy - r1, cx + r1, cy + r1], fill=WHITE)
    d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=ROUTE)

    # 导航箭头（指向前上方的白色三角）
    ex, ey = pts[-1]
    L = int(84 * scale)
    half = int(46 * scale)
    tip = (ex + int(0.55 * L), ey - int(0.75 * L))
    left = (ex - int(0.90 * L), ey - int(0.10 * L))
    right = (ex + int(0.20 * L), ey + int(0.85 * L))
    d.polygon([tip, left, right], fill=WHITE)
    inner = (ex - int(0.30 * L), ey + int(0.20 * L))
    d.polygon([tip, left, inner, right], fill=WHITE)
    _ = half


def make_icon(size, with_bg=True, fg_scale=0.62):
    img = Image.new('RGBA', (BASE, BASE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    if with_bg:
        # 深绿纵向渐变
        for y in range(BASE):
            d.line([(0, y), (BASE, y)], fill=lerp(BG_TOP, BG_BOT, y / BASE))
        # 叠加高光（左上柔和亮斑）
        hl = Image.new('RGBA', (BASE, BASE), (0, 0, 0, 0))
        hd = ImageDraw.Draw(hl)
        hd.ellipse([-140, -180, 320, 260], fill=(255, 255, 255, 26))
        img.alpha_composite(hl)
        d = ImageDraw.Draw(img)
        # 圆角遮罩（传统图标）
        mask = rounded_mask(BASE, 96)
        out = Image.new('RGBA', (BASE, BASE), (0, 0, 0, 0))
        out.paste(img, (0, 0), mask)
        img = out
        d = ImageDraw.Draw(img)
        # 主体图形铺满（传统图标不需要安全区）
        draw_route_and_arrow(d, BASE, 1.0, 0)
    else:
        # 自适应图标前景：图形缩放到中心安全区
        s = fg_scale
        off = BASE * (1 - s) / 2.0
        draw_route_and_arrow(d, BASE, s, off)

    return img.resize((size, size), Image.LANCZOS)


def ensure(path):
    os.makedirs(path, exist_ok=True)


def main():
    # 传统图标 + 圆形图标
    for name, px in DENSITIES.items():
        dirp = os.path.join(RES, 'mipmap-' + name)
        ensure(dirp)
        img = make_icon(px, with_bg=True)
        img.save(os.path.join(dirp, 'ic_launcher.png'))
        img.save(os.path.join(dirp, 'ic_launcher_round.png'))
        print('  ic_launcher %s %dx%d' % (name, px, px))

    # 自适应图标前景
    for name, px in FG_DENSITIES.items():
        dirp = os.path.join(RES, 'mipmap-' + name)
        ensure(dirp)
        make_icon(px, with_bg=False).save(os.path.join(dirp, 'ic_launcher_foreground.png'))
        print('  foreground %s %dx%d' % (name, px, px))

    # 自适应图标描述（API 26+）
    anydpi = os.path.join(RES, 'mipmap-anydpi-v26')
    ensure(anydpi)
    xml = '''<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
'''
    for f in ('ic_launcher.xml', 'ic_launcher_round.xml'):
        with open(os.path.join(anydpi, f), 'w', encoding='utf-8', newline='') as fp:
            fp.write(xml)
    print('  adaptive icon xml: mipmap-anydpi-v26/ic_launcher.xml')
    print('全部图标已生成')


if __name__ == '__main__':
    main()
