# 预览 HTML 静态检查：script 语法 + 未定义的函数调用（我看不到浏览器渲染，这是唯一防线）
import re, sys, subprocess, os

BUILTIN = set('''if for while switch catch function return typeof new delete void in instanceof do else try
parseInt parseFloat isNaN isFinite Number String Boolean Array Object Math JSON Date RegExp Error
document window requestAnimationFrame setInterval setTimeout clearTimeout console alert
encodeURIComponent decodeURIComponent Promise Set Map Symbol'''.split())
BUILTIN |= set('forEach map filter reduce find findIndex some every includes indexOf slice splice concat join push pop shift unshift sort reverse keys values entries'.split())

def check(path):
    html = open(path, encoding='utf-8').read()
    m = re.search(r'<script>(.*)</script>', html, re.S)
    if not m:
        return ['no <script> block'], 0
    js = m.group(1)
    tmp = path + '.tmpcheck.js'
    open(tmp, 'w', encoding='utf-8', newline='').write(js)
    r = subprocess.run(['node', '--check', tmp], capture_output=True, text=True)
    os.remove(tmp)
    problems = []
    if r.returncode != 0:
        problems.append('JS syntax error: ' + (r.stderr.strip().split('\n')[-1] if r.stderr else '?'))
    body = re.sub(r'/\*.*?\*/', ' ', js, flags=re.S)     # 去掉块注释，避免注释里的函数名被当作调用
    body = re.sub(r'//[^\n]*', ' ', body)   # 去掉行注释（\n 在此必须保持反斜杠形式）
    defined = set(re.findall(r'function\s+(\w+)\s*\(', body))
    defined |= set(re.findall(r'(?:let|const|var)\s+(\w+)\s*=\s*(?:function|\()', body))
    called = set(re.findall(r'(?<![\w.$])([A-Za-z_]\w*)\s*\(', body))
    missing = sorted(called - defined - BUILTIN)
    for name in missing:
        problems.append("call to undefined function: %s()" % name)
    return problems, len(js.split('\n'))

if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else 'nav_compass.html'
    probs, lines = check(target)
    for p2 in probs:
        print('[html] %s: %s' % (target, p2))
    print('----')
    print('checked %s (script %d lines), problems: %d' % (target, lines, len(probs)))
    sys.exit(1 if probs else 0)
