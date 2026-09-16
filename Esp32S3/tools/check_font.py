#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""check_font.py — 字库自检：码点升序/唯一、字形偏移不越界、字数统计、缺字采样
用法：python tools/check_font.py [font_data.c]
"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'main/font/font_data.c'
src = open(path, encoding='utf-8').read()

rows = re.findall(r'\{\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', src)
print('字形数:', len(rows))
cps = [int(r[0], 16) for r in rows]
offs = [int(r[2]) for r in rows]
ws = [int(r[1]) for r in rows]

bad = [i for i in range(1, len(cps)) if cps[i] <= cps[i - 1]]
print('升序/唯一性问题:', len(bad), ('first@%d' % bad[0]) if bad else 'OK')

m = re.search(r'font_bits\[\]\s*=\s*\{', src)
bits = len(re.findall(r'0x[0-9A-Fa-f]{2}', src[m.end():])) if m else 0
print('位图字节数:', bits, '(%.1f KB)' % (bits / 1024.0))

over = []
for cp, w, off in rows:
    need = ((int(w) + 7) // 8) * 16
    if int(off) + need > bits:
        over.append((hex(int(cp, 16)), int(off), need))
print('越界字形:', len(over), over[:3])

print('码点范围: 0x%04X .. 0x%04X' % (min(cps), max(cps)))
print('字宽统计: 8px=%d  16px=%d' % (ws.count(8), ws.count(16)))

# ASCII 覆盖检查
missing_ascii = [chr(c) for c in range(0x20, 0x7F) if c not in cps]
print('ASCII 缺失:', missing_ascii if missing_ascii else '无')
print('自检完成')
