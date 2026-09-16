#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""serve_apk.py — 局域网分发 APK（多线程 + 断点续传），零流量
用法：python tools/serve_apk.py [端口，默认 8000]
手机浏览器打开脚本打印的地址即可下载（中断后可续传）
"""
import os
import socket
import sys
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=ROOT, **kw)

    def send_head(self):
        rng = self.headers.get('Range')
        path = self.translate_path(self.path)
        if not rng or not os.path.isfile(path):
            return super().send_head()
        try:
            start = int(rng.split('=')[1].split('-')[0] or 0)
        except Exception:
            return super().send_head()
        size = os.path.getsize(path)
        f = open(path, 'rb')
        f.seek(start)
        self.send_response(206)
        self.send_header('Content-Type', 'application/vnd.android.package-archive')
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, size - 1, size))
        self.send_header('Content-Length', str(size - start))
        self.end_headers()
        return f

    def log_message(self, fmt, *args):
        sys.stdout.write('[http] ' + (fmt % args) + chr(10))
        sys.stdout.flush()


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        return s.getsockname()[0]
    except Exception:
        return '127.0.0.1'
    finally:
        s.close()


if __name__ == '__main__':
    os.chdir(ROOT)
    print('服务目录: ' + ROOT)
    print('手机浏览器打开: http://%s:%d/android/espnav-debug.apk' % (lan_ip(), PORT))
    print('（多线程 + 支持断点续传；Ctrl+C 结束）')
    ThreadingHTTPServer(('0.0.0.0', PORT), Handler).serve_forever()
