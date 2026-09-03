/**
 * test_road_gen.js — road_gen.js 结构断言（node test_road_gen.js，全绿为通过）
 * 覆盖：类型映射 / 出口换算(含与不含入口) / roundabout 弧目标方位 / 抽稀≤16 / 坐标界 / 直行无 road / 帧结构
 */
'use strict';
const assert = require('assert');
const G = require('./road_gen.js');

let n = 0;
function ok(name, fn){
  fn();
  n++;
  console.log('  PASS', name);
}

console.log('== 1) 机动→模板类型映射（协议 type×dir 组合表）==');
ok('turn_right+cross → cross/right', function(){
  assert.deepStrictEqual(G.typeForManeuver('turn_right', 'cross'), {type:'cross', dir:'right'});
});
ok('turn_left+tjunc → tjunc/left', function(){
  assert.deepStrictEqual(G.typeForManeuver('turn_left', 'tjunc'), {type:'tjunc', dir:'left'});
});
ok('fork/curve 映射', function(){
  assert.deepStrictEqual(G.typeForManeuver('fork_right', 'cross'), {type:'fork', dir:'right'});
  assert.deepStrictEqual(G.typeForManeuver('curve', 'cross'), {type:'curve', dir:'left'});
});
ok('straight/uturn → null（不套模板）', function(){
  assert.strictEqual(G.typeForManeuver('straight'), null);
  assert.strictEqual(G.typeForManeuver('uturn'), null);
});
ok('roundabout 映射且 dir 待填', function(){
  assert.deepStrictEqual(G.typeForManeuver('roundabout', 'cross'), {type:'roundabout', dir:null});
});

console.log('== 2) roundabout 出口计数换算（O1，SDK 口径→几何口径）==');
ok('SDK 含入口：第2出口→exit1', function(){ assert.strictEqual(G.sdkExitToDir(2, true), 1); });
ok('SDK 不含入口：第1出口→exit1', function(){ assert.strictEqual(G.sdkExitToDir(1, false), 1); });
ok('SDK 含入口：第3出口→exit2', function(){ assert.strictEqual(G.sdkExitToDir(3, true), 2); });
ok('非法编号抛错', function(){
  assert.throws(function(){ G.sdkExitToDir(0, false); }, /出口编号非法/);
});

console.log('== 3) roundabout 绿路径弧（方向弧，目标方位正确/点数/坐标界）==');
function nearEnd(pts, x, y, tol){
  const e = pts[pts.length - 1];
  return Math.abs(e[0]-x) <= tol && Math.abs(e[1]-y) <= tol;
}
ok('exits[W,N,E] 目标第2(=N) → 弧终点≈环顶(160, cy-b≈61)', function(){
  const pts = G.roundaboutPts(['W','N','E'], 2);
  assert.ok(pts.length <= 16, 'pts<=16, got ' + pts.length);
  assert.ok(nearEnd(pts, 160, 61, 6), 'end=' + JSON.stringify(pts[pts.length-1]));
});
ok('目标第1(=W) → 终点≈(cx-r≈102, 84)', function(){
  const pts = G.roundaboutPts(['W','N','E'], 1);
  assert.ok(nearEnd(pts, 102, 84, 6), 'end=' + JSON.stringify(pts[pts.length-1]));
});
ok('目标第3(=E) → 终点≈(cx+r≈218, 84)', function(){
  const pts = G.roundaboutPts(['W','N','E'], 3);
  assert.ok(nearEnd(pts, 218, 84, 6), 'end=' + JSON.stringify(pts[pts.length-1]));
});
ok('全部点坐标界内(0..319 / 20..239)', function(){
  for(const d of [1,2,3]){
    for(const p of G.roundaboutPts(['W','N','E'], d)){
      assert.ok(p[0] >= 0 && p[0] <= 319 && p[1] >= 20 && p[1] <= 239, JSON.stringify(p));
    }
  }
});

console.log('== 4) buildFrame 帧组装 ==');
ok('直行(>200m/straight) → 无 road、centerLine 存在', function(){
  const f = G.buildFrame({heading: 0, turn_dist: 500, maneuver: 'straight', pathM: [[0,4],[0,60],[0,120]]});
  assert.strictEqual(f.road, undefined);
  assert.ok(Array.isArray(f.centerLine) && f.centerLine.length >= 2);
});
ok('右转进入(<200m) → road{cross,right} + pts≤16 界内', function(){
  const f = G.buildFrame({heading: 90, turn_dist: 120, maneuver: 'turn_right', pathM: [[0,4],[0,80],[0,130],[40,130],[90,130]]});
  assert.strictEqual(f.road.type, 'cross');
  assert.strictEqual(f.road.dir, 'right');
  assert.ok(f.road.pts.length <= 16);
  for(const p of f.road.pts){ assert.ok(p[0] >= 0 && p[0] <= 319 && p[1] >= 20 && p[1] <= 239); }
});
ok('环岛进入：SDK 含入口第3出口 → dir=exit2 且 exits 传回', function(){
  const f = G.buildFrame({heading: 0, turn_dist: 150, maneuver: 'roundabout', rbtExits: ['W','N','E'], rbtSdkExit: 3, rbtSdkIncludesEntry: true});
  assert.strictEqual(f.road.type, 'roundabout');
  assert.strictEqual(f.road.dir, 'exit2');
  assert.deepStrictEqual(f.road.exits, ['W','N','E']);
  assert.ok(f.road.pts.length <= 16);
});
ok('超长路线抽稀 ≤16', function(){
  const long = [];
  for(let i = 0; i < 40; i++) long.push([0, i * 4]);
  const f = G.buildFrame({heading: 0, turn_dist: 120, maneuver: 'turn_left', pathM: long});
  assert.ok(f.road.pts.length <= 16, 'pts=' + f.road.pts.length);
});
ok('示例帧结构 msg_type/payload', function(){
  const sf = G.sampleFrame();
  assert.strictEqual(sf.msg_type, 'NAV_FRAME');
  assert.ok(sf.payload && sf.payload.road && sf.payload.road.type === 'cross');
});

console.log('\nALL PASS (' + n + ' checks)');
