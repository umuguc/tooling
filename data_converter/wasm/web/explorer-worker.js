// Runs the HDF5 WASM module off the main thread.
//
// Files the user drops in are mounted via WORKERFS (only available inside a
// Worker, since it relies on FileReaderSync) instead of being read fully
// into a JS ArrayBuffer and copied into the WASM heap. WORKERFS serves reads
// lazily straight from the File object, so H5Fopen and tree traversal only
// pull in the bytes they actually touch (superblock, object headers, small
// previews) — a multi-GB file can be opened without ever needing to fit
// inside the WASM heap.
importScripts('explorer.js');

const ModuleP = createExplorer();

// One WORKERFS mount per logical file slot so several files (main upload,
// build-panel template, positions .bin, shape-factor .bin) can be live at
// once without name collisions.
const mounts = {}; // slot -> { dir, path }

async function mountFile(Module, slot, file) {
  await unmountFile(Module, slot);
  const dir = '/mnt_' + slot;
  Module.FS.mkdir(dir);
  Module.FS.mount(Module.FS.filesystems.WORKERFS, { files: [file] }, dir);
  const path = dir + '/' + file.name;
  mounts[slot] = { dir, path };
  return path;
}

async function unmountFile(Module, slot) {
  const m = mounts[slot];
  if (!m) return;
  try { Module.FS.unmount(m.dir); } catch (_) {}
  try { Module.FS.rmdir(m.dir); } catch (_) {}
  delete mounts[slot];
}

// Streams a temporary output file the C++ side wrote into MEMFS back to the
// main thread as a series of small transferable chunks, then deletes it.
// A multi-GB result read out (and postMessage'd) as one giant contiguous
// buffer can fail with "Array buffer allocation failed" even though the
// WASM heap itself is fine — that's a separate, browser-level allocation
// ceiling for a single ArrayBuffer. Reading + transferring in bounded
// pieces avoids ever needing one huge contiguous buffer anywhere, and the
// main thread reassembles the download via Blob, which doesn't need
// contiguous backing memory either.
const CHUNK_SIZE = 64 * 1024 * 1024; // 64MB

function streamOutputFile(Module, path, reqId) {
  let stream;
  try {
    stream = Module.FS.open(path, 'r');
  } catch (err) {
    return (err && err.message) || String(err);
  }
  try {
    const size = Module.FS.stat(path).size;
    let pos = 0;
    while (pos < size) {
      const len = Math.min(CHUNK_SIZE, size - pos);
      const chunk = new Uint8Array(len);
      Module.FS.read(stream, chunk, 0, len, pos);
      self.postMessage({ id: reqId, chunk }, [chunk.buffer]);
      pos += len;
    }
    return null;
  } catch (err) {
    return (err && err.message) || String(err);
  } finally {
    Module.FS.close(stream);
    try { Module.FS.unlink(path); } catch (_) {}
  }
}

// Fetches the reason the most recent convert_*/convert_bins_to_hdf5 call
// returned -1 (see g_last_error in explorer.cpp).
function getLastError(Module) {
  try {
    return callStringFn(Module, 'get_last_error', []) || null;
  } catch (_) {
    return null;
  }
}

function callStringFn(Module, fn, args) {
  const types = args.map(a => (typeof a === 'number' ? 'number' : 'string'));
  const ptr = Module.ccall(fn, 'number', types, args);
  const str = Module.UTF8ToString(ptr);
  Module._free_result(ptr);
  return str;
}

ModuleP.then(() => self.postMessage({ type: 'ready' }));

self.onmessage = async (ev) => {
  const msg = ev.data;
  const { id, type } = msg;
  let Module;
  try {
    Module = await ModuleP;
  } catch (err) {
    self.postMessage({ id, ok: false, error: 'WASM module failed to load: ' + err });
    return;
  }

  try {
    switch (type) {
      case 'mount': {
        const path = await mountFile(Module, msg.slot, msg.file);
        self.postMessage({ id, ok: true, path });
        break;
      }
      case 'unmount': {
        await unmountFile(Module, msg.slot);
        self.postMessage({ id, ok: true });
        break;
      }
      case 'explore': {
        const html = callStringFn(Module, 'explore', [msg.path]);
        self.postMessage({ id, ok: true, html });
        break;
      }
      case 'getFileTree': {
        const json = callStringFn(Module, 'get_file_tree', [msg.path]);
        self.postMessage({ id, ok: true, tree: JSON.parse(json) });
        break;
      }
      case 'findGroups': {
        const json = callStringFn(Module, 'find_groups', [msg.path, msg.groupName]);
        self.postMessage({ id, ok: true, groups: JSON.parse(json) });
        break;
      }
      case 'getDatasets': {
        const json = callStringFn(Module, 'get_datasets', [msg.path, msg.groupPath]);
        self.postMessage({ id, ok: true, datasets: JSON.parse(json) });
        break;
      }
      case 'getDatasetCsv': {
        const csv = callStringFn(Module, 'get_dataset_csv', [msg.path]);
        self.postMessage({ id, ok: true, csv });
        break;
      }
      case 'scanBinTiles': {
        const json = callStringFn(Module, 'scan_bin_tiles', [msg.path]);
        self.postMessage({ id, ok: true, info: JSON.parse(json) });
        break;
      }
      case 'convertToBinaryFile': {
        // convert_to_binary_file emits each tile straight to us via
        // js_emit_chunk (postMessage) as it computes it — by the time
        // ccall returns, every chunk for this request has already been
        // sent. No MEMFS file involved, so there's no separate read-back
        // step (and no buffer-size ceiling from one).
        const written = Module.ccall(
          'convert_to_binary_file', 'number',
          ['string', 'string', 'string', 'number', 'number', 'number'],
          [msg.path, msg.group, msg.gridJson, msg.rows, msg.cols, id]
        );
        const convertError = written < 0 ? getLastError(Module) : null;
        self.postMessage({ id, ok: true, written, readError: null, convertError });
        break;
      }
      case 'convertToPointsBinary': {
        // convert_to_points_binary emits raw (x,y,z) points with no tile
        // framing — a flat point-cloud binary, straight to the worker the
        // same way convertToBinaryFile does.
        const written = Module.ccall(
          'convert_to_points_binary', 'number',
          ['string', 'string', 'string', 'number'],
          [msg.path, msg.group, msg.gridJson, id]
        );
        const convertError = written < 0 ? getLastError(Module) : null;
        self.postMessage({ id, ok: true, written, readError: null, convertError });
        break;
      }
      case 'convertSfToBinaryFile': {
        const written = Module.ccall(
          'convert_sf_to_binary_file', 'number',
          ['string', 'string', 'number', 'number', 'string', 'string', 'string', 'string', 'number'],
          [msg.path, msg.gridJson, msg.rows, msg.cols, msg.sfParent, msg.grpL, msg.grpP, msg.grpS, id]
        );
        const convertError = written < 0 ? getLastError(Module) : null;
        self.postMessage({ id, ok: true, written, readError: null, convertError });
        break;
      }
      case 'convertBinsToHdf5': {
        const outPath = '/out_build_' + id + '.h5';
        const ret = Module.ccall(
          'convert_bins_to_hdf5', 'number',
          ['string', 'string', 'string', 'number', 'number', 'string', 'string', 'string', 'string', 'string', 'string'],
          [msg.posPath || '', msg.sfPath || '', msg.namesJson, msg.rows, msg.cols,
           msg.posGroupPath, msg.sfParentPath, msg.grpL, msg.grpP, msg.grpS, outPath]
        );
        const readError = ret > 0 ? streamOutputFile(Module, outPath, id) : null;
        const convertError = ret < 0 ? getLastError(Module) : null;
        self.postMessage({ id, ok: true, ret, readError, convertError });
        break;
      }
      default:
        self.postMessage({ id, ok: false, error: 'Unknown message type: ' + type });
    }
  } catch (err) {
    self.postMessage({ id, ok: false, error: (err && err.message) || String(err) });
  }
};
