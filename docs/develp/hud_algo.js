/**
 * hud_algo.js — EspNav HUD 底图处理算法（可复用、无 DOM 依赖）
 *
 * 用途：把"手机地图截图"处理成"在分光镜 HUD 上尽量少遮挡真实环境"的底图。
 *
 * 设计依据（物理层）：
 *   LCD 的黑色像素 = 液晶挡住背光 → **不发光**；分光镜处那个位置只有真实光透过，完全不遮挡。
 *   所以"遮挡"的本质是 **发光面积 + 发光强度**：
 *     · 减少**发光面积** → 不发光的地方彻底透明（最有效）
 *     · 降低**发光强度** → 只能整体压暗，真实光也被一起压暗、久看疲劳（最钝的刀）
 *   本文件提供的两类算法对应"减少面积"这条路：
 *     A2 sobelEdges —— 只画轮廓（黑底 + 细线）
 *     A3 dotGrid    —— 稀疏点阵（只让 1/gap² 的像素发光）
 *
 * 坐标系约定：所有函数都在 **320×240** 的 ImageData 上工作（与 ESP 屏一致）。
 *
 * 浏览器用法：<script src="hud_algo.js"></script> → 全局 window.HudAlgo
 * Node 用法：const HudAlgo = require('./hud_algo.js')
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.HudAlgo = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const W = 320, H = 240;

  /* ============================================================
   * 基础工具
   * ============================================================ */

  /** 亮度（BT.601），返回 Float32Array（每像素一个值，0-255） */
  function luma(data) {
    const n = data.length >> 2;
    const out = new Float32Array(n);
    for (let i = 0, j = 0; i < data.length; i += 4, j++) {
      out[j] = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
    }
    return out;
  }

  /** 新建一张"全黑但完全不发光"的画布数据（RGB=0） */
  function blackCanvas() {
    const d = new Uint8ClampedArray(W * H * 4);
    for (let i = 3; i < d.length; i += 4) d[i] = 255;   // alpha 不透明
    return d;
  }

  /**
   * 预处理：把底图压成"灰度 + 可调对比 + 可调亮度"，
   * 这样边缘检测与点阵都能在干净的信号上工作。
   */
  function preprocess(src, opt) {
    opt = opt || {};
    const bright = opt.bright != null ? opt.bright : 1.0;     // 亮度相乘
    const contrast = opt.contrast != null ? opt.contrast : 1.0;
    const sat = opt.sat != null ? opt.sat : 1.0;
    const invert = !!opt.invert;                             // 反相（浅色地图给深色线）

    const out = new Uint8ClampedArray(src.length);
    for (let i = 0; i < src.length; i += 4) {
      let r = src[i], g = src[i + 1], b = src[i + 2];
      const gray = 0.299 * r + 0.587 * g + 0.114 * b;
      r = gray + (r - gray) * sat; g = gray + (g - gray) * sat; b = gray + (b - gray) * sat;
      r = (r - 128) * contrast + 128; g = (g - 128) * contrast + 128; b = (b - 128) * contrast + 128;
      r *= bright; g *= bright; b *= bright;
      if (invert) { r = 255 - r; g = 255 - g; b = 255 - b; }
      out[i] = r; out[i + 1] = g; out[i + 2] = b; out[i + 3] = 255;
    }
    return out;
  }

  /* ============================================================
   * A2：Sobel 轮廓 —— 黑底 + 细线，只有边缘发光
   * ============================================================ */

  /**
   * @param {ImageData|{data:Uint8ClampedArray}} src
   * @param {object} opt
   *   thr      {number} 边缘强度阈值（越大线越少）   默认 60
   *   lineW    {number} 线宽（1=单像素）            默认 1
   *   color    {number[]} 线的颜色 [r,g,b]          默认 [80,255,120] 绿
   *   bgBright {number} 背景亮度 0-255（0=完全不发光）默认 0
   *   minGap   {number} 线条去密：若某点邻域内已画过线则跳过（0=不启用）默认 0
   *   pre      {object} 传给 preprocess 的参数
   * @returns {ImageData}  （Node 下没有 ImageData，返回 {data,width,height} 形状对象）
   */
  function sobelEdges(src, opt) {
    opt = opt || {};
    const thr = opt.thr != null ? opt.thr : 60;
    const lineW = Math.max(1, opt.lineW || 1);
    const color = opt.color || [80, 255, 120];
    const bgBright = Math.max(0, Math.min(255, opt.bgBright != null ? opt.bgBright : 0));
    const pre = preprocess(src.data, opt.pre || {});

    const L = luma(pre);
    const at = (x, y) => L[y * W + x];

    // ① 计算梯度幅值
    const mag = new Float32Array(W * H);
    for (let y = 1; y < H - 1; y++) {
      for (let x = 1; x < W - 1; x++) {
        const p00 = at(x - 1, y - 1), p10 = at(x, y - 1), p20 = at(x + 1, y - 1);
        const p01 = at(x - 1, y), p21 = at(x + 1, y);
        const p02 = at(x - 1, y + 1), p12 = at(x, y + 1), p22 = at(x + 1, y + 1);
        const gx = (p20 + 2 * p21 + p22) - (p00 + 2 * p01 + p02);
        const gy = (p02 + 2 * p12 + p22) - (p00 + 2 * p10 + p20);
        mag[y * W + x] = Math.sqrt(gx * gx + gy * gy);
      }
    }

    // ② 生成结果：整张先填背景亮度（0 = 完全不发光）
    const out = blackCanvas();
    if (bgBright > 0) {
      for (let i = 0; i < out.length; i += 4) {
        out[i] = out[i + 1] = out[i + 2] = bgBright;
      }
    }

    // ③ 画线（用 Uint8Array 记录已画过的位置，便于 minGap 去密）
    const painted = new Uint8Array(W * H);
    const half = lineW >> 1;
    const minGap = opt.minGap || 0;
    const gap2 = minGap * minGap;

    for (let y = 1; y < H - 1; y++) {
      for (let x = 1; x < W - 1; x++) {
        if (mag[y * W + x] < thr) continue;

        // minGap：邻域内已有线条则跳过（把密集边缘变稀疏）
        if (minGap > 0) {
          let tooClose = false;
          for (let dy = -minGap; dy <= minGap && !tooClose; dy++) {
            for (let dx = -minGap; dx <= minGap; dx++) {
              if (dx * dx + dy * dy > gap2) continue;
              const yy = y + dy, xx = x + dx;
              if (yy < 0 || yy >= H || xx < 0 || xx >= W) continue;
              if (painted[yy * W + xx]) { tooClose = true; break; }
            }
          }
          if (tooClose) continue;
        }

        for (let dy = -half; dy <= half; dy++) {
          for (let dx = -half; dx <= half; dx++) {
            const yy = y + dy, xx = x + dx;
            if (yy < 0 || yy >= H || xx < 0 || xx >= W) continue;
            const i = (yy * W + xx) << 2;
            out[i] = color[0]; out[i + 1] = color[1]; out[i + 2] = color[2];
            painted[yy * W + xx] = 1;
          }
        }
      }
    }
    return wrap(out);
  }

  /* ============================================================
   * A3：稀疏点阵 —— 只让 1/gap² 的像素发光，形状仍在但发光面积骤降
   * ============================================================ */

  /**
   * @param {object} opt
   *   gap      {number} 网格间距（2-10，越大越稀疏）
   *   minLum   {number} 亮度下限 0-255：低于此值的采样点直接丢弃（保留有信息的点）
   *   dotSize  {number} 每个采样点画多大（1-3，用方块）
   *   color    {number[]} 点阵颜色（默认取原图颜色；给了则强制单色）
   *   bgBright {number} 背景亮度（0=完全不发光）
   *   pre      {object} 预处理参数
   */
  function dotGrid(src, opt) {
    opt = opt || {};
    const gap = Math.max(2, opt.gap || 4);
    const minLum = opt.minLum != null ? opt.minLum : 40;
    const dotSize = Math.max(1, Math.min(gap, opt.dotSize || gap));
    const color = opt.color || null;
    const bgBright = Math.max(0, Math.min(255, opt.bgBright != null ? opt.bgBright : 0));
    const pre = preprocess(src.data, opt.pre || {});

    const out = blackCanvas();
    if (bgBright > 0) {
      for (let i = 0; i < out.length; i += 4) {
        out[i] = out[i + 1] = out[i + 2] = bgBright;
      }
    }

    for (let y = 0; y < H; y += gap) {
      for (let x = 0; x < W; x += gap) {
        const i = (y * W + x) << 2;
        const lum = 0.299 * pre[i] + 0.587 * pre[i + 1] + 0.114 * pre[i + 2];
        if (lum < minLum) continue;                       // 无信息的点不画（进一步减少发光）

        let r, g, b;
        if (color) {
          // 单色模式：按亮度调制，暗的地方也暗
          const k = Math.min(1, lum / 200);
          r = color[0] * k; g = color[1] * k; b = color[2] * k;
        } else {
          r = pre[i]; g = pre[i + 1]; b = pre[i + 2];
        }

        const off = (dotSize - 1) >> 1;
        for (let dy = -off; dy <= dotSize - 1 - off; dy++) {
          for (let dx = -off; dx <= dotSize - 1 - off; dx++) {
            const yy = y + dy, xx = x + dx;
            if (yy < 0 || yy >= H || xx < 0 || xx >= W) continue;
            const j = (yy * W + xx) << 2;
            out[j] = r; out[j + 1] = g; out[j + 2] = b;
          }
        }
      }
    }
    return wrap(out);
  }

  /* ============================================================
   * 分光镜叠加（物理正确的加法模型）
   *   result = 真实 × T + (屏幕 × (1-黑位漏光) + 255 × 黑位漏光) × R
   * 屏幕是自发光体，与透过的真实光是**相加**关系。
   * ============================================================ */
  function compositeAdd(real, screen, opt) {
    opt = opt || {};
    const T = opt.T != null ? opt.T : 0.70;         // 分光镜透过率
    const R = opt.R != null ? opt.R : 0.30;         // 反射率
    const black = opt.black != null ? opt.black : 0.12;  // LCD 黑位漏光（IPS 无法纯黑）

    const o = new Uint8ClampedArray(real.data.length);
    for (let i = 0; i < o.length; i += 4) {
      for (let c = 0; c < 3; c++) {
        const re = real.data[i + c] * T;
        const sc = (screen.data[i + c] * (1 - black) + 255 * black) * R;
        o[i + c] = Math.min(255, re + sc);
      }
      o[i + 3] = 255;
    }
    return wrap(o);
  }

  /* ============================================================
   * 定量指标（判定"遮挡程度"用，不靠肉眼）
   * ============================================================ */

  /** 屏幕侧统计：发光像素占比 + 平均发光量（两者都越小越好） */
  function screenStats(img) {
    const d = img.data;
    let lit = 0, sum = 0, n = d.length >> 2;
    for (let i = 0; i < d.length; i += 4) {
      const m = Math.max(d[i], d[i + 1], d[i + 2]);   // 该像素的发光量
      if (m > 8) lit++;
      sum += m;
    }
    return { litRatio: lit / n, meanLum: sum / n };
  }

  /** RMS 对比度（越大越"有细节"）；用亮度通道计算 */
  function contrast(img) {
    const L = luma(img.data);
    let s = 0;
    for (let i = 0; i < L.length; i++) s += L[i];
    const mean = s / L.length;
    let v = 0;
    for (let i = 0; i < L.length; i++) { const t = L[i] - mean; v += t * t; }
    return Math.sqrt(v / L.length);
  }

  /**
   * 相对对比度 = RMS 对比度 / 平均亮度。
   * 对"整体变亮/变暗"不敏感，只反映**细节是否还看得清** —— 这正是人眼/相机自适应的部分。
   */
  function relContrast(img) {
    const L = luma(img.data);
    let s = 0;
    for (let i = 0; i < L.length; i++) s += L[i];
    const mean = s / L.length;
    if (mean < 1) return 0;
    let v = 0;
    for (let i = 0; i < L.length; i++) { const t = L[i] - mean; v += t * t; }
    return Math.sqrt(v / L.length) / mean;
  }

  /** 平均亮度 */
  function meanLuma(img) {
    const L = luma(img.data);
    let s = 0;
    for (let i = 0; i < L.length; i++) s += L[i];
    return s / L.length;
  }

  /**
   * 真实环境「细节可见度」（判定遮挡的最终标准）。
   *
   * 原理：屏幕上叠加的光（发光像素 + LCD 黑位漏光）都是**抬高基线**，
   * 而真实景物本身的起伏幅度几乎不变。所以细节能不能看清，取决于
   *   **真实信号的相对幅度 ÷ 干扰基线**，即
   *
   *       可见度 = (原真实图平均亮度 × T) ÷ 混合图平均亮度
   *
   *   · 乘 T 是归一化掉"分光镜透过率导致的整体变暗"（人眼会适应，不算冲淡）
   *   · 分母里的"混合图平均亮度"就是干扰基线（屏幕越亮 → 越大 → 可见度越低）
   *
   * 判定口径：
   *   ≈1.0  → 真实细节几乎没损失（屏幕全黑且无漏光时正好是 1.0）
   *   <1.0  → 被冲淡，越低越看不清真实世界
   *
   * ⚠ 为什么不用"对比度"：屏幕图案**自带结构**，用总对比度会把它算成"更清晰"，
   *   导致"发光更多反而分数更高"的错误结论（实测验证过）。用亮度基线则单调正确。
   */
  function visibility(real, mixed, T) {
    const mr = meanLuma(real);
    const mm = meanLuma(mixed);
    if (mm < 1) return 1;
    const t = T != null ? T : 1;
    return (mr * t) / mm;
  }

  /**
   * 兼容旧名：等价于 visibility(real, mixed, T)。
   * 保留是因为早期文档/工具里用的是 keepRatio 这个名字。
   */
  function keepRatio(real, mixed, T) {
    return visibility(real, mixed, T);
  }

  /* ============================================================
   * 辅助
   * ============================================================ */
  function wrap(data) {
    if (typeof ImageData !== 'undefined') return new ImageData(data, W, H);
    return { data: data, width: W, height: H };
  }

  return {
    W: W, H: H,
    luma: luma,
    preprocess: preprocess,
    wrapImage: wrap,        // 把 Uint8ClampedArray 包成 ImageData（外部做"纯压暗"这类处理时会用到）
    sobelEdges: sobelEdges,
    dotGrid: dotGrid,
    compositeAdd: compositeAdd,
    screenStats: screenStats,
    contrast: contrast,
    relContrast: relContrast,
    meanLuma: meanLuma,
    visibility: visibility,
    keepRatio: keepRatio,
  };
});
