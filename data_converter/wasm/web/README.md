# HDF5 Web Tools

A browser-based tool for exploring HDF5 files and converting between the HDF5
format and the UMUGUC binary grid format — no upload to a server, no install.
Everything runs client-side; your file never leaves your machine.

## Running it

This must be served over HTTP — opening `index.html` directly as a `file://`
URL will not work (browsers block the Web Worker it depends on). From this
directory:

```
python3 -m http.server 8000
```

then open `http://localhost:8000/` in a browser.

## Getting started

Drop a file onto the page (or click to browse). Supported extensions:
`.h5 .hdf5 .f5 .he5 .nc .bin`.

Once a file is loaded, up to four buttons appear depending on its type:

* **Explore Structure** — browse an HDF5 file's contents. (Hidden for `.bin` files.)
* **Convert to Binary** — export selected fields from an HDF5 file as one flat point-cloud binary. (Hidden for `.bin` files.)
* **Convert Tiles** — extract datasets from an HDF5 file into the tiled binary grid format (with optional shape factors). (Hidden for `.bin` files.)
* **Build HDF5** — assemble binary grid file(s) back into an HDF5 file.

Large files are supported — the tool reads directly from disk on demand rather
than loading the whole file into memory first, so multi-gigabyte files open
just as fast as small ones. Converting very large groups can take a little
longer since that does have to read the actual data; a status indicator shows
elapsed time while it works.

## Explore Structure

Click **Explore Structure** to open an interactive tree view of the file:

* The left sidebar lists every group and dataset. Click a dataset to see its
  type, shape, a preview of its values, and any attributes attached to it.
* If a dataset's value fits in the preview, a **Download CSV** button appears
  to export its full contents (not just the preview) as a `.csv` file.
* Click **← Back** to return and load a different file.

This is read-only — it doesn't modify or write anything.

## Convert to Binary

Click **Convert to Binary** for the simple case: one flat binary file with
every point from the fields you pick, one after another —

```
x y z
x y z
...
```

— just `(x, y, z)` doubles back to back, no headers, no tiling. This matches
what the native CLI converter (`src/converter`) produces.

1. **Find group by name** — type a group name (e.g. `Positions`, the
   default) and click **Search**. This looks for every group in the file
   whose name matches, however deep it's nested. You can also browse the
   file structure directly in the left sidebar and click **→ Use as group**
   on any group you select there instead of searching.
2. **Select a group** — pick one of the matching groups from the results.
3. **Select fields** — every dataset ("field") inside that group is listed
   and selected by default; uncheck any you don't want. The selection count
   is shown at the top right.
4. **Download** — click **Download points.bin** to get every point from
   every selected field, concatenated in selection order, as one file.

## Convert Tiles

Click **Convert Tiles** for the tiled/grid format instead — the same field
selection as above, but arranged into a `rows × cols` grid of named tiles
(with an optional parallel shape-factors export), matching the layout the
**Build HDF5** panel expects back:

1. **Find groups by name** — same as above.
2. **Select a group** — pick one of the matching groups from the results.
3. **Select datasets** — every dataset inside that group is listed and
   selected by default; uncheck any you don't want. The selection count is
   shown at the top right.
4. **Shape factors** *(optional)* — if the file also has linearity/planarity/
   sphericity data in parallel groups next to the positions, set the parent
   path (auto-filled from the positions group) and the three group names
   (defaults: `Linear`, `Planarity`, `Spherical`).
5. **Grid layout & download** — set how many rows/columns the selected
   datasets should be arranged into as tiles. Click any tile in the preview
   grid to mark it empty (`∅`, no data written) instead of assigning it the
   next dataset. Then:
   * **Download tiles.bin** — the positions grid file.
   * **Download sf.bin** — the shape factors grid file (only if step 4 was filled in).

## Build HDF5

Click **Build HDF5** to go the other way — pack one or two binary grid files
back into a single HDF5 file:

1. **Reference `.f5`** *(optional)* — load an existing HDF5 file just to
   borrow its group paths. Search it by group name, then click
   **→ Positions path** or **→ SF parent** on a result to fill in step 5
   below. (Or browse its structure in the sidebar and use the same buttons
   there.) This step is purely for convenience — it doesn't affect the output.
2. **Binary files** — drop your `tiles.bin` (positions) and/or `sf.bin`
   (shape factors) here. Dropping a file auto-detects its tile count and grid
   dimensions.
3. **Dataset names** — the name each tile gets as an HDF5 dataset. Click
   **Generate** to auto-fill `prefix_{row}x{col}` names from a prefix and
   count (auto-filled after step 2), or paste your own JSON array of names.
4. **Grid layout** — rows are set manually; columns are computed from the
   dataset count.
5. **Output paths** — where things land in the output file: the positions
   group path (default `/Positions`), the shape-factors parent path
   (default `/`), and the three shape-factor group names.

Set an output filename and click **Download .h5**.

## Notes

* Nothing is uploaded anywhere — all processing happens in your browser.
* Closing the tab discards everything; there's no autosave or session
  persistence, so download your results before navigating away.
