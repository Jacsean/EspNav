/**
 * road_gen.js — 手机端「路况模板几何生成器」原型（方案 A，2026-09-03）
 * =====================================================================
 * 职责（ble_protocol V1.8 §3.1.1 + 模板几何规格 V0.3，H1-A/H2-A/O1/O2/O3/O6 落地）：
 *   输入 导航 SDK 实时回调（机动类型/提示/剩余距离 + 车前局部路线 polyline(米, 车辆系)）
 *   → 输出 NAV_FRAME payload（含 road/pts/exits），供 ESP32/HTML 渲染端 readRoad 实时绘制。
 *
 * 约定（与渲染端一致，均为「车头朝上平面像素坐标」）：
 *   - 屏幕系：x:0..319（右+），y:0..239（下+）；车辆位于底部中央 (160,134)；
 *   - 车前距离 → y 映射：近端 y=134、可视远界 y≈20（pxPerM 由可视距离推算）；
 *   - roundabout 平面常量与 nav_sim_v2.html RBT 一致（cx160/cy84/r58，b=0.40r…）；
 *   - 本原型为纯逻辑参考实现（node 可测），后续以 Kotlin 移植进 Android（实施计划阶段4-1）。
 *
 * 运行：node road_gen.js            —— 打印示例帧
 *       node test_road_gen.js       —— 结构断言（协议组合/点数/坐标界/出口换算）
 */

'use strict';

// ---- 与渲染端共享的几何常量（模板几何规格 V0.3 §5.3 / nav_sim_v2.html RBT）----
const PLANE = {
  carX: 160, carY: 134,        // 车辆屏幕位（南入口/条带近端）
  yFar: 20,                    // 可视远界 y
  rbt: {cx: 160, cy: 84, r: 58, b: 0.40, aIn: 0.66, bIn: 0.17} // O2-A（r=观感水平半轴）
};
const AZM = {E:0, SE:45, S:90, SW:135, W:180, NW:225, N:270, NE:315}; // 方位→参数角(°)

// ---- 工具 ----
function clamp(v, lo, hi){ return Math.max(lo, Math.min(hi, v)); }
function rad(name){ return ((AZM[name] !== undefined ? AZM[name] : 0)) * Math.PI / 180; }
/** 车辆系局部路线(米, x=右 y=前) → 车头朝上屏幕坐标（不做透视，渲染端负责视觉） */
function screenize(pathM, pxPerM){
  const pts = (pathM || []).map(function(p){
    return [PLANE.carX + p[0] * pxPerM, PLANE.carY - p[1] * pxPerM];
  }).map(function(p){
    return [Math.round(clamp(p[0], 0, 319)), Math.round(clamp(p[1], PLANE.yFar, 239))];
  });
  return pts;
}
/** 等距抽稀到 ≤n 点（保持首尾） */
function decimate(pts, n){
  if(!pts || pts.length <= n) return pts || [];
  const step = (pts.length - 1) / (n - 1);
  const out = [];
  for(let i = 0; i < n; i++) out.push(pts[Math.round(i * step)]);
  return out;
}
function headingNorm(h){ return ((h % 360) + 360) % 360; }

// ---- 机动 → 模板类型映射（ble_protocol V1.8 type×dir 组合表）----
function typeForManeuver(m, junction){
  switch(m){
    case 'turn_left':  return {type: junction === 'tjunc' ? 'tjunc' : 'cross', dir: 'left'};
    case 'turn_right': return {type: junction === 'tjunc' ? 'tjunc' : 'cross', dir: 'right'};
    case 'fork_left':  return {type: 'fork', dir: 'left'};
    case 'fork_right': return {type: 'fork', dir: 'right'};
    case 'curve':      return {type: 'curve', dir: 'left'};   // 弯向由 pts 表达，dir 仅示意
    case 'roundabout': return {type: 'roundabout', dir: null}; // dir=exitN 由出口换算填入
    default:           return null;                            // straight/uturn 不套模板
  }
}

// ---- roundabout 出口计数换算（O1：SDK 播报口径 → 渲染几何口径 dir=exitN）----
// sdkIncludesEntry=true 表示 SDK 把南入口计入出口编号（如 v3 样例「第2出口=东」含入口）
function sdkExitToDir(sdkN, sdkIncludesEntry){
  const dirN = sdkN - (sdkIncludesEntry ? 1 : 0);
  if(dirN < 1) throw new Error('sdkExitToDir: 出口编号非法 sdkN=' + sdkN + ' includesEntry=' + sdkIncludesEntry);
  return dirN;
}

// ---- roundabout 绿路径 pts（平面坐标；与渲染端方向弧算法同构，保证两端一致）----
// exits: 不含南入口、按绕行方向排列；dirN: 几何出口序（1 起，目标=exits[dirN-1]）
function roundaboutPts(exits, dirN){
  const rb = PLANE.rbt;
  const cx = rb.cx, cy = rb.cy, r = rb.r, b = r * rb.b;
  const aIn = r * rb.aIn, bIn = r * rb.bIn;
  const exitsL = (exits && exits.length) ? exits : ['W', 'N', 'E'];
  const ti = clamp((dirN || 1) - 1, 0, exitsL.length - 1);
  const target = exitsL[ti];
  const thS = rad('S');
  let d0 = rad(exitsL[0]) - thS;
  while(d0 >  Math.PI) d0 -= 2 * Math.PI;
  while(d0 < -Math.PI) d0 += 2 * Math.PI;
  const spin = (d0 >= 0) ? 1 : -1;
  const thT = rad(target), tau = 2 * Math.PI;
  let sweep = spin > 0 ? ((thT - thS) % tau + tau) % tau : -(((thS - thT) % tau + tau) % tau);
  if(sweep === 0) sweep = spin * tau * 0.999;
  const aM = (r + aIn) / 2, bM = (b + bIn) / 2;
  const N = 32;
  const arc = [];
  for(let i = 0; i <= N; i++){
    const th = thS + sweep * i / N;
    arc.push([Math.round(cx + aM * Math.cos(th)), Math.round(cy + bM * Math.sin(th))]);
  }
  const outP = [Math.round(cx + r * Math.cos(thT)), Math.round(cy + b * Math.sin(thT))];
  const path = [[PLANE.carX, PLANE.carY], [Math.round(cx), Math.round(cy + b)]]
    .concat(arc, [outP]);
  return decimate(path, 16);
}

// ---- 组装 NAV_FRAME payload（核心入口）----
// nav: {
//   heading, turn_dist, hint, total_dist, progress_pct, elapsed_min, eta_time,  // 导航回调字段
//   pathM: [[x_m,y_m],...],            // 车前局部路线（车辆系，米；含转向走向）
//   visMeters: 140,                    // 可视距离（米）→ 决定 pxPerM
//   maneuver: 'straight'|'turn_left'|'turn_right'|'roundabout'|'fork_left'|'fork_right'|'curve'|'uturn',
//   junction: 'cross'|'tjunc',         // turn_* 时的路口形态（默认 cross）
//   rbtExits: ['W','N','E'],           // roundabout：候选出口方位（不含入口、绕行序）
//   rbtSdkExit: 2, rbtSdkIncludesEntry: true,   // roundabout：SDK 播报口径 → dir 换算（O1）
// }
function buildFrame(nav){
  const px = (PLANE.carY - PLANE.yFar) / Math.max(1, nav.visMeters || 140);
  const base = {
    heading: headingNorm(nav.heading || 0),
    turn_dist: Math.round(nav.turn_dist || 0),
    hint: nav.hint || '',
    total_dist: nav.total_dist || 0,
    progress_pct: nav.progress_pct || 0,
    elapsed_min: nav.elapsed_min || 0,
    eta_time: nav.eta_time || '',
    pos: [PLANE.carX, PLANE.carY - 24]           // 车标（条带模式）
  };
  const m = nav.maneuver || 'straight';
  const entering = nav.turn_dist < 200 && m !== 'straight' && m !== 'uturn';
  if(entering){
    const tm = typeForManeuver(m, nav.junction);
    const road = {type: tm.type, half: 62};
    if(tm.dir) road.dir = tm.dir;
    if(m === 'roundabout'){
      const exits = (nav.rbtExits && nav.rbtExits.length) ? nav.rbtExits : ['W', 'N', 'E'];
      const dirN = sdkExitToDir(nav.rbtSdkExit !== undefined ? nav.rbtSdkExit : 1, !!nav.rbtSdkIncludesEntry);
      road.exits = exits;
      road.dir = 'exit' + dirN;
      road.pts = roundaboutPts(exits, dirN);   // 完整绿路径（含方向弧），渲染端优先采用
    } else {
      road.pts = decimate(screenize(nav.pathM || [[0, 8]], px), 16);
    }
    base.road = road;
    base.centerLine = road.pts;
  } else {
    const c = screenize(nav.pathM || [[0, 4], [0, 60], [0, 130]], px);
    base.centerLine = decimate(c, 6);
    base.pastCenter = [base.centerLine[0]];
    base.routeCenter = base.centerLine.slice(1);
  }
  return base;
}

function sampleFrame(){
  return {
    msg_type: 'NAV_FRAME',
    payload: buildFrame({
      heading: 90, turn_dist: 120, hint: '前方120米路口右转',
      total_dist: 11400, progress_pct: 26, elapsed_min: 19, eta_time: '15:11',
      maneuver: 'turn_right', junction: 'cross',
      pathM: [[0, 4], [0, 40], [0, 80], [0, 130], [30, 130], [60, 130]]
    })
  };
}

module.exports = {
  PLANE: PLANE, AZM: AZM,
  clamp: clamp, rad: rad, screenize: screenize, decimate: decimate, headingNorm: headingNorm,
  typeForManeuver: typeForManeuver, sdkExitToDir: sdkExitToDir, roundaboutPts: roundaboutPts,
  buildFrame: buildFrame, sampleFrame: sampleFrame
};

if(require.main === module){
  console.log(JSON.stringify(sampleFrame(), null, 2));
}
