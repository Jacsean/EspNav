/**
 * send_frames.js — PC 端测试脚本：向 ESP32 TCP :8899 发送协议帧（V1.10）
 * 用法：
 *   node send_frames.js --demo                     # 条带帧序列（默认）
 *   node send_frames.js --road all                 # 依次演示全部路况模板
 *   node send_frames.js --road crossRight          # 只发某一种模板
 *   node send_frames.js --file frames.jsonl        # 每行一个完整 JSON 帧
 *   （可选 --host 192.168.4.1 --port 8899）
 * 真机语义：车标 pos 固定；两侧虚线滚动由渲染端按 dash_speed 驱动。
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
const ROAD = arg('road', null);

/* 模板样例（参数与 nav_sim_v2.html / 几何规格 V0.3 一致；近端 y=144） */
const ROAD_SAMPLES = {
  straight:  { type: 'straight', pts: [[160,144],[160,30]], half: 62 },
  curveL:    { type: 'curve', pts: [[160,144],[160,112],[150,82],[118,52],[96,30]], half: 62 },
  curveR:    { type: 'curve', pts: [[160,144],[160,112],[172,82],[204,52],[226,30]], half: 62 },
  tLeft:     { type: 'tjunc', dir: 'left',  pts: [[160,144],[160,70],[32,70]], half: 62 },
  crossRight:{ type: 'cross', dir: 'right', pts: [[160,144],[160,70],[300,70]], half: 62 },
  rbt2:      { type: 'roundabout', exits: ['W','N','E'], dir: 'exit2' },   /* 目标=第2出口(北, 180°弧) */
  rbt3:      { type: 'roundabout', exits: ['W','N','E'], dir: 'exit3' },   /* 目标=第3出口(东, 270°弧) */
  multi:     { type: 'multi', pts: [[160,144],[160,68],[110,40]], half: 62 },
  forkRight: { type: 'fork', dir: 'right', pts: [[160,100],[180,78],[214,50],[252,32]], half: 62 },
};
const ROAD_HINTS = {
  straight:'前方800米直行', curveL:'前方弯道200米向左', curveR:'前方弯道200米向右',
  tLeft:'前方150米路口左转', crossRight:'十字路口右转 120米',
  rbt2:'前方300米环岛第2出口驶出', rbt3:'前方300米环岛第3出口驶出',
  multi:'多岔路口走左侧支路', forkRight:'靠右驶出匝道 400米',
};

/* 行程图轨迹（小地图局部坐标 0..40；dot 沿线推进，肉眼可见变化） */
const OV_TRACKS = {
  straight:   [[8,6],[14,12],[20,18],[26,24],[32,30],[38,36]],
  curveL:     [[8,8],[14,10],[20,14],[26,20],[30,28],[34,36]],
  curveR:     [[8,8],[14,12],[18,18],[22,26],[26,33],[30,37]],
  tLeft:      [[8,6],[16,12],[24,18],[24,26],[24,34],[24,38]],
  crossRight: [[8,6],[16,12],[24,18],[32,20],[36,20],[38,20]],
  rbt2:       [[8,6],[16,12],[24,18],[26,14],[28,10],[30,8]],
  rbt3:       [[8,6],[16,12],[24,18],[30,16],[34,14],[38,14]],
  multi:      [[8,6],[16,12],[24,18],[22,24],[18,30],[16,36]],
  forkRight:  [[8,6],[16,12],[24,18],[28,22],[34,28],[38,34]],
  strip:      [[8,6],[14,12],[20,18],[26,24],[32,30],[38,36]],
};
/* step 越大 -> 走过的轨迹越长、黄点越靠前（每一帧都不同） */
function ovFor(key, step) {
  const tr = OV_TRACKS[key] || OV_TRACKS.strip;
  const k = Math.max(1, Math.min(tr.length, step));
  const ov = tr.slice(0, k);
  return { overview: ov, overview_dot: ov[ov.length - 1] };
}

function frameObj(payload, type = 'NAV_FRAME') {
  return JSON.stringify({ msg_type: type, payload: payload }) + '\n';
}
function basePayload(hint, key, idx) {
  return {
    heading: (idx * 40) % 360, turn_dist: 120, hint: hint,
    total_dist: 11400, progress_pct: 26, elapsed_min: 19, eta_time: '15:11',
    centerLine: [[160,144],[160,120],[160,90],[160,60],[160,34]],
    pastCenter: [[160,144],[160,120]],
    routeCenter: [[160,120],[160,90],[160,60],[160,34]],
    pos: [160, 110]
  };
}
function stripPayload(turnDist, hint, step) {
  const center = [[160,144],[160,118],[160,86],[160,54],[160,30]];
  const pc = 1 + Math.floor((500 - turnDist) / 200);
  const base = {
    heading: (turnDist * 3) % 360, turn_dist: turnDist, hint: hint,
    total_dist: 8200, progress_pct: 34, elapsed_min: 28, eta_time: '14:27',
    centerLine: center,
    pastCenter: center.slice(0, Math.max(2, Math.min(4, pc + 1))),
    routeCenter: center.slice(Math.max(1, pc)),
    pos: [160, 110]
  };
  return Object.assign(base, ovFor('strip', step || 1));
}

function roadList() {
  if (!ROAD) return [];
  const keys = Object.keys(ROAD_SAMPLES);
  if (ROAD === 'all') return keys;
  return ROAD_SAMPLES[ROAD] ? [ROAD] : [];
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
    return;
  }
  if (ROAD) {
    const keys = roadList();
    let i = 0, t;
    const send = () => {
      const k = keys[Math.floor(i / 3) % keys.length];
      const step = (i % 3) + 1;                    /* 每模板 3 帧：行程图逐帧推进 */
      const pl = basePayload(ROAD_HINTS[k] || k, k, i);
      pl.road = ROAD_SAMPLES[k];
      Object.assign(pl, ovFor(k, step));
      sock.write(frameObj(pl));
      console.log('[tx] road=' + k + ' step=' + step + ' dot=' + JSON.stringify(pl.overview_dot));
      i++;
      if (i >= keys.length * 3) { clearInterval(t); sock.end(); }
    };
    send();
    t = setInterval(send, 700);
    return;
  }
  /* 默认：条带 demo */
  let n = 0;
  const t = setInterval(() => {
    n++;
    if (n <= 10) {
      const turnDist = 500 - n * 40;
      sock.write(frameObj(stripPayload(turnDist, '前方' + turnDist + '米直行', n)));
      console.log('[tx] strip frame#' + n + ' turn_dist=' + turnDist);
    } else if (n === 11) {
      sock.write(frameObj({ ts: Math.floor(Date.now() / 1000) }, 'PING'));
      console.log('[tx] PING');
    } else if (n === 12) {
      sock.write(frameObj({}, 'GET_CONFIG'));
      console.log('[tx] GET_CONFIG');
    } else { clearInterval(t); sock.end(); }
  }, 400);
});
sock.on('data', d => console.log('[rx] ' + d.toString().trim()));
sock.on('error', e => console.log('[err] ' + e.message + '（确认电脑已连上 ESPNav-AP 热点）'));
sock.on('close', () => console.log('[close]'));
