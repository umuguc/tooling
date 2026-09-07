# Grid Visualizer (Web)

An in-browser 3D viewer for the binary grid point-cloud format (`tiles.bin` /
`sf.bin`) — no upload to a server, no install. Everything runs client-side;
your files never leave your machine.

## Running it

This must be served over HTTP — opening `index.html` directly as a `file://`
URL will not work (browsers block the Web Worker it depends on). From this
directory:

```
python3 -m http.server 8000
```

then open `http://localhost:8000/` in a browser.

## Using it

1. Drop a **tiles file** (required) — the positions grid file, e.g. the
   `tiles.bin` produced by the [data converter's web tool's "Convert
   Tiles" mode](../../../data_converter/wasm/web/README.md#convert-tiles)
   (not its "Convert to Binary" mode — that produces a flat point list with
   no tile framing, which this viewer can't parse).
2. Optionally drop a **shape factors file** — a parallel grid file (same
   tile layout) whose (x, y, z) values are interpreted as RGB colors in
   `[0, 1]`, giving you a second coloring mode.
3. Click **Visualize**.

Don't have a file handy? Click **Preload sample data (Stanford bunny)**
instead — it fetches the bundled `data/bunny_tiles.bin` and
`data/bunny_shape.bin` and visualizes them immediately, no drop required.

Once loaded you get an interactive 3D point-cloud viewer:

* **Drag** to rotate, **right-drag / two-finger drag** to pan, **scroll /
  pinch** to zoom.
* **Toggle Colors** switches between the tile-based coloring (each tile gets
  a distinct hue) and the shape-factors coloring, if a shape-factors file
  was loaded.
* The chip in the top-left shows the total point count and tile grid
  dimensions.

Large files are supported — file reading and point-cloud parsing happen off
the main thread, and the parsed vertex data streams back to the page in
small pieces rather than being embedded as one giant block, so multi-million
point clouds load without freezing the tab or running out of memory.

## Notes

* Nothing is uploaded anywhere — all processing happens in your browser.
* Closing the tab discards everything; there's no autosave or session
  persistence.
