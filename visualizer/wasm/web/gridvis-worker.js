// Runs the point-cloud WASM module off the main thread.
//
// Files the user drops in are mounted via WORKERFS (only available inside a
// Worker, since it relies on FileReaderSync) instead of being read fully
// into a JS ArrayBuffer and copied into the WASM heap/MEMFS first. And the
// parsed vertex buffers (position/color) stream back to the main thread as
// small chunks — see js_emit_chunk in gridvis.cpp — instead of being
// base64-encoded into one giant HTML string, which is what let this module
// only handle small files before.
importScripts('gridvis.js');

const ModuleP = createGridVis();

// One WORKERFS mount per logical file slot (tiles / sf) so both can be
// live at once without name collisions.
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
      case 'visualize': {
        // visualize() emits pos/col_tile/col_sf chunks straight to us
        // (tagged by `kind`) as it parses — by the time ccall returns,
        // every chunk for this request has already been sent.
        const ptr = Module.ccall(
          'visualize', 'number',
          ['string', 'string', 'number'],
          [msg.tilesPath, msg.sfPath || '', id]
        );
        const json = Module.UTF8ToString(ptr);
        Module._free_result(ptr);
        const meta = JSON.parse(json);
        if (meta.error) {
          self.postMessage({ id, ok: false, error: meta.error });
        } else {
          self.postMessage({ id, ok: true, meta });
        }
        break;
      }
      default:
        self.postMessage({ id, ok: false, error: 'Unknown message type: ' + type });
    }
  } catch (err) {
    self.postMessage({ id, ok: false, error: (err && err.message) || String(err) });
  }
};
