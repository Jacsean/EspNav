# -*- coding: utf-8 -*-
"""gen_font.py — 生成 ESP32 点阵字库（ASCII 8x16 + 常用汉字 16x16）
依赖：Python + Pillow（已装 10.2），Windows 系统字体。
输出：main/font/font_data.c（Unicode 索引子集字库；缺字显示占位方块）
运行：python tools/gen_font.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

# ---------- 字集 ----------
HANZI = (
 "前方米直行左右转路口弯道岛第出驶多岔走侧支靠匝道十字继续掉头进入离开距"
 "离到达预计全程已完成耗时信号中断微短信张三来电未接方向北南东西环主辅并"
 "线段起点终剩余分钟小时公设置亮度动画速度弹窗消息关闭清除暂无数据连接成"
 "功失败重试等待初始化错误状态电量充电导航开始结束取消确认返回菜单首页图"
 "路线规划偏航重算拥堵缓行事故施工限速拍照测速服务区收费站桥梁隧道高速国"
 "道省县乡上下一二三四五六七八九十百千万零地点老记得准时过来个取件码稍后"
 "发材料明天早晚上"
)
SYMBOLS = "…｜：°"

def render_glyph(ch, w, h, fontpath, size, yoff=0):
    """渲染单字到位图并居中裁剪到 w×h，返回按行(MSB 左)的字节列表"""
    S = 48
    img = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(fontpath, size)
    except Exception:
        font = ImageFont.load_default()
    d.text((S // 2, S // 2 + yoff), ch, font=font, fill=255, anchor='mm')
    bbox = img.getbbox()
    if bbox:
        img = img.crop(bbox)
    # 居中贴入 w×h
    out = Image.new('L', (w, h), 0)
    ox = max(0, (w - img.width) // 2)
    oy = max(0, (h - img.height) // 2)
    out.paste(img.crop((0, 0, min(img.width, w), min(img.height, h))), (ox, oy))
    px = out.load()
    rows = []
    for y in range(h):
        bits = 0
        for x in range(w):
            if px[x, y] >= 96:
                bits |= (1 << (w - 1 - x))
        rows.append(bits)
    return rows

def pack(rows, w):
    nb = (w + 7) // 8
    data = bytearray()
    for y in range(len(rows)):
        v = rows[y]
        for b in range(nb):
            data.append((v >> (8 * (nb - 1 - b))) & 0xFF)
    return data

FONT_ZH = r'C:\Windows\Fonts\simhei.ttf'      # 黑体（16px 清晰）
FONT_ASCII = r'C:\Windows\Fonts\consola.ttf'  # Consolas（半角）

glyphs = []   # (cp, w, data)
# ASCII 8x16
for cp in range(0x20, 0x7F):
    rows = render_glyph(chr(cp), 8, 16, FONT_ASCII, 13)
    glyphs.append((cp, 8, pack(rows, 8)))
# 符号 + 汉字 16x16
seen = set()
for ch in SYMBOLS + HANZI:
    cp = ord(ch)
    if cp in seen:
        continue
    seen.add(cp)
    rows = render_glyph(ch, 16, 16, FONT_ZH, 16)
    glyphs.append((cp, 16, pack(rows, 16)))

glyphs.sort(key=lambda g: g[0])

bits = bytearray()
table = []
for cp, w, data in glyphs:
    table.append((cp, w, len(bits)))
    bits.extend(data)

# ---------- 抽样预览（终端） ----------
def preview(chlist):
    by = {cp: (w, off) for cp, w, off in table}
    for ch in chlist:
        cp = ord(ch)
        if cp not in by:
            print('missing:', ch); continue
        w, off = by[cp]
        nb = (w + 7) // 8
        print('--- U+%04X (%dpx) ---' % (cp, w))
        for y in range(16):
            row = ''
            for b in range(nb):
                row = row + bin(bits[off + y * nb + b])[2:].zfill(8)
            print(row[:w].replace('0', '.').replace('1', '#'))

preview(['前', '方', '米', '转', '4', 'm'])

# ---------- 输出 C ----------
out = []
out.append('/* 自动生成：tools/gen_font.py（请勿手改）')
out.append(' * 点阵字库子集：ASCII 8x16 + %d 个汉字/符号 16x16（Unicode 索引，二分查找）' % (len(glyphs) - 95))
out.append(' */')
out.append('#include "font.h"')
out.append('')
out.append('const font_glyph_t font_glyphs[] = {')
for cp, w, off in table:
    out.append('    { 0x%04X, %d, %d },' % (cp, w, off))
out.append('};')
out.append('const int font_glyph_count = %d;' % len(table))
out.append('')
out.append('const uint8_t font_bits[] = {')
line = '    '
for i, b in enumerate(bits):
    line += '0x%02X,' % b
    if (i + 1) % 16 == 0:
        out.append(line)
        line = '    '
if line.strip():
    out.append(line)
out.append('};')
out.append('')

open(os.path.join('main', 'font', 'font_data.c'), 'w', encoding='utf-8', newline='').write('\n'.join(out))
print('glyphs=%d bits=%d bytes -> main/font/font_data.c' % (len(table), len(bits)))
