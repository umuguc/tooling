#include <H5Cpp.h>
#include <hdf5.h>

#include <emscripten.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

// ── Data structures ───────────────────────────────────────────────────────────

struct AttrEntry {
  std::string name;
  std::string value;
};

struct DatasetInfo {
  std::string path;
  std::string type;
  std::string shape;
  std::string value;
  std::vector<AttrEntry> attributes;
};

struct Node {
  std::string name;
  std::string path;
  bool isGroup = false;
  std::vector<Node> children;
  DatasetInfo meta;
};

// ── HDF5 helpers ──────────────────────────────────────────────────────────────

static std::string joinPath(const std::string &a, const std::string &b) {
  return (a == "/") ? "/" + b : a + "/" + b;
}

// Converts one HDF5 value (raw bytes, file byte order) to a display string.
static std::string valToStr(hid_t type, const void *raw) {
  H5T_class_t cls  = H5Tget_class(type);
  size_t      size = H5Tget_size(type);

  if (cls == H5T_INTEGER) {
    bool sgn = H5Tget_sign(type) != H5T_SGN_NONE;
    hid_t src = H5Tcopy(type);
    std::vector<uint8_t> buf(std::max(size, sizeof(uint64_t)), 0);
    memcpy(buf.data(), raw, size);
    if (sgn) {
      H5Tconvert(src, H5T_NATIVE_INT64,  1, buf.data(), nullptr, H5P_DEFAULT);
      H5Tclose(src);
      int64_t v; memcpy(&v, buf.data(), 8);
      return std::to_string(v);
    } else {
      H5Tconvert(src, H5T_NATIVE_UINT64, 1, buf.data(), nullptr, H5P_DEFAULT);
      H5Tclose(src);
      uint64_t v; memcpy(&v, buf.data(), 8);
      return std::to_string(v);
    }
  }

  if (cls == H5T_FLOAT) {
    hid_t src = H5Tcopy(type);
    std::vector<uint8_t> buf(std::max(size, sizeof(double)), 0);
    memcpy(buf.data(), raw, size);
    H5Tconvert(src, H5T_NATIVE_DOUBLE, 1, buf.data(), nullptr, H5P_DEFAULT);
    H5Tclose(src);
    double v; memcpy(&v, buf.data(), 8);
    std::ostringstream oss; oss << std::setprecision(6) << v;
    return oss.str();
  }

  if (cls == H5T_STRING) {
    if (H5Tis_variable_str(type)) {
      const char *s = *static_cast<const char *const *>(raw);
      return s ? s : "";
    }
    const char *p = static_cast<const char *>(raw);
    const char *nul = (const char *)memchr(p, '\0', size);
    return std::string(p, nul ? (size_t)(nul - p) : size);
  }

  if (cls == H5T_ENUM) {
    char ename[256] = {};
    if (H5Tenum_nameof(type, raw, ename, sizeof(ename)) >= 0) return ename;
    hid_t base = H5Tget_super(type);
    std::string r = valToStr(base, raw); H5Tclose(base);
    return r;
  }

  if (cls == H5T_COMPOUND) {
    int nm = H5Tget_nmembers(type);
    std::ostringstream oss; oss << "{";
    for (int m = 0; m < nm; m++) {
      if (m) oss << ", ";
      char  *mname = H5Tget_member_name(type, m);
      hid_t  mtype = H5Tget_member_type(type, m);
      size_t moff  = H5Tget_member_offset(type, m);
      oss << mname << ": " << valToStr(mtype, static_cast<const char *>(raw) + moff);
      H5free_memory(mname); H5Tclose(mtype);
    }
    oss << "}";
    return oss.str();
  }

  if (cls == H5T_ARRAY) {
    int ndims = H5Tget_array_ndims(type);
    std::vector<hsize_t> dims(ndims);
    H5Tget_array_dims2(type, dims.data());
    hsize_t total = 1; for (auto d : dims) total *= d;
    hid_t  base = H5Tget_super(type); size_t bsz = H5Tget_size(base);
    std::ostringstream oss; oss << "[";
    hsize_t show = std::min(total, (hsize_t)100);
    for (hsize_t i = 0; i < show; i++) {
      if (i) oss << ", ";
      oss << valToStr(base, static_cast<const char *>(raw) + i * bsz);
    }
    if (total > show) oss << ", ...";
    oss << "]"; H5Tclose(base);
    return oss.str();
  }

  if (cls == H5T_VLEN) {
    const hvl_t *vl = static_cast<const hvl_t *>(raw);
    if (!vl || !vl->p) return "[]";
    hid_t  base = H5Tget_super(type); size_t bsz = H5Tget_size(base);
    std::ostringstream oss; oss << "[";
    hsize_t show = std::min((hsize_t)vl->len, (hsize_t)100);
    for (hsize_t i = 0; i < show; i++) {
      if (i) oss << ", ";
      oss << valToStr(base, static_cast<const char *>(vl->p) + i * bsz);
    }
    if (vl->len > show) oss << ", ...";
    oss << "]"; H5Tclose(base);
    return oss.str();
  }

  return "<type:" + std::to_string((int)cls) + ">";
}

// Generates CSV for a buffer of nelem HDF5 elements. Compound → header+rows.
static std::string dataToCSV(hid_t type, const uint8_t *buf,
                              hsize_t nelem, size_t type_sz) {
  std::ostringstream csv;
  if (H5Tget_class(type) == H5T_COMPOUND) {
    int nm = H5Tget_nmembers(type);
    std::vector<std::string> mnames(nm);
    std::vector<hid_t>      mtypes(nm);
    std::vector<size_t>     moffs(nm);
    for (int m = 0; m < nm; m++) {
      char *n = H5Tget_member_name(type, m);
      mnames[m] = n; H5free_memory(n);
      mtypes[m] = H5Tget_member_type(type, m);
      moffs[m]  = H5Tget_member_offset(type, m);
    }
    for (int m = 0; m < nm; m++) { if (m) csv << ","; csv << mnames[m]; }
    csv << "\n";
    for (hsize_t k = 0; k < nelem; k++) {
      const uint8_t *elem = buf + k * type_sz;
      for (int m = 0; m < nm; m++) {
        if (m) csv << ",";
        std::string v = valToStr(mtypes[m], elem + moffs[m]);
        if (v.find(',') != std::string::npos || v.find('"') != std::string::npos
            || v.find('\n') != std::string::npos) {
          csv << '"';
          for (char c : v) { if (c == '"') csv << '"'; csv << c; }
          csv << '"';
        } else { csv << v; }
      }
      csv << "\n";
    }
    for (auto t : mtypes) H5Tclose(t);
  } else {
    for (hsize_t k = 0; k < nelem; k++)
      csv << valToStr(type, buf + k * type_sz) << "\n";
  }
  return csv.str();
}

static std::vector<AttrEntry> readAllAttrEntries(hid_t obj) {
  std::vector<AttrEntry> attrs;

  int n = H5Aget_num_attrs(obj);
  for (int i = 0; i < n; i++) {
    hid_t attr = H5Aopen_idx(obj, i);
    if (attr < 0) continue;

    char name[1024] = {};
    H5Aget_name(attr, 1024, name);

    hid_t       type  = H5Aget_type(attr);
    hid_t       space = H5Aget_space(attr);
    H5T_class_t cls   = H5Tget_class(type);
    hssize_t    nelem = H5Sget_simple_extent_npoints(space);
    if (nelem < 1) nelem = 1;

    // H5Tget_size returns H5T_VARIABLE (SIZE_MAX) for vlen strings; use pointer size instead.
    bool   is_vlen_str = (cls == H5T_STRING && H5Tis_variable_str(type));
    size_t type_sz     = is_vlen_str ? sizeof(char *) : H5Tget_size(type);
    size_t total_bytes = type_sz * (size_t)nelem;

    std::string val;

    if (type_sz != H5T_VARIABLE && total_bytes > 0 && total_bytes <= (64u << 20u)) {
      std::vector<uint8_t> buf(total_bytes, 0);
      if (H5Aread(attr, type, buf.data()) >= 0) {
        if (nelem == 1) {
          val = valToStr(type, buf.data());
        } else {
          std::ostringstream oss; oss << "[";
          hssize_t show = std::min(nelem, (hssize_t)100);
          for (hssize_t k = 0; k < show; k++) {
            if (k) oss << ", ";
            oss << valToStr(type, buf.data() + (size_t)k * type_sz);
          }
          if (nelem > show) oss << ", ...";
          oss << "]";
          val = oss.str();
        }
        if (cls == H5T_VLEN || is_vlen_str)
          H5Treclaim(type, space, H5P_DEFAULT, buf.data());
      }
    }

    if (!val.empty())
      attrs.push_back({name, val});

    H5Sclose(space);
    H5Tclose(type);
    H5Aclose(attr);
  }
  return attrs;
}

static DatasetInfo readDatasetInfo(hid_t file, const std::string &path) {
  DatasetInfo info;
  info.path = path;

  hid_t dset = H5Dopen2(file, path.c_str(), H5P_DEFAULT);
  if (dset < 0) return info;

  hid_t       type  = H5Dget_type(dset);
  hid_t       space = H5Dget_space(dset);
  H5T_class_t tcls  = H5Tget_class(type);

  info.type = (tcls == H5T_FLOAT) ? "float" : (tcls == H5T_INTEGER) ? "int" : "other";

  int ndims = H5Sget_simple_extent_ndims(space);
  if (ndims < 0) ndims = 0;
  std::vector<hsize_t> dims(ndims);
  H5Sget_simple_extent_dims(space, dims.data(), nullptr);

  std::ostringstream ss;
  ss << "(";
  for (int i = 0; i < ndims; i++) { ss << dims[i]; if (i + 1 < ndims) ss << ","; }
  ss << ")";
  info.shape = ss.str();

  // Read actual data values for small datasets (≤1024 elements)
  hsize_t nelem = (hsize_t)H5Sget_simple_extent_npoints(space);
  if (nelem == 0) nelem = 1;
  bool   is_vlen_str = (tcls == H5T_STRING && H5Tis_variable_str(type));
  bool   is_vlen     = (tcls == H5T_VLEN);
  size_t type_sz     = is_vlen_str ? sizeof(char *)
                     : is_vlen     ? sizeof(hvl_t)
                     : H5Tget_size(type);

  if (type_sz != H5T_VARIABLE && type_sz > 0) {
    // Read first min(nelem,100) elements for the panel display via hyperslab.
    // Full data is extracted on demand by get_dataset_csv() when download clicked.
    const hsize_t MAX_DISP = 100;
    hsize_t to_read = std::min(nelem, MAX_DISP);

    hid_t mem_spc, file_spc;
    bool  own_spc = false;

    if (to_read == nelem) {
      mem_spc  = H5S_ALL;
      file_spc = H5S_ALL;
    } else {
      own_spc  = true;
      mem_spc  = H5Screate_simple(1, &to_read, nullptr);
      file_spc = H5Scopy(space);
      std::vector<hsize_t> sel_start(ndims, 0);
      std::vector<hsize_t> sel_count(dims.begin(), dims.end());
      if (ndims == 1) {
        sel_count[0] = to_read;
      } else {
        hsize_t inner = nelem / dims[0];
        sel_count[0]  = std::max((hsize_t)1, (to_read + inner - 1) / inner);
        to_read = std::min(sel_count[0] * inner, nelem);
        H5Sclose(mem_spc);
        mem_spc = H5Screate_simple(1, &to_read, nullptr);
      }
      H5Sselect_hyperslab(file_spc, H5S_SELECT_SET,
                          sel_start.data(), nullptr,
                          sel_count.data(), nullptr);
    }

    if (type_sz * to_read <= (16u << 20u)) {
      std::vector<uint8_t> buf(type_sz * to_read, 0);
      if (H5Dread(dset, type, mem_spc, file_spc, H5P_DEFAULT, buf.data()) >= 0) {
        if (nelem == 1) {
          info.value = valToStr(type, buf.data());
        } else {
          std::ostringstream oss; oss << "[";
          for (hsize_t k = 0; k < to_read; k++) {
            if (k) oss << ", ";
            oss << valToStr(type, buf.data() + k * type_sz);
          }
          if (nelem > to_read) oss << ", ...(" << nelem << " total)";
          oss << "]";
          info.value = oss.str();
        }
        if (is_vlen || is_vlen_str)
          H5Treclaim(type, own_spc ? mem_spc : space,
                     H5P_DEFAULT, buf.data());
      }
    }

    if (own_spc) { H5Sclose(mem_spc); H5Sclose(file_spc); }
  }

  info.attributes = readAllAttrEntries(dset);

  H5Sclose(space);
  H5Tclose(type);
  H5Dclose(dset);
  return info;
}

// JSON string literal escaping (handles the common control chars).
static std::string jsonStr(const std::string &s) {
  std::string r;
  for (unsigned char c : s) {
    if      (c == '"')  r += "\\\"";
    else if (c == '\\') r += "\\\\";
    else if (c == '\n') r += "\\n";
    else if (c == '\r') r += "\\r";
    else if (c == '\t') r += "\\t";
    else                r += (char)c;
  }
  return r;
}

// Serialises a Node tree to compact JSON (only name/path/type/children).
static void nodeToJson(const Node &n, std::ostringstream &o) {
  o << "{\"name\":\"" << jsonStr(n.name)
    << "\",\"path\":\"" << jsonStr(n.path)
    << "\",\"type\":\""  << (n.isGroup ? "group" : "dataset") << "\"";
  if (!n.children.empty()) {
    o << ",\"children\":[";
    for (size_t i = 0; i < n.children.size(); ++i) {
      if (i) o << ",";
      nodeToJson(n.children[i], o);
    }
    o << "]";
  }
  o << "}";
}

// BFS so each object is visited via its shortest path first.
static void buildTree(hid_t file, Node &root) {
  std::unordered_set<std::string> visited;

  {
    hid_t grp = H5Gopen2(file, "/", H5P_DEFAULT);
    if (grp >= 0) {
      H5O_info2_t oi = {};
      H5Oget_info3(grp, &oi, H5O_INFO_BASIC);
      visited.insert(std::string(
          reinterpret_cast<const char *>(oi.token.__data), H5O_MAX_TOKEN_SIZE));
      H5Gclose(grp);
    }
  }

  std::queue<Node *> q;
  q.push(&root);

  while (!q.empty()) {
    Node *cur = q.front(); q.pop();
    if (!cur->isGroup) continue;

    hid_t grp = H5Gopen2(file, cur->path.c_str(), H5P_DEFAULT);
    if (grp < 0) continue;

    hsize_t nobj = 0;
    H5Gget_num_objs(grp, &nobj);

    for (hsize_t i = 0; i < nobj; i++) {
      char name[1024] = {};
      H5Gget_objname_by_idx(grp, i, name, 1024);
      int type = H5Gget_objtype_by_idx(grp, i);
      if (type != H5G_GROUP && type != H5G_DATASET) continue;

      H5O_info2_t oi = {};
      if (H5Oget_info_by_name3(grp, name, &oi, H5O_INFO_BASIC, H5P_DEFAULT) < 0)
        continue;
      std::string tok(reinterpret_cast<const char *>(oi.token.__data), H5O_MAX_TOKEN_SIZE);
      if (visited.count(tok)) continue;
      visited.insert(tok);

      Node child;
      child.name    = name;
      child.path    = joinPath(cur->path, name);
      child.isGroup = (type == H5G_GROUP);
      if (type == H5G_DATASET)
        child.meta = readDatasetInfo(file, child.path);
      cur->children.push_back(std::move(child));
    }

    H5Gclose(grp);

    for (auto &child : cur->children)
      if (child.isGroup) q.push(&child);
  }
}

// ── Lazy CSV export (called from JS via ccall on download click) ──────────────

static std::string g_hdf5_path;
static hid_t       g_hdf5_file = -1; // kept open so downloads work after VFS unlink

extern "C" {

// Returns a malloc'd CSV string for the given dataset path.
// Uses the file handle kept open by explore() — works even after VFS unlink.
// The caller (JS) must call Module._free() on the returned pointer.
EMSCRIPTEN_KEEPALIVE
char *get_dataset_csv(const char *dset_path_cstr) {
  if (g_hdf5_file < 0 || !dset_path_cstr) return nullptr;

  std::string result;
  hid_t dset = H5Dopen2(g_hdf5_file, dset_path_cstr, H5P_DEFAULT);
  if (dset >= 0) {
    hid_t type  = H5Dget_type(dset);
    hid_t space = H5Dget_space(dset);
    hsize_t nelem = (hsize_t)H5Sget_simple_extent_npoints(space);
    if (nelem == 0) nelem = 1;

    H5T_class_t tcls        = H5Tget_class(type);
    bool        is_vlen_str = (tcls == H5T_STRING && H5Tis_variable_str(type));
    bool        is_vlen     = (tcls == H5T_VLEN);
    size_t      type_sz     = is_vlen_str ? sizeof(char *)
                             : is_vlen     ? sizeof(hvl_t)
                             : H5Tget_size(type);

    const size_t MEM_LIMIT = 512u << 20u;
    if (type_sz != H5T_VARIABLE && type_sz > 0 && nelem <= MEM_LIMIT / type_sz) {
      std::vector<uint8_t> buf(type_sz * nelem, 0);
      if (H5Dread(dset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data()) >= 0) {
        result = dataToCSV(type, buf.data(), nelem, type_sz);
        if (is_vlen || is_vlen_str)
          H5Treclaim(type, space, H5P_DEFAULT, buf.data());
      }
    }
    H5Sclose(space);
    H5Tclose(type);
    H5Dclose(dset);
  }

  char *out = (char *)malloc(result.size() + 1);
  if (out) memcpy(out, result.c_str(), result.size() + 1);
  return out;
}

} // extern "C"

// ── HTML generation ───────────────────────────────────────────────────────────

static std::string eH(const std::string &s) {
  std::string r;
  for (unsigned char c : s) {
    if      (c == '&') r += "&amp;";
    else if (c == '<') r += "&lt;";
    else if (c == '>') r += "&gt;";
    else if (c == '"') r += "&quot;";
    else               r += c;
  }
  return r;
}

static std::string eJ(const std::string &s) {
  std::string r;
  char hex[8];
  for (unsigned char c : s) {
    if      (c == '\\') r += "\\\\";
    else if (c == '"')  r += "\\\"";
    else if (c == '\n') r += "\\n";
    else if (c == '\r') r += "\\r";
    else if (c < 0x20 || c == 0x7f) {
      snprintf(hex, sizeof(hex), "\\u%04x", (unsigned)c);
      r += hex;
    }
    else r += (char)c;
  }
  return r;
}

static std::string generateHTML(const Node &root) {
  std::ostringstream treeHtml, dataJs;

  std::function<void(const Node &, int)> renderNode = [&](const Node &n, int depth) {
    if (n.isGroup) {
      treeHtml << "<details" << (depth < 2 ? " open" : "") << ">\n"
               << "<summary><span class='gi'>&#9654;</span>"
               << "<span class='gn'>" << eH(n.name) << "</span></summary>\n"
               << "<div class='ch'>\n";
      for (auto &c : n.children) renderNode(c, depth + 1);
      treeHtml << "</div></details>\n";
    } else {
      treeHtml << "<div class='ds' data-path=\"" << eH(n.meta.path)
               << "\" onclick='show(this)'>"
               << "<span class='di'>&#9670;</span>"
               << "<span class='dn'>" << eH(n.name) << "</span>"
               << "</div>\n";
    }
  };

  std::function<void(const Node &)> collectData = [&](const Node &n) {
    if (!n.isGroup) {
      dataJs << "D[\"" << eJ(n.meta.path) << "\"]={p:\"" << eJ(n.meta.path)
             << "\",t:\"" << eJ(n.meta.type)
             << "\",s:\"" << eJ(n.meta.shape)
             << "\",v:\"" << eJ(n.meta.value)
             << "\",a:[";
      bool first = true;
      for (auto &a : n.meta.attributes) {
        if (!first) dataJs << ",";
        dataJs << "{n:\"" << eJ(a.name) << "\",v:\"" << eJ(a.value) << "\"}";
        first = false;
      }
      dataJs << "]};\n";
    }
    for (auto &c : n.children) collectData(c);
  };

  renderNode(root, 0);
  collectData(root);

  std::ostringstream out;
  out << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>HDF5 Explorer</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:#0d1117;color:#e6edf3;display:flex;height:100vh;overflow:hidden;font-size:13px}
#sidebar{width:340px;min-width:160px;background:#161b22;border-right:1px solid #30363d;display:flex;flex-direction:column;overflow:hidden}
#sh{padding:12px 16px;border-bottom:1px solid #30363d;font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.1em;color:#7d8590;flex-shrink:0}
#tree{overflow-y:auto;padding:6px 0;flex:1}
#tree::-webkit-scrollbar{width:5px}
#tree::-webkit-scrollbar-track{background:transparent}
#tree::-webkit-scrollbar-thumb{background:#30363d;border-radius:3px}
details>summary{display:flex;align-items:center;gap:5px;padding:3px 14px;cursor:pointer;list-style:none;user-select:none;white-space:nowrap;overflow:hidden}
details>summary::-webkit-details-marker{display:none}
details>summary:hover{background:#1c2128}
.gi{font-size:8px;color:#7d8590;flex-shrink:0;width:10px;text-align:center;display:inline-block;transition:transform .12s ease}
details[open]>summary>.gi{transform:rotate(90deg)}
.gn{color:#f0883e;font-weight:500;overflow:hidden;text-overflow:ellipsis}
.ch{border-left:1px solid #21262d;margin-left:18px}
.ds{display:flex;align-items:center;gap:5px;padding:3px 14px;cursor:pointer;white-space:nowrap;overflow:hidden}
.ds:hover{background:#1c2128}
.ds.active{background:#1f6feb1a;border-left:2px solid #1f6feb;padding-left:12px}
.di{color:#3fb950;font-size:10px;flex-shrink:0}
.dn{color:#acd0f7;overflow:hidden;text-overflow:ellipsis}
#panel{flex:1;overflow-y:auto;padding:36px 40px;display:flex;flex-direction:column;gap:22px}
#panel::-webkit-scrollbar{width:5px}
#panel::-webkit-scrollbar-track{background:transparent}
#panel::-webkit-scrollbar-thumb{background:#30363d;border-radius:3px}
.empty{margin:auto;text-align:center;color:#3d444d;font-size:14px;line-height:2}
.ei{font-size:52px;display:block;margin-bottom:8px;opacity:.4}
.crumb{font-size:11px;color:#7d8590;font-family:'Cascadia Code','Fira Code','Consolas',monospace;word-break:break-all;margin-bottom:6px}
.title{font-size:24px;font-weight:600;color:#e6edf3}
.badges{display:flex;gap:8px;flex-wrap:wrap}
.badge{padding:3px 11px;border-radius:12px;font-size:12px;font-weight:500;font-family:'Cascadia Code','Fira Code','Consolas',monospace}
.bt{background:#12261e;color:#3fb950;border:1px solid #2ea04330}
.bs{background:#111d3a;color:#79c0ff;border:1px solid #1f6feb30}
.dval{color:#a5d6ff;font-family:monospace;font-size:13px;padding:8px 12px;background:#161b22;border-radius:6px;word-break:break-all;margin-bottom:8px}
.dlbtn{display:inline-block;margin-bottom:16px;padding:5px 14px;background:#21262d;border:1px solid #30363d;color:#58a6ff;border-radius:6px;cursor:pointer;font-size:12px}
.dlbtn:hover{background:#1f6feb25;border-color:#58a6ff}
.stitle{font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.1em;color:#7d8590;margin-bottom:10px}
.at{width:100%;border-collapse:collapse;font-family:'Cascadia Code','Fira Code','Consolas',monospace;font-size:12px}
.at tr{border-bottom:1px solid #21262d}
.at tr:last-child{border-bottom:none}
.at tr:hover td{background:#1c2128}
.at td{padding:7px 10px;vertical-align:top}
.ak{color:#ff7b72;width:1%;white-space:nowrap;padding-right:28px}
.av{color:#a5d6ff;word-break:break-all}
</style>
</head>
<body>
<div id="sidebar">
  <div id="sh">HDF5 Explorer</div>
  <div id="tree">
)HTML";

  out << treeHtml.str();

  out << R"HTML(  </div>
</div>
<div id="panel">
  <div class="empty"><span class="ei">&#9670;</span>Select a dataset</div>
</div>
<script>
const D={};
)HTML";

  out << dataJs.str();

  out << R"HTML(
let cur=null;
function show(el){
  if(cur)cur.classList.remove('active');
  cur=el;el.classList.add('active');
  const p=el.dataset.path,d=D[p];
  if(!d)return;
  const name=p.split('/').filter(Boolean).pop()||p;
  let val='';
  if(d.v) val='<div class="stitle">Value</div><div class="dval">'+e(d.v)+'</div>';
  let dl='';
  if(d.v) dl='<button class="dlbtn" data-path="'+e(p)+'">&#11123; Download CSV</button>';
  let at='';
  if(d.a&&d.a.length){
    at='<div class="stitle">Attributes</div><table class="at">';
    for(const a of d.a)
      at+='<tr><td class="ak">'+e(a.n)+'</td><td class="av">'+e(a.v)+'</td></tr>';
    at+='</table>';
  }
  const panel=document.getElementById('panel');
  panel.innerHTML=
    '<div class="crumb">'+e(p)+'</div>'
   +'<div class="title">'+e(name)+'</div>'
   +'<div class="badges">'
   +'<span class="badge bt">'+e(d.t)+'</span>'
   +'<span class="badge bs">'+e(d.s)+'</span>'
   +'</div>'
   +val+dl+at;
  if(d.v){
    panel.querySelector('.dlbtn').addEventListener('click',function(ev){
      dlCSV(ev.currentTarget.dataset.path);
    });
  }
}
function dlCSV(path){
  const name=path.split('/').filter(Boolean).pop()||'data';
  const btn=event&&event.currentTarget;
  if(btn){btn.textContent='Extracting...';btn.disabled=true;}
  setTimeout(function(){
    const M=window.parent._explorerModule||window._explorerModule;
    const ptr=M.ccall('get_dataset_csv','number',['string'],[path]);
    if(btn){btn.textContent='&#11123; Download CSV';btn.disabled=false;}
    if(!ptr)return;
    const csv=M.UTF8ToString(ptr);
    M._free_result(ptr);
    const blob=new Blob([csv],{type:'text/csv'});
    const a=document.createElement('a');
    a.href=URL.createObjectURL(blob);
    a.download=name+'.csv';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(a.href);
  },10);
}
function e(s){
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}
</script>
</body>
</html>)HTML";

  return out.str();
}

// ── LZ4 filter registration ───────────────────────────────────────────────────
// H5Z_LZ4 is defined in H5Zlz4.c compiled alongside this file.
// Registering it statically means lz4-compressed datasets are readable
// without a dynamic plugin path.
extern "C" const H5Z_class2_t H5Z_LZ4[];
namespace {
    struct LZ4Init { LZ4Init() { H5Zregister(H5Z_LZ4); } } _lz4_init;
}

// ── Converter helpers ─────────────────────────────────────────────────────────

static std::string jsonStringArray(const std::vector<std::string>& v) {
  std::ostringstream o;
  o << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i) o << ",";
    o << "\"" << eJ(v[i]) << "\"";
  }
  o << "]";
  return o.str();
}

// Parses a JSON string array.  null entries are preserved as empty strings so
// callers can distinguish "empty slot" from "dataset with an empty name".
static std::vector<std::string> parseJsonStringArray(const char* json) {
  std::vector<std::string> r;
  if (!json) return r;
  const char* p = json;
  while (*p && *p != '[') p++;
  if (!*p) return r;
  p++;
  while (*p) {
    while (*p && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    if (*p == ']' || !*p) break;
    if (*p == '"') {
      p++;
      std::string s;
      while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) { p++; s += *p++; }
        else s += *p++;
      }
      if (*p == '"') p++;
      r.push_back(s);
    } else if (strncmp(p, "null", 4) == 0) {
      r.push_back("");   // empty string = empty/null position
      p += 4;
    } else p++;
  }
  return r;
}

// BFS through all groups, collecting those whose leaf name matches target.
static void findGroupsRec(hid_t file_id, const std::string& target,
                           const std::string& cur_path,
                           std::unordered_set<std::string>& visited,
                           std::vector<std::string>& out) {
  hid_t grp = H5Gopen2(file_id, cur_path.c_str(), H5P_DEFAULT);
  if (grp < 0) return;

  H5O_info2_t oi = {};
  if (H5Oget_info3(grp, &oi, H5O_INFO_BASIC) >= 0) {
    std::string tok(reinterpret_cast<const char*>(oi.token.__data), H5O_MAX_TOKEN_SIZE);
    if (visited.count(tok)) { H5Gclose(grp); return; }
    visited.insert(tok);
  }

  hsize_t nobj = 0;
  H5Gget_num_objs(grp, &nobj);
  for (hsize_t i = 0; i < nobj; i++) {
    char name[1024] = {};
    H5Gget_objname_by_idx(grp, i, name, 1024);
    if (H5Gget_objtype_by_idx(grp, i) != H5G_GROUP) continue;
    std::string child = (cur_path == "/") ? std::string("/") + name : cur_path + "/" + name;
    if (std::string(name) == target) out.push_back(child);
    findGroupsRec(file_id, target, child, visited, out);
  }
  H5Gclose(grp);
}

static std::vector<std::string> listDatasets(hid_t file_id, const std::string& group_path) {
  std::vector<std::string> result;
  hid_t grp = H5Gopen2(file_id, group_path.c_str(), H5P_DEFAULT);
  if (grp < 0) return result;
  hsize_t nobj = 0;
  H5Gget_num_objs(grp, &nobj);
  for (hsize_t i = 0; i < nobj; i++) {
    char name[1024] = {};
    H5Gget_objname_by_idx(grp, i, name, 1024);
    if (H5Gget_objtype_by_idx(grp, i) == H5G_DATASET)
      result.emplace_back(name);
  }
  H5Gclose(grp);
  return result;
}

// Compound point type matching converter.cpp's CompoundData / Point structs.
struct XYZ { double x, y, z; };

// Reads the first 3 members of a compound dataset as 3 doubles.
// Auto-detects field names from the file's compound type, so it works for any
// compound layout — {x,y,z} for positions or {linearity,planarity,sphericity}
// for shape factors alike.
// When apply_shift is true, adds the Fiber::NumericalShift attribute offset.
static std::vector<XYZ> readCompound3(hid_t file_id, const std::string& full_path,
                                       bool apply_shift) {
  std::vector<XYZ> pts;
  hid_t dset = H5Dopen2(file_id, full_path.c_str(), H5P_DEFAULT);
  if (dset < 0) return pts;

  // Detect the first 3 member names from the file's compound type.
  std::string f0 = "x", f1 = "y", f2 = "z";
  hid_t ftype = H5Dget_type(dset);
  if (H5Tget_class(ftype) == H5T_COMPOUND && H5Tget_nmembers(ftype) >= 3) {
    if (char* m = H5Tget_member_name(ftype, 0)) { f0 = m; H5free_memory(m); }
    if (char* m = H5Tget_member_name(ftype, 1)) { f1 = m; H5free_memory(m); }
    if (char* m = H5Tget_member_name(ftype, 2)) { f2 = m; H5free_memory(m); }
  }
  H5Tclose(ftype);

  hid_t space = H5Dget_space(dset);
  hssize_t n   = H5Sget_simple_extent_npoints(space);
  H5Sclose(space);

  if (n > 0 && n <= 100'000'000) {
    hid_t mt = H5Tcreate(H5T_COMPOUND, sizeof(XYZ));
    H5Tinsert(mt, f0.c_str(), 0,               H5T_NATIVE_DOUBLE);
    H5Tinsert(mt, f1.c_str(), sizeof(double),   H5T_NATIVE_DOUBLE);
    H5Tinsert(mt, f2.c_str(), 2*sizeof(double), H5T_NATIVE_DOUBLE);
    pts.resize(n);
    if (H5Dread(dset, mt, H5S_ALL, H5S_ALL, H5P_DEFAULT, pts.data()) < 0)
      pts.clear();
    H5Tclose(mt);
  }

  if (apply_shift && !pts.empty() && H5Aexists(dset, "Fiber::NumericalShift") > 0) {
    hid_t attr = H5Aopen(dset, "Fiber::NumericalShift", H5P_DEFAULT);
    if (attr >= 0) {
      double shift[3] = {};
      // Read with the file's own type — works for H5T_ARRAY[3] (scalar
      // dataspace, npoints==1) as well as a simple 3-element dataspace.
      hid_t aftype = H5Aget_type(attr);
      H5Aread(attr, aftype, shift);
      H5Tclose(aftype);
      H5Aclose(attr);
      for (auto& p : pts) { p.x += shift[0]; p.y += shift[1]; p.z += shift[2]; }
    }
  }

  H5Dclose(dset);
  return pts;
}

static std::vector<XYZ> readPoints(hid_t file_id, const std::string& group_path,
                                    const std::string& ds_name) {
  std::string path = (group_path == "/" ? "" : group_path) + "/" + ds_name;
  return readCompound3(file_id, path, true);
}

// Reads a flat 1D scalar dataset as doubles, converting from file type.
static std::vector<double> readScalar1D(hid_t file_id, const std::string& full_path) {
  std::vector<double> data;
  hid_t dset = H5Dopen2(file_id, full_path.c_str(), H5P_DEFAULT);
  if (dset < 0) return data;
  hid_t space = H5Dget_space(dset);
  hssize_t n  = H5Sget_simple_extent_npoints(space);
  H5Sclose(space);
  if (n > 0 && n <= 500'000'000) {
    data.resize(n);
    if (H5Dread(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data()) < 0)
      data.clear();
  }
  H5Dclose(dset);
  return data;
}

// Matches binary_io::detail::GridIndex — must stay layout-compatible.
struct GridIndex { int x, y; };

template<typename T>
static void append(std::vector<uint8_t>& buf, const T& v) {
  const auto* p = reinterpret_cast<const uint8_t*>(&v);
  buf.insert(buf.end(), p, p + sizeof(T));
}

// Serialises one tile into the binary_io grid-file format (binary_io.hpp):
//   GridIndex{int x, int y}  →  sizeof(GridIndex) = 8 bytes
//   int size                 →  4 bytes
//   size × (double,double,double)  →  size × sizeof(double)*3 bytes
static void writeTile(std::vector<uint8_t>& buf, int ti, int tj,
                       const std::vector<XYZ>& pts) {
  GridIndex gi{ti, tj};
  append(buf, gi);
  int n = (int)pts.size();
  append(buf, n);
  for (auto& p : pts) {
    append(buf, p.x); append(buf, p.y); append(buf, p.z);
  }
}

static char* makeJsonResult(const std::string& json) {
  char* r = static_cast<char*>(std::malloc(json.size() + 1));
  if (r) std::memcpy(r, json.c_str(), json.size() + 1);
  return r;
}

// ── Emscripten exports ────────────────────────────────────────────────────────

extern "C" {

// Processes the HDF5 file at h5path (must exist in the Emscripten virtual FS),
// returns a malloc'd UTF-8 HTML string. Caller must free with free_result().
EMSCRIPTEN_KEEPALIVE
char *explore(const char *h5path) {
  try {
    g_hdf5_path = h5path ? h5path : "";
    // Close any previously held file handle before opening the new file.
    if (g_hdf5_file >= 0) { H5Fclose(g_hdf5_file); g_hdf5_file = -1; }
    // Open a long-lived handle for download calls (stays valid after VFS unlink).
    g_hdf5_file = H5Fopen(h5path, H5F_ACC_RDONLY, H5P_DEFAULT);
    H5::H5File file(h5path, H5F_ACC_RDONLY);

    Node root;
    root.name    = "/";
    root.path    = "/";
    root.isGroup = true;
    buildTree(file.getId(), root);

    std::string html = generateHTML(root);
    char *result = static_cast<char *>(std::malloc(html.size() + 1));
    std::memcpy(result, html.c_str(), html.size() + 1);
    return result;
  } catch (const H5::Exception &ex) {
    std::string msg = std::string("HDF5 error: ") + ex.getCDetailMsg();
    char *r = static_cast<char *>(std::malloc(msg.size() + 1));
    std::memcpy(r, msg.c_str(), msg.size() + 1);
    return r;
  } catch (const std::exception &ex) {
    std::string msg = std::string("Error: ") + ex.what();
    char *r = static_cast<char *>(std::malloc(msg.size() + 1));
    std::memcpy(r, msg.c_str(), msg.size() + 1);
    return r;
  } catch (...) {
    const char *msg = "Unknown error processing HDF5 file";
    char *r = static_cast<char *>(std::malloc(std::strlen(msg) + 1));
    std::strcpy(r, msg);
    return r;
  }
}

// Frees the string returned by explore(), find_groups(), or get_datasets().
EMSCRIPTEN_KEEPALIVE
void free_result(char *ptr) {
  std::free(ptr);
}

// Returns a malloc'd JSON array of HDF5 group paths whose leaf name equals group_name.
// Caller must free with free_result().
EMSCRIPTEN_KEEPALIVE
char* find_groups(const char* h5path, const char* group_name) {
  try {
    hid_t file = H5Fopen(h5path, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) return makeJsonResult("[]");
    std::unordered_set<std::string> visited;
    std::vector<std::string>        groups;
    findGroupsRec(file, group_name ? group_name : "", "/", visited, groups);
    H5Fclose(file);
    return makeJsonResult(jsonStringArray(groups));
  } catch (...) {
    return makeJsonResult("[]");
  }
}

// Returns a malloc'd JSON array of dataset names inside group_path.
// Caller must free with free_result().
EMSCRIPTEN_KEEPALIVE
char* get_datasets(const char* h5path, const char* group_path) {
  try {
    hid_t file = H5Fopen(h5path, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) return makeJsonResult("[]");
    auto ds = listDatasets(file, group_path ? group_path : "/");
    H5Fclose(file);
    return makeJsonResult(jsonStringArray(ds));
  } catch (...) {
    return makeJsonResult("[]");
  }
}

// Converts selected datasets to binary_io grid format and writes to out_vfs_path in the
// Emscripten virtual FS. Returns the number of bytes written, or -1 on error.
// datasets_json : JSON array of dataset names in row-major order (rows * cols entries).
EMSCRIPTEN_KEEPALIVE
int convert_to_binary_file(const char* h5path, const char* group_path,
                            const char* datasets_json, int rows, int cols,
                            const char* out_vfs_path) {
  try {
    hid_t file = H5Fopen(h5path, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) return -1;

    auto        names = parseJsonStringArray(datasets_json);
    std::string gpath = group_path ? group_path : "/";
    std::vector<uint8_t> buf;

    for (int i = 0; i < rows; i++) {
      for (int j = 0; j < cols; j++) {
        int idx = i * cols + j;
        if (idx >= (int)names.size()) break;
        if (names[idx].empty())
          writeTile(buf, i, j, {});          // null/empty slot — write 0-point tile
        else
          writeTile(buf, i, j, readPoints(file, gpath, names[idx]));
      }
    }
    H5Fclose(file);

    if (buf.empty()) return 0;

    std::ofstream out(out_vfs_path ? out_vfs_path : "/out.bin", std::ios::binary);
    if (!out) return -1;
    out.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
    return (int)buf.size();
  } catch (...) {
    return -1;
  }
}

// Converts shape factors to binary_io grid format.
//
// SF data is organised as three parallel groups (grp_l, grp_p, grp_s) sitting
// at sf_parent_path, each containing one scalar 1-D dataset per fiber — the
// same dataset names as in the positions group.
//
//   sf_parent_path/grp_l/fiber_name  →  N doubles (linearity per point)
//   sf_parent_path/grp_p/fiber_name  →  N doubles (planarity per point)
//   sf_parent_path/grp_s/fiber_name  →  N doubles (sphericity per point)
//
// These are interleaved as (L, P, S) triplets and written in binary_io format.
EMSCRIPTEN_KEEPALIVE
int convert_sf_to_binary_file(const char* h5path,
                               const char* datasets_json,
                               int rows, int cols,
                               const char* sf_parent_path,
                               const char* grp_l, const char* grp_p, const char* grp_s,
                               const char* out_vfs_path) {
  try {
    hid_t file = H5Fopen(h5path, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) return -1;

    auto        names = parseJsonStringArray(datasets_json);
    std::string sfp   = sf_parent_path ? sf_parent_path : "/";
    if (sfp.size() > 1 && sfp.back() == '/') sfp.pop_back();
    std::string gl = grp_l ? grp_l : "Linear";
    std::string gp = grp_p ? grp_p : "Planarity";
    std::string gs = grp_s ? grp_s : "Spherical";

    std::vector<uint8_t> buf;

    for (int i = 0; i < rows; i++) {
      for (int j = 0; j < cols; j++) {
        int idx = i * cols + j;
        if (idx >= (int)names.size()) break;
        const std::string& ds = names[idx];
        if (ds.empty()) {
          writeTile(buf, i, j, {});          // null/empty slot — write 0-point tile
        } else {
          auto L = readScalar1D(file, sfp + "/" + gl + "/" + ds);
          auto P = readScalar1D(file, sfp + "/" + gp + "/" + ds);
          auto S = readScalar1D(file, sfp + "/" + gs + "/" + ds);
          size_t n = L.size();
          std::vector<XYZ> pts(n);
          for (size_t k = 0; k < n; k++)
            pts[k] = { L[k], k < P.size() ? P[k] : 0.0, k < S.size() ? S[k] : 0.0 };
          writeTile(buf, i, j, pts);
        }
      }
    }
    H5Fclose(file);

    if (buf.empty()) return 0;

    std::ofstream out(out_vfs_path ? out_vfs_path : "/sf_out.bin", std::ios::binary);
    if (!out) return -1;
    out.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
    return (int)buf.size();
  } catch (...) {
    return -1;
  }
}

// ── Binary → HDF5 helpers ─────────────────────────────────────────────────────

// Reads one tile from a binary_io grid file (binary_io::read_grid_file pattern).
// Returns false on EOF or a corrupt/oversized count.
static bool readTileBin(std::ifstream& f, GridIndex& gi, int& amount,
                         std::vector<XYZ>& pts) {
  if (!f.read(reinterpret_cast<char*>(&gi), sizeof(GridIndex))) return false;
  if (!f.read(reinterpret_cast<char*>(&amount), sizeof(int)))   return false;
  if (amount < 0 || amount > 100'000'000) return false;
  pts.resize(amount);
  // Read all points in one call (binary_io writes sizeof(double)*3 per point).
  if (amount > 0 && !f.read(reinterpret_cast<char*>(pts.data()),
                             amount * static_cast<std::streamsize>(sizeof(double) * 3)))
    return false;
  return true;
}

static void ensureGroups(hid_t file_id, std::initializer_list<std::string> paths) {
  hid_t lcpl = H5Pcreate(H5P_LINK_CREATE);
  H5Pset_create_intermediate_group(lcpl, 1);
  for (const auto& p : paths) {
    hid_t grp = H5Gcreate2(file_id, p.c_str(), lcpl, H5P_DEFAULT, H5P_DEFAULT);
    if (grp >= 0) H5Gclose(grp);
  }
  H5Pclose(lcpl);
}

// Returns a dataset creation property list with a single chunk (the whole
// dataset) and the LZ4 filter applied.  Caller must H5Pclose the result.
// Empty datasets (n == 0) still get a chunk-size of 1 so the DCPL is valid.
static hid_t makeLZ4Dcpl(hsize_t n) {
  hid_t   dcpl  = H5Pcreate(H5P_DATASET_CREATE);
  hsize_t chunk = n > 0 ? n : 1;
  H5Pset_chunk(dcpl, 1, &chunk);
  H5Pset_filter(dcpl, H5Z_LZ4[0].id, H5Z_FLAG_OPTIONAL, 0, nullptr);
  return dcpl;
}

// Reads the binary tiled positions file and writes it back into an HDF5 file.
// Each tile (ti, tj) is written as a compound {x,y,z} dataset at
//   group_path / datasets_json[ti*cols+tj]
EMSCRIPTEN_KEEPALIVE
int binary_to_hdf5(const char* bin_vfs_path,
                   const char* datasets_json,
                   int rows, int cols,
                   const char* group_path,
                   const char* out_h5_path) {
  try {
    std::ifstream fin(bin_vfs_path ? bin_vfs_path : "/in.bin", std::ios::binary);
    if (!fin) return -1;

    auto        names = parseJsonStringArray(datasets_json);
    std::string gpath = group_path ? group_path : "/";
    if (gpath.size() > 1 && gpath.back() == '/') gpath.pop_back();

    hid_t out_file = H5Fcreate(out_h5_path ? out_h5_path : "/out.h5",
                                H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (out_file < 0) return -1;

    if (gpath != "/")
      ensureGroups(out_file, {gpath});

    hid_t xyz_type = H5Tcreate(H5T_COMPOUND, sizeof(XYZ));
    H5Tinsert(xyz_type, "x", 0,               H5T_NATIVE_DOUBLE);
    H5Tinsert(xyz_type, "y", sizeof(double),   H5T_NATIVE_DOUBLE);
    H5Tinsert(xyz_type, "z", 2*sizeof(double), H5T_NATIVE_DOUBLE);

    GridIndex gi; int amount; std::vector<XYZ> pts;
    while (readTileBin(fin, gi, amount, pts)) {
      int idx = gi.x * cols + gi.y;
      if (idx < 0 || idx >= (int)names.size()) continue;
      std::string ds_path = gpath + "/" + names[idx];
      hsize_t n = (hsize_t)pts.size();
      hid_t space = H5Screate_simple(1, &n, nullptr);
      hid_t cprop = makeLZ4Dcpl(n);
      hid_t dset  = H5Dcreate2(out_file, ds_path.c_str(), xyz_type,
                                space, H5P_DEFAULT, cprop, H5P_DEFAULT);
      if (dset >= 0) {
        if (n > 0)
          H5Dwrite(dset, xyz_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, pts.data());
        H5Dclose(dset);
      }
      H5Sclose(space);
      H5Pclose(cprop);
    }

    H5Tclose(xyz_type);
    H5Fclose(out_file);
    return 0;
  } catch (...) {
    return -1;
  }
}

// Reads the binary tiled shape-factor file and writes it back into an HDF5 file.
// Each tile (ti, tj) XYZ triplet is split as (L, P, S) and written to
//   sf_parent_path / grp_l / ds_name
//   sf_parent_path / grp_p / ds_name
//   sf_parent_path / grp_s / ds_name
EMSCRIPTEN_KEEPALIVE
int binary_sf_to_hdf5(const char* bin_vfs_path,
                       const char* datasets_json,
                       int rows, int cols,
                       const char* sf_parent_path,
                       const char* grp_l, const char* grp_p, const char* grp_s,
                       const char* out_h5_path) {
  try {
    std::ifstream fin(bin_vfs_path ? bin_vfs_path : "/in.bin", std::ios::binary);
    if (!fin) return -1;

    auto        names = parseJsonStringArray(datasets_json);
    std::string sfp   = sf_parent_path ? sf_parent_path : "/";
    if (sfp.size() > 1 && sfp.back() == '/') sfp.pop_back();
    std::string gl = grp_l ? grp_l : "Linear";
    std::string gp = grp_p ? grp_p : "Planarity";
    std::string gs = grp_s ? grp_s : "Spherical";

    hid_t out_file = H5Fcreate(out_h5_path ? out_h5_path : "/sf_out.h5",
                                H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (out_file < 0) return -1;

    ensureGroups(out_file, {sfp + "/" + gl, sfp + "/" + gp, sfp + "/" + gs});

    auto writeDs = [&](const std::string& grpname, const std::string& ds,
                       const std::vector<double>& data) {
      hsize_t n    = (hsize_t)data.size();
      std::string path = sfp + "/" + grpname + "/" + ds;
      hid_t space = H5Screate_simple(1, &n, nullptr);
      hid_t cprop = makeLZ4Dcpl(n);
      hid_t dset  = H5Dcreate2(out_file, path.c_str(), H5T_NATIVE_DOUBLE,
                                space, H5P_DEFAULT, cprop, H5P_DEFAULT);
      if (dset >= 0) {
        if (n > 0)
          H5Dwrite(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());
        H5Dclose(dset);
      }
      H5Sclose(space);
      H5Pclose(cprop);
    };

    GridIndex gi; int amount; std::vector<XYZ> pts;
    while (readTileBin(fin, gi, amount, pts)) {
      int idx = gi.x * cols + gi.y;
      if (idx < 0 || idx >= (int)names.size()) continue;
      const std::string& ds = names[idx];
      size_t n = pts.size();
      std::vector<double> L(n), P(n), S(n);
      for (size_t k = 0; k < n; k++) { L[k] = pts[k].x; P[k] = pts[k].y; S[k] = pts[k].z; }
      writeDs(gl, ds, L);
      writeDs(gp, ds, P);
      writeDs(gs, ds, S);
    }

    H5Fclose(out_file);
    return 0;
  } catch (...) {
    return -1;
  }
}

// Writes positions .bin and/or shape-factor .bin into a single HDF5 file.
// Pass "" for pos_bin_vfs or sf_bin_vfs to skip that source.
// Returns the number of sources successfully written (0, 1, or 2), or -1 on error.
EMSCRIPTEN_KEEPALIVE
int convert_bins_to_hdf5(
    const char* pos_bin_vfs,
    const char* sf_bin_vfs,
    const char* datasets_json,
    int rows, int cols,
    const char* pos_group_path,
    const char* sf_parent_path,
    const char* grp_l, const char* grp_p, const char* grp_s,
    const char* out_h5_path) {
  try {
    auto names = parseJsonStringArray(datasets_json);
    if (names.empty()) return -1;

    hid_t out_file = H5Fcreate(out_h5_path ? out_h5_path : "/combined_out.h5",
                                H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (out_file < 0) return -1;

    int sources = 0;

    if (pos_bin_vfs && *pos_bin_vfs) {
      std::ifstream fin(pos_bin_vfs, std::ios::binary);
      if (fin) {
        std::string gpath = pos_group_path ? pos_group_path : "/Positions";
        if (gpath.size() > 1 && gpath.back() == '/') gpath.pop_back();
        if (gpath != "/") ensureGroups(out_file, {gpath});

        hid_t xyz_type = H5Tcreate(H5T_COMPOUND, sizeof(XYZ));
        H5Tinsert(xyz_type, "x", 0,               H5T_NATIVE_DOUBLE);
        H5Tinsert(xyz_type, "y", sizeof(double),   H5T_NATIVE_DOUBLE);
        H5Tinsert(xyz_type, "z", 2*sizeof(double), H5T_NATIVE_DOUBLE);

        GridIndex gi; int amount; std::vector<XYZ> pts;
        while (readTileBin(fin, gi, amount, pts)) {
          int idx = gi.x * cols + gi.y;
          if (idx < 0 || idx >= (int)names.size()) continue;
          std::string ds_path = gpath + "/" + names[idx];
          hsize_t n = (hsize_t)pts.size();
          hid_t space = H5Screate_simple(1, &n, nullptr);
          hid_t cprop = makeLZ4Dcpl(n);
          hid_t dset  = H5Dcreate2(out_file, ds_path.c_str(), xyz_type,
                                    space, H5P_DEFAULT, cprop, H5P_DEFAULT);
          if (dset >= 0) {
            if (n > 0) H5Dwrite(dset, xyz_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, pts.data());
            H5Dclose(dset);
          }
          H5Sclose(space);
          H5Pclose(cprop);
        }
        H5Tclose(xyz_type);
        ++sources;
      }
    }

    if (sf_bin_vfs && *sf_bin_vfs) {
      std::ifstream fin(sf_bin_vfs, std::ios::binary);
      if (fin) {
        std::string sfp = sf_parent_path ? sf_parent_path : "/";
        if (sfp.size() > 1 && sfp.back() == '/') sfp.pop_back();
        std::string gl  = grp_l ? grp_l : "Linear";
        std::string gps = grp_p ? grp_p : "Planarity";
        std::string gs  = grp_s ? grp_s : "Spherical";

        ensureGroups(out_file, {sfp + "/" + gl, sfp + "/" + gps, sfp + "/" + gs});

        auto writeDs = [&](const std::string& grpname, const std::string& ds,
                           const std::vector<double>& data) {
          hsize_t n = (hsize_t)data.size();
          std::string path = sfp + "/" + grpname + "/" + ds;
          hid_t space = H5Screate_simple(1, &n, nullptr);
          hid_t cprop = makeLZ4Dcpl(n);
          hid_t dset  = H5Dcreate2(out_file, path.c_str(), H5T_NATIVE_DOUBLE,
                                    space, H5P_DEFAULT, cprop, H5P_DEFAULT);
          if (dset >= 0) {
            if (n > 0) H5Dwrite(dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());
            H5Dclose(dset);
          }
          H5Sclose(space);
          H5Pclose(cprop);
        };

        GridIndex gi; int amount; std::vector<XYZ> pts;
        while (readTileBin(fin, gi, amount, pts)) {
          int idx = gi.x * cols + gi.y;
          if (idx < 0 || idx >= (int)names.size()) continue;
          const std::string& ds = names[idx];
          size_t n = pts.size();
          std::vector<double> L(n), P(n), S(n);
          for (size_t k = 0; k < n; k++) { L[k] = pts[k].x; P[k] = pts[k].y; S[k] = pts[k].z; }
          writeDs(gl, ds, L);
          writeDs(gps, ds, P);
          writeDs(gs, ds, S);
        }
        ++sources;
      }
    }

    H5Fclose(out_file);
    return sources;
  } catch (...) {
    return -1;
  }
}

// Returns the full HDF5 file structure as JSON using the same BFS traversal
// as the file explorer — each node is reached via its shortest path first.
// {"name":"/","path":"/","type":"group","children":[...]}
EMSCRIPTEN_KEEPALIVE
char* get_file_tree(const char* h5path) {
  hid_t file = H5Fopen(h5path ? h5path : "", H5F_ACC_RDONLY, H5P_DEFAULT);
  if (file < 0) return makeJsonResult("null");
  Node root;
  root.name    = "/";
  root.path    = "/";
  root.isGroup = true;
  buildTree(file, root);
  H5Fclose(file);
  std::ostringstream o;
  nodeToJson(root, o);
  return makeJsonResult(o.str());
}

// Scans a binary_io grid file and returns tile metadata as JSON without
// loading point data — uses seekg to skip each tile's payload.
// Returns: {"count":N,"rows":max_x+1,"cols":max_y+1}
EMSCRIPTEN_KEEPALIVE
char* scan_bin_tiles(const char* bin_vfs_path) {
  std::ifstream fin(bin_vfs_path ? bin_vfs_path : "", std::ios::binary);
  if (!fin) return makeJsonResult("{\"count\":0,\"rows\":0,\"cols\":0}");

  int max_x = -1, max_y = -1, count = 0;
  GridIndex gi; int amount;

  while (fin.read(reinterpret_cast<char*>(&gi), sizeof(GridIndex)) &&
         fin.read(reinterpret_cast<char*>(&amount), sizeof(int))) {
    if (amount < 0 || amount > 100'000'000) break;
    fin.seekg(static_cast<std::streamoff>(amount) *
              static_cast<std::streamoff>(sizeof(double) * 3), std::ios::cur);
    if (!fin) break;
    if (gi.x > max_x) max_x = gi.x;
    if (gi.y > max_y) max_y = gi.y;
    ++count;
  }

  std::ostringstream o;
  o << "{\"count\":" << count
    << ",\"rows\":"  << (max_x + 1)
    << ",\"cols\":"  << (max_y + 1) << "}";
  return makeJsonResult(o.str());
}

} // extern "C"
