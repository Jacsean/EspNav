/**
 * send_frames.js — PC 端测试脚本：向 ESP32 TCP :8899 发送协议帧（V1.10）
 * 用法：
 *   node send_frames.js --host 192.168.4.1 --port 8899 --demo
 *   node send_frames.js --file frames.jsonl     # 每行一个完整 JSON 帧（含 \n 自动补）
 * 说明：先让电脑连上 ESP32 的热点 ESPNav-AP（密码 espnav1234），再运行本脚本。
 */
'use strict';
const net = require('net');
const fs = require('fs');

function arg(name, def) {
  const i = process.argv.indexOf('--' + name);
  return i > 0 ? process.argv[i + 1] : def;
}
const HOST = arg('host', '192.168.4.1');
const PORT = parseInt(arg('port', '8899'), 10);
const FILE = arg('file', null);
const DEMO = process.argv.includes('--demo') || !FILE;

function frame(payload, type = 'NAV_FRAME') {
  return JSON.stringify({ msg_type: type, payload: payload }) + '\n';
}
function stripFrame(heading, turnDist, hint, posY) {
  return {
    heading: heading, turn_dist: turnDist, hint: hint,
    total_dist: 8200, progress_pct: 34, elapsed_min: 28, eta_time: '14:27',
    centerLine: [[160, 130], [160, 110], [160, 80], [160, 50], [160, 28]],
    pastCenter: [[160, 130], [160, 110]],
    routeCenter: [[160, 110], [160, 80], [160, 50], [160, 28]],
    pos: [160, typeof posY === 'number' ? posY : 110]
  };
}

const sock = net.connect(PORT, HOST, () => {
  console.log('[ok] connected ' + HOST + ':' + PORT);
  if (FILE) {
    const lines = fs.readFileSync(FILE, 'utf8').split(/\r?\n/).filter(Boolean);
    let i = 0;
    const t = setInterval(() => {
      if (i >= lines.length) { clearInterval(t); sock.end(); return; }
      sock.write(lines[i].trim() + '\n');
      console.log('[tx] ' + lines[i].slice(0, 100));
      i++;
    }, 300);
  } else {
    // demo：直行 10 帧（车标/距离/提示变化）-> PING -> GET_CONFIG
    let n = 0;
    const t = setInterval(() => {
      n++;
      if (n <= 10) {
        const turnDist = 500 - n * 40;
        const y = 118 - n * 4;
        const msg = frame(stripFrame(0, turnDist, '前方' + turnDist + '米直行', y));
        sock.write(msg);
        console.log('[tx] frame#' + n + ' turn_dist=' + turnDist);
      } else if (n === 11) {
        sock.write(frame({ ts: Math.floor(Date.now() / 1000) }, 'PING'));
        console.log('[tx] PING');
      } else if (n === 12) {
        sock.write(frame({}, 'GET_CONFIG'));
        console.log('[tx] GET_CONFIG');
      } else {
        clearInterval(t); sock.end();
      }
    }, 400);
  }
});
sock.on('data', d => console.log('[rx] ' + d.toString().trim()));
sock.on('error', e => console.log('[err] ' + e.message + '（确认电脑已连上 ESPNav-AP 热点）'));
sock.on('close', () => console.log('[close]'));
