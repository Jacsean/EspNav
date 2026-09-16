#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""precheck.py - ESP32 firmware source pre-check (no compiler needed).

Run after every code change, before flashing. Checks:
  1) missing include: calls a function declared in a header that is not (indirectly) included
  2) missing include: uses a typedef declared in a header that is not included
  3) unbalanced braces / parens / brackets
  4) broken string literals (odd number of quotes = escape damage)
  5) static function defined but never used (-Werror=unused-function)

Usage: python Esp32S3/tools/precheck.py [main_dir]
Exit code: 0 = clean, 1 = problems found.
"""
import os
import re
import sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else 'main'
NL = chr(10)
QD = chr(34)
QS = chr(39)
BS = chr(92)

BUILTINS = set(("if for while switch return sizeof do else goto break continue "
                "case default va_start va_end va_arg va_copy typeof alignof").split())


def read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def strip_comments(src):
    """状态机剥注释：正确跳过字符串/字符常量内部的 // 与 /* （保留换行以稳定行号）"""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == QD or c == QS:                      # 字符串/字符常量：原样保留（含转义）
            q = c
            out.append(c)
            i += 1
            while i < n:
                ch = src[i]
                out.append(ch)
                if ch == BS and i + 1 < n:
                    out.append(src[i + 1])
                    i += 2
                    continue
                i += 1
                if ch == q:
                    break
            continue
        if c == chr(47) and i + 1 < n and src[i + 1] == chr(47):      # //
            while i < n and src[i] != NL:
                i += 1
            continue
        if c == chr(47) and i + 1 < n and src[i + 1] == chr(42):      # /*
            i += 2
            while i + 1 < n and not (src[i] == chr(42) and src[i + 1] == chr(47)):
                if src[i] == NL:
                    out.append(NL)
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return str().join(out)


def strip_literals(src):
    """Remove string/char literals by hand (robust, no regex escaping issues)."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == QD or c == QS:
            i += 1
            while i < n and src[i] != c:
                if src[i] == BS:
                    i += 1
                i += 1
            i += 1
            out.append(c + c)
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def parse_headers(hdr_paths):
    func2hdr, type2hdr = {}, {}
    for p in hdr_paths:
        base = os.path.basename(p)
        src = strip_comments(read(p))
        for m in re.finditer(r'(?m)^[ \t]*(?:[A-Za-z_][\w \t\*]+?)[ \t]+\**(\w+)[ \t]*\([^;{]*?\)[ \t]*;', src):
            func2hdr.setdefault(m.group(1), base)
        for m in re.finditer(r'(?m)^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]*\(', src):
            func2hdr.setdefault(m.group(1), base)
        for m in re.finditer(r'typedef[ \t]+(?:struct|union|enum)\b[^;]*?\}[ \t]*(\w+)[ \t]*;', src, flags=re.S):
            type2hdr.setdefault(m.group(1), base)
        for m in re.finditer(r'typedef[ \t]+[^;{}]+?\b(\w+)[ \t]*;', src):
            type2hdr.setdefault(m.group(1), base)
    return func2hdr, type2hdr


def includes_of(path, hdr_by_name):
    seen, stack, out = set(), [path], set()
    while stack:
        cur = stack.pop()
        if cur in seen or not os.path.exists(cur):
            continue
        seen.add(cur)
        for m in re.finditer(r'#[ \t]*include[ \t]*"([^"]+)"', read(cur)):
            name = os.path.basename(m.group(1))
            out.add(name)
            if name in hdr_by_name:
                stack.append(hdr_by_name[name])
    return out


def check_balance(path, raw):
    issues = []
    body = strip_comments(raw)
    lit = strip_literals(body)
    for op, cl, label in (('{', '}', 'braces'), ('(', ')', 'parens'), ('[', ']', 'brackets')):
        if lit.count(op) != lit.count(cl):
            issues.append('%s unbalanced: %d vs %d' % (label, lit.count(op), lit.count(cl)))
    for i, line in enumerate(body.split(NL), 1):
        probe = strip_literals(line)
        if probe.rstrip().endswith(BS):
            continue
        if probe.count(QD) % 2 == 1:
            issues.append('line %d unbalanced quote: %s' % (i, line.strip()[:80]))
    return issues


# ---- 系统头缺失检查（预检项 6）：用到 C 库函数但未 include 对应系统头 ----
SYSHDR = {
    'snprintf':     ('stdio.h',  ['sprintf', 'printf', 'vsnprintf', 'snprintf']),
    'strstr':       ('string.h', ['strlen', 'strncmp', 'strcmp', 'memcpy', 'memset', 'strncpy', 'strchr']),
    'cosf':         ('math.h',   ['sinf', 'sqrtf', 'acosf', 'fminf', 'fmaxf', 'atan2f', 'powf']),
    'atoi':         ('stdlib.h', ['strtol', 'malloc', 'free']),
}


def check_sys_headers(path, src):
    issues = []
    inc = set(re.findall(r'#include[ 	]*[<"]([^">]+)[">]', src))
    for sym, (hdr, alts) in SYSHDR.items():
        for x in [sym] + alts:
            if re.search('(^|[^A-Za-z0-9_])' + re.escape(x) + '[ 	]*[(]', src):
                if hdr not in inc:
                    issues.append('uses %s but misses #include <%s>' % (x, hdr))
                break
    return issues


# ---- 预检项 7：使用先于声明（模块级 static 变量 / 文件内 static 函数）----
def check_decl_order(path, src):
    issues = []
    lines = src.split(NL)
    var_line = {}
    for i, l in enumerate(lines):
        m = re.match(r'static[ 	]+[\w 	\*]+?[\* 	]+(\w+)[ 	]*(\[[^\]]*\])?[ 	]*(=|;)', l)
        if m and '(' not in l.split('=')[0]:
            var_line.setdefault(m.group(1), i)
    func_line = {}
    for m in re.finditer(r'(?m)^[ 	]*(?:static[ 	]+)?[A-Za-z_][\w 	\*]*?[ 	]+\**(\w+)[ 	]*\([^;]*\)[ 	]*\{', src):
        func_line.setdefault(m.group(1), src[:m.start()].count(NL))
    for name, vline in sorted(var_line.items()):
        for m in re.finditer('(^|[^A-Za-z0-9_.>])' + re.escape(name) + r'', src):
            u = src[:m.start()].count(NL)
            if u < vline:
                issues.append('uses static %s (line %d) before its definition (line %d)' % (name, u + 1, vline + 1))
                break
    for name, fline in sorted(func_line.items()):
        if fline < len(lines) and not re.match(r'static[ 	]', lines[fline]):
            continue
        for m in re.finditer('(^|[^A-Za-z0-9_.>])' + re.escape(name) + r'[ 	]*[(]', src):
            u = src[:m.start()].count(NL)
            if u < fline:
                issues.append('calls static %s (line %d) before its definition (line %d) - add a prototype' % (name, u + 1, fline + 1))
                break
    return issues


# ---- 预检项 8：控制字符（TAB/LF/CR 除外）----
def check_control_chars(path, raw):
    allowed = (chr(9), chr(10), chr(13))
    ctl = sorted(set(ch for ch in raw if ord(ch) < 32 and ch not in allowed))
    if ctl:
        return ['contains control chars: ' + ' '.join('0x%02X' % ord(c) for c in ctl)]
    return []


# ---- 预检项 9：snprintf 截断风险（-Werror=format-truncation）----
# 能精确判定的（目标与源都是本文件数组）-> 报错；目标是结构成员（无法得知实际宽度）-> 仅提示
def check_format_truncation(path, src):
    issues, hints = [], []
    body = strip_literals(strip_comments(src))
    arr = {}
    for m in re.finditer(r'char\s+(\w+)\s*\[\s*(\d+)\s*\]', body):
        arr[m.group(1)] = int(m.group(2))
    if not arr:
        return issues, hints
    for m in re.finditer(r'snprintf\s*\(\s*([^,]+?)\s*,\s*sizeof\s*\(\s*([\w.\->\[\]]+)\s*\)', body):
        dst_expr = m.group(1).strip()
        size_key = m.group(2).strip()
        tail = body[m.end(): m.end() + 240]
        stop = tail.find(');')
        if stop >= 0:
            tail = tail[:stop]
        src_used = None
        for sname, ssz in arr.items():
            if re.search('(^|[^A-Za-z0-9_])' + re.escape(sname) + '($|[^A-Za-z0-9_])', tail):
                src_used = (sname, ssz)
                break
        if not src_used:
            continue
        sname, ssz = src_used
        if size_key in arr:                       # 目标也是本文件数组 -> 精确判定
            if ssz > arr[size_key]:
                issues.append('snprintf into %s[%d] may get %s[%d] -> truncation (use memcpy + explicit length)'
                              % (size_key, arr[size_key], sname, ssz))
        elif ('.' in size_key or '->' in size_key or '(' in dst_expr):
            hints.append('check %s target vs source %s[%d]: member width unknown -> verify manually'
                         % (size_key, sname, ssz))
    return issues, hints


def check_unterminated_string(path, src):
    """C 源码里双引号字符串跨行未闭合（heredoc 折叠 \n 的典型后果，必然编译失败）"""
    issues = []
    body = strip_comments(src)
    for i, line in enumerate(body.split('\n'), 1):
        n, j = 0, 0
        while j < len(line):
            c = line[j]
            if c == '\\':
                j += 2
                continue
            if c == "'":            # 字符字面量：整段跳过（里面可能包含引号，如 '\'"\''）
                j += 1
                while j < len(line):
                    if line[j] == '\\':
                        j += 2
                        continue
                    if line[j] == "'":
                        j += 1
                        break
                    j += 1
                continue
            if c == '"':
                n += 1
            j += 1
        if n % 2 == 1:
            issues.append('line %d: unterminated string literal (missing terminating quote)' % i)
    return issues


def check_color_macros(root):
    """使用了未定义的 RGB565_* 颜色宏（C 编译必报 undeclared identifier）"""
    defined, used = set(), {}
    for dirpath, _, names in os.walk(root):
        for fn2 in names:
            if not fn2.endswith(('.c', '.h')):
                continue
            fp = os.path.join(dirpath, fn2)
            src = open(fp, encoding='utf-8', errors='replace').read()
            defined |= set(re.findall(r'#define\s+(RGB565_\w+)', src))
            body = strip_literals(strip_comments(src))
            for m in set(re.findall(r'(RGB565_[A-Z0-9_]+)', body)):
                used.setdefault(m, fp)
    return ['%s: undefined color macro %s' % (fp, m) for m, fp in sorted(used.items()) if m not in defined]


def main():
    if not os.path.isdir(ROOT):
        print('dir not found: %s' % ROOT)
        return 1
    files = []
    for dp, _, fns in os.walk(ROOT):
        for fn in fns:
            if fn.endswith(('.c', '.h')):
                files.append(os.path.join(dp, fn).replace(BS, '/'))
    hdr_paths = [f for f in files if f.endswith('.h')]
    c_paths = [f for f in files if f.endswith('.c')]
    hdr_by_name = {os.path.basename(p): p for p in hdr_paths}
    func2hdr, type2hdr = parse_headers(hdr_paths)

    problems = 0
    for c in c_paths:
        raw = read(c)
        inc = includes_of(c, hdr_by_name)
        src = strip_comments(raw)
        defined = set(re.findall(r'(?m)^[ \t]*(?:static[ \t]+)?[A-Za-z_][\w \t\*]*?[ \t]+\**(\w+)[ \t]*\([^;]*\)[ \t]*\{', src))

        missing = defaultdict(set)
        for m in re.finditer(r'\b([A-Za-z_]\w*)[ \t]*\(', src):
            fn = m.group(1)
            if fn in BUILTINS or fn in defined:
                continue
            hdr = func2hdr.get(fn)
            if hdr and hdr not in inc:
                missing[hdr].add(fn)
        for hdr, fns in sorted(missing.items()):
            print('[missing include] %s: #include "%s"  (uses: %s)' % (c, hdr, ', '.join(sorted(fns))))
            problems += 1

        tmiss = defaultdict(set)
        for m in re.finditer(r'\b([A-Za-z_]\w*_t)\b', src):
            ty = m.group(1)
            hdr = type2hdr.get(ty)
            if hdr and hdr not in inc:
                tmiss[hdr].add(ty)
        for hdr, tys in sorted(tmiss.items()):
            print('[missing include] %s: #include "%s"  (types: %s)' % (c, hdr, ', '.join(sorted(tys))))
            problems += 1

        tr_issues, tr_hints = check_format_truncation(c, raw)
        for msg in tr_issues:
            print('[truncation] %s: %s' % (c, msg))
            problems += 1
        for msg in tr_hints:
            print('[hint] %s: %s' % (c, msg))

        for msg in check_control_chars(c, raw):
            print('[control-char] %s: %s' % (c, msg))
            problems += 1

        for msg in check_decl_order(c, src):
            print('[decl-order] %s: %s' % (c, msg))
            problems += 1

        for msg in check_sys_headers(c, raw):
            print('[missing sysheader] %s: %s' % (c, msg))
            problems += 1

        for msg in check_unterminated_string(c, raw):
            print('[string] %s: %s' % (c, msg))
            problems += 1

        for msg in check_balance(c, raw):
            print('[syntax] %s: %s' % (c, msg))
            problems += 1

        for m in re.finditer(r'(?m)^[ \t]*static[ \t]+[\w \t\*]+?[ \t]+\**(\w+)[ \t]*\([^;]*\)[ \t]*\{', src):
            fn = m.group(1)
            if len(re.findall(r'\b%s\b' % re.escape(fn), src)) < 2:
                print('[unused] %s: static %s never used (-Werror=unused-function)' % (c, fn))
                problems += 1

    for msg in check_color_macros(ROOT):
        print('[color] %s' % msg)
        problems += 1

    print('----')
    print('checked %d .c / %d .h, problems: %d' % (len(c_paths), len(hdr_paths), problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
