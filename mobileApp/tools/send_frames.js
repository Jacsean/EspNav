/**
 * send_frames.js — PC 端测试脚本：向 ESP32 TCP :8899 发送协议帧（V1.10）
 * 用法：
 *   node send_frames.js --host 192.168.4.1 --port 8899 --demo
 *   node send_frames.js --file frames.jsonl     # 每行一个完整 JSON 帧
 * 真机语义（M3 起固件按此实现）：
 *   - 车标 pos 固定（车头朝上视角，车在画面底部）；
 *   - centerLine/pastCenter/routeCenter 随行驶更新；
 *   - 两侧虚线“向下滚动”由渲染端按 dash_speed 做动画，不靠帧位移。
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

function frameObj(payload, type = 'NAV_FRAME') {
  return JSON.stringify({ msg_type: type, payload: payload }) + '\n';
}
/* 固定中心线；pastCount 增长模拟“已行驶段变长”，pos 固定不动 */
function stripFrame(turnDist, hint, pastCount) {
  const center = [[160, 130], [160, 110], [160, 80], [160, 50], [160, 28]];
  const pc = Math.max(1, Math.min(center.length - 1, pastCount));
  return {
    heading: 0, turn_dist: turnDist, hint: hint,
    total_dist: 8200, progress_pct: 34, elapsed_min: 28, eta_time: '14:27',
    centerLine: center,
    pastCenter: center.slice(0, pc + 1),
    routeCenter: center.slice(pc),
    pos: [160, 108]                      /* 车标固定；较底边(130)上移约一个车身 */
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
    let n = 0;
    const t = setInterval(() => {
      n++;
      if (n <= 10) {
        const turnDist = 500 - n * 40;
        sock.write(frameObj(stripFrame(turnDist, '前方' + turnDist + '米直行', 1 + Math.floor(n / 4))));
        console.log('[tx] frame#' + n + ' turn_dist=' + turnDist + ' past=' + (1 + Math.floor(n / 4)));
      } else if (n === 11) {
        sock.write(frameObj({ ts: Math.floor(Date.now() / 1000) }, 'PING'));
        console.log('[tx] PING');
      } else if (n === 12) {
        sock.write(frameObj({}, 'GET_CONFIG'));
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
