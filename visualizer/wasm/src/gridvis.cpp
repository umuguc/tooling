#define _USE_MATH_DEFINES
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <emscripten.h>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct GridIndex {
  int x, y;
};
using P3f = std::array<float, 3>;

struct FlatGrid {
  std::vector<float> pos;
  std::vector<float> col_tile;
  std::vector<float> col_sf;

  size_t total = 0;
  size_t nx = 0, ny = 0;

  float xmin = 1e30f, ymin = 1e30f, zmin = 1e30f;
  float xmax = -1e30f, ymax = -1e30f, zmax = -1e30f;
};

P3f hue_color(size_t i, size_t total) {
  double h = (total > 0) ? (double)i / (double)total : 0.0;
  return {(float)((std::sin(h * 2 * M_PI) + 1) * 0.5),
          (float)((std::sin((h + 1.0 / 3) * 2 * M_PI) + 1) * 0.5),
          (float)((std::sin((h + 2.0 / 3) * 2 * M_PI) + 1) * 0.5)};
}

bool read_grid_flat(const std::string &path, FlatGrid &g, bool is_sf,
                    const FlatGrid *tiles_for_sf_bounds = nullptr) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;

  size_t total = 0;
  int max_x = -1, max_y = -1;
  {
    while (true) {
      GridIndex idx;
      if (!f.read(reinterpret_cast<char *>(&idx), sizeof(idx)))
        break;
      int count;
      if (!f.read(reinterpret_cast<char *>(&count), sizeof(count)))
        break;
      if (idx.x < 0 || idx.y < 0 || count < 0 || count > 10'000'000)
        break;
      total += (size_t)count;
      if (idx.x > max_x)
        max_x = idx.x;
      if (idx.y > max_y)
        max_y = idx.y;

      f.seekg((std::streamoff)count * (std::streamoff)(sizeof(double) * 3),
              std::ios::cur);
      if (!f)
        break;
    }
  }

  if (total == 0)
    return true;

  g.total = total;
  g.nx = (size_t)(max_x + 1);
  g.ny = (size_t)(max_y + 1);

  if (!is_sf) {
    g.pos.resize(total * 3);
    g.col_tile.resize(total * 3);
    g.col_sf.resize(total * 3, 0.4f);
  } else {
    g.col_sf.resize(total * 3, 0.4f);
  }

  size_t num_tiles = g.nx * g.ny;
  std::vector<P3f> palette(num_tiles);
  for (size_t i = 0; i < num_tiles; i++)
    palette[i] = hue_color(i, num_tiles);

  f.clear();
  f.seekg(0, std::ios::beg);
  size_t gi = 0;

  while (true) {
    GridIndex idx;
    if (!f.read(reinterpret_cast<char *>(&idx), sizeof(idx)))
      break;
    int count;
    if (!f.read(reinterpret_cast<char *>(&count), sizeof(count)))
      break;
    if (idx.x < 0 || idx.y < 0 || count < 0 || count > 10'000'000)
      break;

    size_t tile_flat = (size_t)idx.x * g.ny + (size_t)idx.y;
    P3f tc = (tile_flat < palette.size()) ? palette[tile_flat]
                                          : P3f{0.5f, 0.5f, 0.5f};

    for (int k = 0; k < count && gi < total; k++, gi++) {
      double buf[3];
      if (!f.read(reinterpret_cast<char *>(buf), sizeof(double) * 3))
        goto done;

      if (!is_sf) {
        g.pos[gi * 3] = (float)buf[0];
        g.pos[gi * 3 + 1] = (float)buf[1];
        g.pos[gi * 3 + 2] = (float)buf[2];

        if (g.pos[gi * 3] < g.xmin)
          g.xmin = g.pos[gi * 3];
        if (g.pos[gi * 3] > g.xmax)
          g.xmax = g.pos[gi * 3];
        if (g.pos[gi * 3 + 1] < g.ymin)
          g.ymin = g.pos[gi * 3 + 1];
        if (g.pos[gi * 3 + 1] > g.ymax)
          g.ymax = g.pos[gi * 3 + 1];
        if (g.pos[gi * 3 + 2] < g.zmin)
          g.zmin = g.pos[gi * 3 + 2];
        if (g.pos[gi * 3 + 2] > g.zmax)
          g.zmax = g.pos[gi * 3 + 2];

        g.col_tile[gi * 3] = tc[0];
        g.col_tile[gi * 3 + 1] = tc[1];
        g.col_tile[gi * 3 + 2] = tc[2];
      } else {
        // SF: store as color if in [0,1]
        float r = (float)buf[0], gr = (float)buf[1], b = (float)buf[2];
        if (r >= 0 && r <= 1 && gr >= 0 && gr <= 1 && b >= 0 && b <= 1) {
          g.col_sf[gi * 3] = r;
          g.col_sf[gi * 3 + 1] = gr;
          g.col_sf[gi * 3 + 2] = b;
        }
      }
    }
  }
done:
  return true;
}

static const char B64C[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void b64_encode_stream(std::ostringstream &out, const void *data,
                       size_t nbytes) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < nbytes; i += 3) {
    uint32_t v = (uint32_t)p[i] << 16;
    if (i + 1 < nbytes)
      v |= (uint32_t)p[i + 1] << 8;
    if (i + 2 < nbytes)
      v |= (uint32_t)p[i + 2];
    out << B64C[(v >> 18) & 63] << B64C[(v >> 12) & 63]
        << ((i + 1 < nbytes) ? B64C[(v >> 6) & 63] : '=')
        << ((i + 2 < nbytes) ? B64C[v & 63] : '=');
  }
}

std::string generate_html(FlatGrid &tiles, FlatGrid &sf) {
  size_t total = tiles.total;
  if (total == 0)
    return "<html><body "
           "style='background:#0d1117;color:#ff7b72;padding:2em;font-family:"
           "sans-serif'>No points found in file.</body></html>";

  size_t nx = tiles.nx;
  size_t ny = tiles.ny;
  size_t num_tiles = nx * ny;
  bool has_sf = (sf.total > 0 && sf.col_sf.size() == total * 3);

  if (has_sf) {
    tiles.col_sf = std::move(sf.col_sf);
  }

  float cx = (tiles.xmin + tiles.xmax) * 0.5f;
  float cy = (tiles.ymin + tiles.ymax) * 0.5f;
  float cz = (tiles.zmin + tiles.zmax) * 0.5f;
  float scale = std::max({tiles.xmax - tiles.xmin, tiles.ymax - tiles.ymin,
                          tiles.zmax - tiles.zmin, 1e-6f});
  for (size_t i = 0; i < total; i++) {
    tiles.pos[i * 3] = (tiles.pos[i * 3] - cx) / scale * 2.0f;
    tiles.pos[i * 3 + 1] = (tiles.pos[i * 3 + 1] - cy) / scale * 2.0f;
    tiles.pos[i * 3 + 2] = (tiles.pos[i * 3 + 2] - cz) / scale * 2.0f;
  }

  std::ostringstream o;
  o.str().reserve(tiles.pos.size() * sizeof(float) *
                  6); // rough estimate for b64

  o << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>Grid Visualizer</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#0d1117;overflow:hidden;font-family:'Segoe UI',system-ui,-apple-system,sans-serif}
canvas{display:block;width:100vw;height:100vh}
#ui{position:fixed;top:16px;left:16px;display:flex;flex-direction:column;gap:10px;pointer-events:none}
#btn-color{pointer-events:all;background:#1f6feb;color:#e6edf3;border:none;border-radius:8px;padding:8px 16px;font-size:13px;font-weight:500;cursor:pointer;transition:background .2s}
#btn-color:hover{background:#388bfd}
.chip{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:6px 12px;font-size:12px;color:#7d8590;line-height:1.5}
.chip b{color:#e6edf3}
#hint{position:fixed;bottom:16px;left:16px;font-size:11px;color:#3d444d}
</style>
</head>
<body>
<canvas id="c"></canvas>
<div id="ui">
  <button id="btn-color" onclick="toggleColor()">Toggle Colors</button>
)HTML";

  o << "  <div class=\"chip\"><b>" << total << "</b> points &nbsp;·&nbsp; <b>"
    << num_tiles << "</b> tiles (" << nx << "&times;" << ny << ")</div>\n";
  o << "  <div id=\"mode-label\" class=\"chip\">Mode: <b>"
    << (has_sf ? "Shape Factors" : "Tiles") << "</b></div>\n";

  o << R"HTML(</div>
<div id="hint">Drag: rotate &nbsp;|&nbsp; Right-drag / two-finger: pan &nbsp;|&nbsp; Scroll / pinch: zoom</div>
<script>
)HTML";

  o << "const N=" << total << ",HAS_SF=" << (has_sf ? "true" : "false")
    << ";\n";

  o << "const POS_B64='";
  b64_encode_stream(o, tiles.pos.data(), tiles.pos.size() * sizeof(float));
  o << "';\n";
  {
    std::vector<float> tmp;
    tmp.swap(tiles.pos);
  }

  o << "const COL_TILE_B64='";
  b64_encode_stream(o, tiles.col_tile.data(),
                    tiles.col_tile.size() * sizeof(float));
  o << "';\n";
  {
    std::vector<float> tmp;
    tmp.swap(tiles.col_tile);
  }

  o << "const COL_SF_B64='";
  b64_encode_stream(o, tiles.col_sf.data(),
                    tiles.col_sf.size() * sizeof(float));
  o << "';\n";
  {
    std::vector<float> tmp;
    tmp.swap(tiles.col_sf);
  }

  o << R"JS(
function b64ToF32(s) {
  const raw = atob(s);
  const u8  = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) u8[i] = raw.charCodeAt(i);
  return new Float32Array(u8.buffer); // zero-copy view
}

// ── WebGL setup ───────────────────────────────────────────────────────────────
const canvas = document.getElementById('c');
const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
if (!gl) {
  document.body.innerHTML = '<p style="color:#ff7b72;padding:2em;font-family:sans-serif">WebGL not supported in this browser.</p>';
  throw 0;
}

const VSRC = `
attribute vec3 a_pos;
attribute vec3 a_col;
uniform mat4 u_mvp;
varying vec3 v_col;
void main() {
  gl_Position = u_mvp * vec4(a_pos, 1.0);
  gl_PointSize = 2.0;
  v_col = a_col;
}`;

const FSRC = `
precision mediump float;
varying vec3 v_col;
void main() {
  vec2 p = gl_PointCoord - vec2(0.5);
  if (dot(p, p) > 0.25) discard;
  gl_FragColor = vec4(v_col, 1.0);
}`;

function compile(src, type) {
  const s = gl.createShader(type);
  gl.shaderSource(s, src);
  gl.compileShader(s);
  return s;
}

const prog = gl.createProgram();
gl.attachShader(prog, compile(VSRC, gl.VERTEX_SHADER));
gl.attachShader(prog, compile(FSRC, gl.FRAGMENT_SHADER));
gl.linkProgram(prog);
gl.useProgram(prog);

const aPos = gl.getAttribLocation(prog, 'a_pos');
const aCol = gl.getAttribLocation(prog, 'a_col');
const uMvp = gl.getUniformLocation(prog, 'u_mvp');

function makeVbo(b64) {
  const data = b64ToF32(b64);
  const buf  = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  return buf; // JS Float32Array goes out of scope → GC can collect
}

const bufPos  = makeVbo(POS_B64);
const bufTile = makeVbo(COL_TILE_B64);
const bufSf   = makeVbo(COL_SF_B64);

// ── Color mode ────────────────────────────────────────────────────────────────
let colorMode = HAS_SF ? 0 : 1;

function toggleColor() {
  colorMode = HAS_SF ? (colorMode === 0 ? 1 : 0) : 1;
  document.getElementById('mode-label').innerHTML =
    'Mode: <b>' + (colorMode === 0 ? 'Shape Factors' : 'Tiles') + '</b>';
  render();
}

// ── Camera (spherical coordinates) ───────────────────────────────────────────
let theta = 0.5, phi = 1.1, radius = 3.0, panX = 0, panY = 0;

function norm(v) { const l = Math.hypot(...v) || 1; return v.map(x => x/l); }
function cross(a, b) { return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]; }
function dot(a, b)   { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
function sub(a, b)   { return [a[0]-b[0], a[1]-b[1], a[2]-b[2]]; }

function lookAt(eye, target) {
  const up = [0, 1, 0];
  const z = norm(sub(eye, target));
  const x = norm(cross(up, z));
  const y = cross(z, x);
  return new Float32Array([
    x[0], y[0], z[0], 0,
    x[1], y[1], z[1], 0,
    x[2], y[2], z[2], 0,
    -dot(x, eye), -dot(y, eye), -dot(z, eye), 1
  ]);
}

function perspective(fov, aspect, near, far) {
  const f = 1 / Math.tan(fov * 0.5), d = near - far;
  return new Float32Array([
    f/aspect, 0,  0,                    0,
    0,        f,  0,                    0,
    0,        0,  (near+far)/d,        -1,
    0,        0,  2*near*far/d,         0
  ]);
}

function mul4(a, b) {
  const r = new Float32Array(16);
  for (let c = 0; c < 4; c++)
    for (let row = 0; row < 4; row++)
      for (let k = 0; k < 4; k++)
        r[c*4+row] += a[k*4+row] * b[c*4+k];
  return r;
}

function getEye() {
  const sp = Math.sin(phi), cp = Math.cos(phi);
  return [panX + radius*sp*Math.cos(theta), panY + radius*cp, radius*sp*Math.sin(theta)];
}

// ── Render ────────────────────────────────────────────────────────────────────
function render() {
  const w = canvas.clientWidth  * devicePixelRatio | 0;
  const h = canvas.clientHeight * devicePixelRatio | 0;
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  gl.viewport(0, 0, w, h);
  gl.clearColor(0.051, 0.067, 0.09, 1);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.enable(gl.DEPTH_TEST);

  const eye    = getEye();
  const target = [panX, panY, 0];
  const V   = lookAt(eye, target);
  const P   = perspective(Math.PI / 3, w / h, 0.01, 100);
  const MVP = mul4(P, V);

  gl.uniformMatrix4fv(uMvp, false, MVP);

  gl.bindBuffer(gl.ARRAY_BUFFER, bufPos);
  gl.enableVertexAttribArray(aPos);
  gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);

  gl.bindBuffer(gl.ARRAY_BUFFER, colorMode === 0 ? bufSf : bufTile);
  gl.enableVertexAttribArray(aCol);
  gl.vertexAttribPointer(aCol, 3, gl.FLOAT, false, 0, 0);

  gl.drawArrays(gl.POINTS, 0, N);
}

// ── Mouse interaction ─────────────────────────────────────────────────────────
let drag = null;
canvas.addEventListener('mousedown', e => {
  drag = { x: e.clientX, y: e.clientY, btn: e.button, theta, phi, panX, panY };
  e.preventDefault();
});
canvas.addEventListener('contextmenu', e => e.preventDefault());
window.addEventListener('mousemove', e => {
  if (!drag) return;
  const dx = (e.clientX - drag.x) * 0.005;
  const dy = (e.clientY - drag.y) * 0.005;
  if (drag.btn === 0) {
    theta = drag.theta - dx;
    phi   = Math.max(0.05, Math.min(Math.PI - 0.05, drag.phi + dy));
  } else {
    panX = drag.panX - dx * radius;
    panY = drag.panY + dy * radius;
  }
  render();
});
window.addEventListener('mouseup', () => drag = null);
canvas.addEventListener('wheel', e => {
  radius = Math.max(0.1, radius * (1 + e.deltaY * 0.001));
  render();
  e.preventDefault();
}, { passive: false });

// ── Touch interaction ─────────────────────────────────────────────────────────
let lastTouches = null;
canvas.addEventListener('touchstart', e => {
  lastTouches = Array.from(e.touches).map(t => ({ x: t.clientX, y: t.clientY }));
  e.preventDefault();
}, { passive: false });
canvas.addEventListener('touchmove', e => {
  const ts = Array.from(e.touches).map(t => ({ x: t.clientX, y: t.clientY }));
  if (ts.length === 1 && lastTouches && lastTouches.length === 1) {
    theta -= (ts[0].x - lastTouches[0].x) * 0.005;
    phi = Math.max(0.05, Math.min(Math.PI - 0.05, phi + (ts[0].y - lastTouches[0].y) * 0.005));
  } else if (ts.length === 2 && lastTouches && lastTouches.length === 2) {
    const d0 = Math.hypot(lastTouches[0].x - lastTouches[1].x, lastTouches[0].y - lastTouches[1].y);
    const d1 = Math.hypot(ts[0].x - ts[1].x, ts[0].y - ts[1].y);
    if (d0 > 0) radius = Math.max(0.1, radius * d0 / d1);
  }
  lastTouches = ts;
  render();
  e.preventDefault();
}, { passive: false });

window.addEventListener('resize', render);
render();
)JS";

  o << "</script>\n</body>\n</html>";
  return o.str();
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
char *visualize(const char *tiles_path, const char *sf_path) {
  try {
    FlatGrid tiles, sf_grid;

    read_grid_flat(tiles_path ? tiles_path : "", tiles, false);

    if (sf_path && sf_path[0] != '\0')
      read_grid_flat(sf_path, sf_grid, true);

    std::string html = generate_html(tiles, sf_grid);
    char *r = static_cast<char *>(std::malloc(html.size() + 1));
    if (!r)
      throw std::bad_alloc();
    std::memcpy(r, html.c_str(), html.size() + 1);
    return r;
  } catch (const std::exception &ex) {
    std::string msg = "<html><body "
                      "style='background:#0d1117;color:#ff7b72;padding:2em;"
                      "font-family:sans-serif'>Error: ";
    msg += ex.what();
    msg += "</body></html>";
    char *r = static_cast<char *>(std::malloc(msg.size() + 1));
    if (r)
      std::memcpy(r, msg.c_str(), msg.size() + 1);
    return r;
  } catch (...) {
    const char *msg = "<html><body "
                      "style='background:#0d1117;color:#ff7b72;padding:2em'>"
                      "Unknown error</body></html>";
    char *r = static_cast<char *>(std::malloc(std::strlen(msg) + 1));
    if (r)
      std::strcpy(r, msg);
    return r;
  }
}

EMSCRIPTEN_KEEPALIVE
void free_result(char *ptr) { std::free(ptr); }
}