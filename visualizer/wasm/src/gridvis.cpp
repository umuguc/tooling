#define _USE_MATH_DEFINES
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <emscripten.h>
#include <fstream>
#include <limits>
#include <new>
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

// is_sf selects what gets allocated: tiles (is_sf=false) get pos+col_tile;
// shape factors (is_sf=true) get only col_sf. Neither path allocates the
// other's arrays — the old code always allocated a default-grey col_sf for
// tiles even when no shape-factors file was given (and it just got thrown
// away), which wasted a full point-cloud-sized buffer on every load.
bool read_grid_flat(const std::string &path, FlatGrid &g, bool is_sf) {
  // The point-reading pass below issues one small (24-byte) read() per
  // point. Over a WORKERFS-mounted file, every read that misses the
  // stream's internal buffer becomes a FileReaderSync call — with the
  // default ~8KB libstdc++ buffer that's roughly one such call every ~340
  // points, which for tens of millions of points adds up to enough
  // real per-call overhead to make parsing take minutes. A large custom
  // buffer cuts that by three orders of magnitude. Must be set before
  // open() — iobuf must outlive f, hence the declaration order.
  std::vector<char> iobuf(8 * 1024 * 1024); // 8MB
  std::ifstream f;
  f.rdbuf()->pubsetbuf(iobuf.data(), (std::streamsize)iobuf.size());
  f.open(path, std::ios::binary);
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

// Hands a byte range straight to the worker's postMessage queue as a small
// transferable chunk, tagged with which buffer (kind) it belongs to.
//
// The previous design built one giant HTML string with the whole point
// cloud base64-encoded inline (POS_B64/COL_TILE_B64/COL_SF_B64 consts) —
// that inflates the data ~4/3 as text, and generating + returning it as a
// single malloc'd C string meant several full-size copies existed at once
// (the float arrays, the growing base64 text, the returned std::string,
// the final malloc'd buffer). For a large point cloud that multiplied
// memory use well past what the raw data needed. Streaming raw float
// bytes straight to the worker — which then uploads each chunk directly
// into a GPU buffer — avoids the encoding overhead and every one of those
// extra copies; peak memory is bounded by one chunk (64MB) at a time.
EM_JS(void, js_emit_chunk, (int req_id, int kind, const uint8_t *data, int len), {
  var chunk = new Uint8Array(len);
  chunk.set(HEAPU8.subarray(data, data + len));
  postMessage({ id : req_id, chunk : chunk, kind : kind }, [ chunk.buffer ]);
});

// kind: 0 = position, 1 = tile color, 2 = shape-factor color.
static void emit_floats_chunked(int req_id, int kind, const std::vector<float> &v) {
  const uint8_t *data = reinterpret_cast<const uint8_t *>(v.data());
  size_t total_len = v.size() * sizeof(float);
  const size_t CHUNK = 64u * 1024u * 1024u; // 64MB, a multiple of 4 so a float never splits across chunks
  size_t off = 0;
  while (off < total_len) {
    size_t len = std::min(CHUNK, total_len - off);
    js_emit_chunk(req_id, kind, data + off, (int)len);
    off += len;
  }
}

static std::string json_escape(const std::string &s) {
  std::string r;
  for (char c : s) {
    if (c == '"' || c == '\\')
      r += '\\';
    r += c;
  }
  return r;
}

static char *make_result(const std::string &json) {
  char *r = static_cast<char *>(std::malloc(json.size() + 1));
  if (r)
    std::memcpy(r, json.c_str(), json.size() + 1);
  return r;
}

extern "C" {

// Parses tiles_path (and optionally sf_path) and streams the point cloud's
// vertex buffers to the worker via js_emit_chunk (see above), tagged with
// req_id so the worker can route them back to the right pending request.
// Returns a malloc'd JSON string: {"total":N,"nx":..,"ny":..,"hasSf":bool}
// on success, or {"error":"..."} on failure. Caller must free_result() it.
EMSCRIPTEN_KEEPALIVE
char *visualize(const char *tiles_path, const char *sf_path, int req_id) {
  try {
    FlatGrid tiles, sf_grid;
    read_grid_flat(tiles_path ? tiles_path : "", tiles, false);

    if (tiles.total == 0)
      return make_result("{\"error\":\"No points found in file.\"}");

    bool has_sf = false;
    if (sf_path && sf_path[0] != '\0') {
      read_grid_flat(sf_path, sf_grid, true);
      if (sf_grid.col_sf.size() == tiles.total * 3) {
        tiles.col_sf = std::move(sf_grid.col_sf);
        has_sf = true;
      }
    }

    // Normalise positions to roughly [-1,1] around their centroid, same as
    // the old generate_html did, just done once here instead of on the
    // per-file-generated page.
    float cx = (tiles.xmin + tiles.xmax) * 0.5f;
    float cy = (tiles.ymin + tiles.ymax) * 0.5f;
    float cz = (tiles.zmin + tiles.zmax) * 0.5f;
    float scale = std::max({tiles.xmax - tiles.xmin, tiles.ymax - tiles.ymin,
                            tiles.zmax - tiles.zmin, 1e-6f});
    for (size_t i = 0; i < tiles.total; i++) {
      tiles.pos[i * 3] = (tiles.pos[i * 3] - cx) / scale * 2.0f;
      tiles.pos[i * 3 + 1] = (tiles.pos[i * 3 + 1] - cy) / scale * 2.0f;
      tiles.pos[i * 3 + 2] = (tiles.pos[i * 3 + 2] - cz) / scale * 2.0f;
    }

    std::ostringstream meta;
    meta << "{\"total\":" << tiles.total << ",\"nx\":" << tiles.nx
        << ",\"ny\":" << tiles.ny << ",\"hasSf\":" << (has_sf ? "true" : "false")
        << "}";

    emit_floats_chunked(req_id, 0, tiles.pos);
    { std::vector<float> t; t.swap(tiles.pos); }
    emit_floats_chunked(req_id, 1, tiles.col_tile);
    { std::vector<float> t; t.swap(tiles.col_tile); }
    if (has_sf) {
      emit_floats_chunked(req_id, 2, tiles.col_sf);
      { std::vector<float> t; t.swap(tiles.col_sf); }
    }

    return make_result(meta.str());
  } catch (const std::bad_alloc &) {
    return make_result("{\"error\":\"out of memory while parsing the file\"}");
  } catch (const std::exception &ex) {
    return make_result("{\"error\":\"" + json_escape(ex.what()) + "\"}");
  } catch (...) {
    return make_result("{\"error\":\"unknown error\"}");
  }
}

EMSCRIPTEN_KEEPALIVE
void free_result(char *ptr) { std::free(ptr); }
}
