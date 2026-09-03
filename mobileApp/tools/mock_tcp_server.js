/**
 * mock_tcp_server.js — 按 ble_protocol V1.10 模拟 ESP32 TCP Server（无板联调用）
 * 用途：手机 App / road_gen 输出在无 ESP32 硬件时验证"帧 → 网络 → 对端"链路。
 * 行为（模拟 ESP32 承载）：
 *   - TCP Server :8899（可 --port 覆盖），按 `\n` 切帧、JSON.parse；
 *   - 收到 PING → 回 PONG{ts:原样}；GET_CONFIG → 回 DEV_STATUS 样例；其余打印摘要；
 *   - 非法 JSON 计数并打印。
 * 运行：node mock_tcp_server.js [--port 8899]
 */
'use strict';
const net = require('net');

const port = (function(){
  const i = process.argv.indexOf('--port');
  return i > 0 ? parseInt(process.argv[i+1], 10) : 8899;
})();

const DEV_STATUS = {msg_type:'DEV_STATUS', payload:{
  lcd_brightness:80, dash_speed:60, anim_enable:true, popup_timeout:5,
  firmware_ver:'V0.0-mock', err:0
}};

let buf = '';
let errCount = 0;

const server = net.createServer(function(sock){
  const peer = sock.remoteAddress + ':' + sock.remotePort;
  console.log('[conn] ' + peer);
  buf = '';
  sock.on('data', function(chunk){
    buf += chunk.toString('utf8');
    let nl;
    while((nl = buf.indexOf('\n')) >= 0){
      const raw = buf.slice(0, nl).trim();
      buf = buf.slice(nl + 1);
      if(!raw) continue;
      let obj = null;
      try{ obj = JSON.parse(raw); }catch(e){ errCount++; console.log('[bad-json #' + errCount + '] ' + raw.slice(0, 120)); continue; }
      const mt = obj && obj.msg_type;
      if(mt === 'PING'){
        sock.write(JSON.stringify({msg_type:'PONG', payload:{ts: obj.payload && obj.payload.ts}}) + '\n');
        console.log('[ping] ts=' + ((obj.payload && obj.payload.ts) || '?') + ' -> PONG');
      }else if(mt === 'GET_CONFIG'){
        sock.write(JSON.stringify(DEV_STATUS) + '\n');
        console.log('[get_config] -> DEV_STATUS');
      }else if(mt === 'NAV_FRAME'){
        const p = obj.payload || {};
        console.log('[NAV_FRAME] hint="' + (p.hint||'') + '" turn_dist=' + p.turn_dist +
          (p.road ? ' road.type=' + p.road.type + ' dir=' + (p.road.dir||'-') + ' pts=' + (p.road.pts?p.road.pts.length:0) : ' (条带)'));
      }else{
        console.log('[' + mt + '] ' + raw.slice(0, 160));
      }
    }
  });
  sock.on('close', function(){ console.log('[close] ' + peer); });
  sock.on('error', function(e){ console.log('[sock-err] ' + e.message); });
});

server.listen(port, function(){
  console.log('mock ESP32 TCP server（协议 V1.10）监听 :' + port + ' —— 按 Ctrl+C 停止');
});
server.on('error', function(e){ console.error('listen 失败: ' + e.message); process.exit(1); });
