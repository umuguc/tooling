#define _USE_MATH_DEFINES
#include <emscripten.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Binary format (matches binary_io.hpp) ─────────────────────────────────────
// Each chunk: GridIndex{int x, int y} + int count + count * (3 doubles)

struct GridIndex { int x, y; };
using P3f = std::array<float, 3>;

std::vector<std::vector<std::vector<P3f>>>
read_grid(const std::string& path) {
    std::vector<std::vector<std::vector<P3f>>> grid;
    std::ifstream f(path, std::ios::binary);
    if (!f) return grid;

    bool ok = true;
    while (ok) {
        GridIndex idx;
        if (!f.read(reinterpret_cast<char*>(&idx), sizeof(idx))) break;
        int count;
        if (!f.read(reinterpret_cast<char*>(&count), sizeof(count))) break;
        if (idx.x < 0 || idx.y < 0 || count < 0 || count > 10'000'000) break;

        size_t xi = (size_t)idx.x, yi = (size_t)idx.y;
        if (xi >= grid.size())      grid.resize(xi + 1);
        if (yi >= grid[xi].size())  grid[xi].resize(yi + 1);

        for (int k = 0; k < count; k++) {
            double buf[3];
            if (!f.read(reinterpret_cast<char*>(buf), sizeof(double) * 3)) { ok = false; break; }
            grid[xi][yi].push_back({(float)buf[0], (float)buf[1], (float)buf[2]});
        }
    }
    return grid;
}

// ── Color utilities ───────────────────────────────────────────────────────────

P3f hue_color(size_t i, size_t total) {
    double h = (total > 0) ? (double)i / (double)total : 0.0;
    return {
        (float)((std::sin(h * 2 * M_PI) + 1) * 0.5),
        (float)((std::sin((h + 1.0/3) * 2 * M_PI) + 1) * 0.5),
        (float)((std::sin((h + 2.0/3) * 2 * M_PI) + 1) * 0.5)
    };
}

// ── Base64 ────────────────────────────────────────────────────────────────────

static const char B64C[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64_encode(const void* data, size_t nbytes) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    std::string r;
    r.reserve(((nbytes + 2) / 3) * 4);
    for (size_t i = 0; i < nbytes; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16;
        if (i + 1 < nbytes) v |= (uint32_t)p[i + 1] << 8;
        if (i + 2 < nbytes) v |= (uint32_t)p[i + 2];
        r += B64C[(v >> 18) & 63];
        r += B64C[(v >> 12) & 63];
        r += (i + 1 < nbytes) ? B64C[(v >>  6) & 63] : '=';
        r += (i + 2 < nbytes) ? B64C[ v         & 63] : '=';
    }
    return r;
}

// ── HTML generation ───────────────────────────────────────────────────────────

std::string generate_html(
    const std::vector<std::vector<std::vector<P3f>>>& tiles,
    const std::vector<std::vector<std::vector<P3f>>>& sf)
{
    // Count total points
    size_t total = 0;
    for (auto& row : tiles) for (auto& cell : row) total += cell.size();
    if (total == 0)
        return "<html><body style='background:#0d1117;color:#ff7b72;padding:2em;font-family:sans-serif'>No points found in file.</body></html>";

    size_t nx       = tiles.size();
    size_t ny       = (nx > 0) ? tiles[0].size() : 0;
    size_t num_tiles = nx * ny;
    bool   has_sf   = !sf.empty();

    // Tile color palette
    std::vector<P3f> palette(num_tiles);
    for (size_t i = 0; i < num_tiles; i++) palette[i] = hue_color(i, num_tiles);

    // Flatten point data into parallel arrays
    std::vector<float> pos(total * 3);
    std::vector<float> col_tile(total * 3);
    std::vector<float> col_sf(total * 3);

    float xmin =  1e30f, ymin =  1e30f, zmin =  1e30f;
    float xmax = -1e30f, ymax = -1e30f, zmax = -1e30f;

    size_t gi = 0;
    for (size_t i = 0; i < tiles.size(); i++) {
        for (size_t j = 0; j < tiles[i].size(); j++) {
            size_t tile_idx = (ny > 0) ? (i * ny + j) % num_tiles : 0;
            P3f tc = palette[tile_idx];

            for (size_t k = 0; k < tiles[i][j].size(); k++) {
                const auto& p = tiles[i][j][k];
                pos[gi*3]   = p[0];
                pos[gi*3+1] = p[1];
                pos[gi*3+2] = p[2];
                xmin = std::min(xmin, p[0]); xmax = std::max(xmax, p[0]);
                ymin = std::min(ymin, p[1]); ymax = std::max(ymax, p[1]);
                zmin = std::min(zmin, p[2]); zmax = std::max(zmax, p[2]);

                col_tile[gi*3]   = tc[0];
                col_tile[gi*3+1] = tc[1];
                col_tile[gi*3+2] = tc[2];

                P3f sfc = {0.4f, 0.4f, 0.4f};
                if (has_sf && i < sf.size() && j < sf[i].size() && k < sf[i][j].size()) {
                    const auto& sp = sf[i][j][k];
                    if (sp[0] >= 0 && sp[0] <= 1 &&
                        sp[1] >= 0 && sp[1] <= 1 &&
                        sp[2] >= 0 && sp[2] <= 1)
                        sfc = sp;
                }
                col_sf[gi*3]   = sfc[0];
                col_sf[gi*3+1] = sfc[1];
                col_sf[gi*3+2] = sfc[2];

                gi++;
            }
        }
    }

    // Normalize positions to [-1, 1]
    float cx = (xmin + xmax) * 0.5f, cy = (ymin + ymax) * 0.5f, cz = (zmin + zmax) * 0.5f;
    float scale = std::max({xmax - xmin, ymax - ymin, zmax - zmin, 1e-6f});
    for (size_t i = 0; i < total; i++) {
        pos[i*3]   = (pos[i*3]   - cx) / scale * 2.0f;
        pos[i*3+1] = (pos[i*3+1] - cy) / scale * 2.0f;
        pos[i*3+2] = (pos[i*3+2] - cz) / scale * 2.0f;
    }

    // Encode as base64 Float32Arrays
    std::string b64_pos  = b64_encode(pos.data(),      pos.size()      * sizeof(float));
    std::string b64_tile = b64_encode(col_tile.data(), col_tile.size() * sizeof(float));
    std::string b64_sf   = b64_encode(col_sf.data(),   col_sf.size()   * sizeof(float));

    std::ostringstream o;

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

    o << "const N=" << total << ",HAS_SF=" << (has_sf ? "true" : "false") << ";\n";
    o << "function b64f(s){"
         "const b=atob(s),u=new Uint8Array(b.length);"
         "for(let i=0;i<b.length;i++)u[i]=b.charCodeAt(i);"
         "return new Float32Array(u.buffer);}\n";
    o << "const POS=b64f('"      << b64_pos  << "');\n";
    o << "const COL_TILE=b64f('" << b64_tile << "');\n";
    o << "const COL_SF=b64f('"   << b64_sf   << "');\n";

    o << R"JS(
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

function makeVbo(data) {
  const b = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, b);
  gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  return b;
}

const bufPos  = makeVbo(POS);
const bufTile = makeVbo(COL_TILE);
const bufSf   = makeVbo(COL_SF);

// ── Color mode ────────────────────────────────────────────────────────────────
// 0 = shape factors (if available), 1 = tile colors
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

// ── Emscripten exports ────────────────────────────────────────────────────────

extern "C" {

// Reads tiles_path (required) and sf_path (optional, pass "" to skip) from the
// Emscripten virtual FS and returns a malloc'd self-contained HTML string.
// Caller must free with free_result().
EMSCRIPTEN_KEEPALIVE
char* visualize(const char* tiles_path, const char* sf_path) {
    try {
        auto tiles = read_grid(tiles_path ? tiles_path : "");
        std::vector<std::vector<std::vector<P3f>>> sf;
        if (sf_path && sf_path[0] != '\0')
            sf = read_grid(sf_path);

        std::string html = generate_html(tiles, sf);
        char* r = static_cast<char*>(std::malloc(html.size() + 1));
        if (!r) throw std::bad_alloc();
        std::memcpy(r, html.c_str(), html.size() + 1);
        return r;
    } catch (const std::exception& ex) {
        std::string msg = "<html><body style='background:#0d1117;color:#ff7b72;padding:2em;font-family:sans-serif'>Error: ";
        msg += ex.what();
        msg += "</body></html>";
        char* r = static_cast<char*>(std::malloc(msg.size() + 1));
        if (r) std::memcpy(r, msg.c_str(), msg.size() + 1);
        return r;
    } catch (...) {
        const char* msg = "<html><body style='background:#0d1117;color:#ff7b72;padding:2em'>Unknown error</body></html>";
        char* r = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
        if (r) std::strcpy(r, msg);
        return r;
    }
}

EMSCRIPTEN_KEEPALIVE
void free_result(char* ptr) {
    std::free(ptr);
}

} // extern "C"
