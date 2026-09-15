#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""kotlin_precheck.py - Android(Kotlin/XML) 工程静态预检（无需 Android SDK）

检查项：
  1) Kotlin 括号/引号平衡
  2) suspend fun 内裸用 isActive（接收者不符 -> Unresolved reference；应写 coroutineContext.isActive 或 scope.isActive）
  3) XML 合法性（Manifest/layout/values 解析）
  4) layout 里的 id 与代码里 binding.xxx 的对应关系
  5) @string/... 引用是否都存在
  6) 未使用的 import（提示，不计入问题数）

用法：python mobileApp/android/tools/kotlin_precheck.py [工程根，默认 .]
退出码：0 = 通过；1 = 有问题
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = sys.argv[1] if len(sys.argv) > 1 else '.'
NL = chr(10)
Q = chr(34)
SQ = chr(39)
BS = chr(92)


def read(p):
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


def strip_literals(s):
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c in (Q, SQ):
            i += 1
            while i < len(s) and s[i] != c:
                if s[i] == BS:
                    i += 1
                i += 1
            i += 1
            out.append(c + c)
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def strip_comments(s):
    s = re.sub(re.escape('/' + '*') + '.*?' + re.escape('*' + '/'),
               lambda m: NL * m.group(0).count(NL), s, flags=re.S)
    s = re.sub(re.escape('/' + '/') + '[^' + NL + ']*', ' ', s)
    return s


def check_balance(path, src):
    issues = []
    body = strip_literals(strip_comments(src))
    for op, cl, label in (('{', '}', 'braces'), ('(', ')', 'parens'), ('[', ']', 'brackets')):
        if body.count(op) != body.count(cl):
            issues.append('%s unbalanced: %d vs %d' % (label, body.count(op), body.count(cl)))
    return issues


def check_bare_isactive(path, src):
    """suspend fun（签名无 CoroutineScope 接收者）里裸用 isActive -> 编译错误"""
    issues = []
    body = strip_literals(strip_comments(src))
    lines = body.split(NL)
    depth = 0
    suspend_at_depth = None
    pending_suspend = False
    for i, raw in enumerate(lines, 1):
        line = raw
        m = re.search(r'suspend\s+fun\s+([\w.]+\s*\.\s*)?(\w+)\s*\(', line)
        if m:
            receiver = m.group(1)
            pending_suspend = (receiver is None)      # 无接收者才算“裸”
        if pending_suspend and '{' in line:
            suspend_at_depth = depth
            pending_suspend = False
        if suspend_at_depth is not None:
            for mm in re.finditer(r'(?<![.\w])isActive\b', line):
                issues.append('line %d: bare isActive inside suspend fun - use coroutineContext.isActive / scope.isActive' % i)
                break
        depth += line.count('{') - line.count('}')
        if suspend_at_depth is not None and depth <= suspend_at_depth:
            suspend_at_depth = None
    return issues


# ---- 预检项 3：必需 import（用到某符号但没有 import 其声明处 -> Unresolved reference）----
REQUIRED_IMPORTS = {
    'isActive':          'kotlinx.coroutines.isActive',
    'coroutineContext':  'kotlin.coroutines.coroutineContext',
    'launch':            'kotlinx.coroutines.launch',
    'delay':             'kotlinx.coroutines.delay',
    'withContext':       'kotlinx.coroutines.withContext',
    'Dispatchers':       'kotlinx.coroutines.Dispatchers',
    'Channel':           'kotlinx.coroutines.channels.Channel',
    'Job':               'kotlinx.coroutines.Job',
    'lifecycleScope':    'androidx.lifecycle.lifecycleScope',
    'Log':               'android.util.Log',
    'JSONObject':        'org.json.JSONObject',
    'JSONArray':         'org.json.JSONArray',
    'AppCompatActivity': 'androidx.appcompat.app.AppCompatActivity',
    'SeekBar':           'android.widget.SeekBar',
    'View':              'android.view.View',
    'SimpleDateFormat':  'java.text.SimpleDateFormat',
    'Locale':            'java.util.Locale',
}

PRE = '(^|[^A-Za-z0-9_.])'
POST = '($|[^A-Za-z0-9_])'


def check_required_imports(path, src):
    issues = []
    body = strip_literals(strip_comments(src))
    imports = set(re.findall(r'(?m)^[ 	]*import[ 	]+([\w.]+)', src))
    for sym, fq in sorted(REQUIRED_IMPORTS.items()):
        if re.search(PRE + re.escape(sym) + POST, body) and fq not in imports:
            issues.append('uses %s but misses "import %s" (Unresolved reference)' % (sym, fq))
    return issues


def main():
    kt_files, xml_files = [], []
    for dp, _, fns in os.walk(ROOT):
        if any(x in dp for x in ('.gradle', 'build', '.idea')):
            continue
        for fn in fns:
            p = os.path.join(dp, fn).replace(BS, '/')
            if fn.endswith('.kt'):
                kt_files.append(p)
            elif fn.endswith('.xml'):
                xml_files.append(p)
    if not kt_files and not xml_files:
        print('未找到 Kotlin/XML 文件（root=%s）' % ROOT)
        return 1

    problems = 0
    for p in kt_files:
        src = read(p)
        for msg in check_balance(p, src):
            print('[syntax] %s: %s' % (p, msg))
            problems += 1
        for msg in check_required_imports(p, src):
            print('[import] %s: %s' % (p, msg))
            problems += 1

        for msg in check_bare_isactive(p, src):
            print('[coroutine] %s: %s' % (p, msg))
            problems += 1
        # 未使用 import（提示）
        body = NL.join(l for l in src.split(NL) if not l.strip().startswith('import '))
        unused = []
        for m in re.finditer(r'^import\s+([\w.]+)(?:\s+as\s+(\w+))?', src, flags=re.M):
            name = m.group(2) or m.group(1).split('.')[-1]
            if not re.search(r'(^|[^A-Za-z0-9_.])' + re.escape(name) + r'($|[^A-Za-z0-9_])', body):
                unused.append(name)
        if unused:
            print('[hint] %s: 可能未使用的 import: %s' % (p, ', '.join(unused)))

    layouts = []
    for p in xml_files:
        src = read(p)
        try:
            ET.fromstring(src)
        except Exception as e:
            print('[xml] %s: 解析失败: %s' % (p, e))
            problems += 1
        if p.endswith('res/layout/activity_main.xml'):
            layouts.append(p)

    # layout id <-> binding 属性
    if layouts:
        ids = set()
        for p in layouts:
            ids |= set(re.findall(r'android:id="@\+id/(\w+)"', read(p)))
        used = set()
        for p in kt_files:
            # 只取 binding.<小写开头属性>，避免命中 import ...databinding.ActivityMainBinding
            for m in re.finditer(r'binding\.([a-z]\w*)', read(p)):
                used.add(m.group(1))
        missing = sorted(used - ids - {'root'})
        if missing:
            print('[layout] 代码用到但布局不存在的 id: %s' % ', '.join(missing))
            problems += 1
        else:
            print('[layout] id 与 binding 属性一致（%d 个）' % len(ids))

    # @string 引用检查
    str_files = [p for p in xml_files if p.endswith('strings.xml')]
    if str_files:
        names = set(re.findall(r'name="(\w+)"', read(str_files[0])))
        refs = set()
        for p in xml_files:
            refs |= set(re.findall(r'@string/(\w+)', read(p)))
        for p in kt_files:
            refs |= set(re.findall(r'R\.string\.(\w+)', read(p)))
        miss = sorted(refs - names)
        if miss:
            print('[strings] 引用但未定义的字符串资源: %s' % ', '.join(miss))
            problems += 1
        else:
            print('[strings] 引用全部存在（%d 个）' % len(refs))

    print('----')
    print('checked %d .kt / %d .xml, problems: %d' % (len(kt_files), len(xml_files), problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
