# Visualizer

This is the visualizer, which visualizes a point cloud in various ways.

## Requirements

This visualizer needs Open3D to show the point cloud.

The only requirements are a as follows:
* CMake (3.20)
* Clang with C++17 capabilities
* Open3D (v0.18)

> Disclaimer: There was a windows implementation but it was dropped because of inconveniences 

## Compilation and Execution

A sample compilation workflow would look similar to this:

```
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. 
ninja
```

Then call the program like this:

```
./src/GridVis <tile_file> [shape_factor_file] [--interactive] [--save-images <prefix>]
./src/HeightMapVis <filename> [--downsample]
```

## Applications

For this tool we implemented two applications:

* HeightMapVis: Takes in a point cloud file in simple binary and visualizes it. If the point cloud is big a downsample option can be used.
* GridVis: Takes in a grid file and optionally a shape factor file and visualizes this. 

## Browser Grid Visualizer

`wasm/web/` contains an in-browser version of GridVis (build it via `emcmake` in
`wasm/`, see `wasm/CMakeLists.txt`). No Open3D or native build required — it runs
the WASM module in a Web Worker, mounts uploaded files via WORKERFS for on-demand
reads, and streams parsed point-cloud data straight into WebGL buffers, so it
handles multi-million-point files without needing them fully resident in memory.

Because it uses a Web Worker, it must be served over HTTP — opening `index.html`
directly as a `file://` URL will not work. From `wasm/web/`:

```
python3 -m http.server 8000
```

then open `http://localhost:8000/`. See [wasm/web/README.md](./wasm/web/README.md)
for how to use it.



