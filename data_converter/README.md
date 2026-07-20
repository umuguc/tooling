# Data Converter

This is the converter which converts hdf5 to a plain binary format we can than use in the UMUGUC project.

## Requirements

This data converter is self contained building everything from scratch. All needed dependencies are downloaded, compiled and copied to the right place out of the box. 

The only requirements are a as follows:
* CMake (3.20)
* Clang with C++17 capabilities

> Disclaimer: This was only tested on linux so keep this in mind.

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
HDF5_PLUGIN_PATH="./src/" ./src/converter <group_name_of_pointcloud_dataset> <output_filename.bin> <input_filename.f5> [<input_filename.f5> ...]
```

## Browser HDF5 Explorer

`wasm/web/` contains an in-browser HDF5 explorer/converter (build it via `emcmake` in
`wasm/`, see `wasm/CMakeLists.txt`). It runs the WASM module in a Web Worker and mounts
uploaded files via WORKERFS for on-demand reads, so it can open files far larger than
the WASM heap without loading them fully into memory first.

Because it uses a Web Worker, it must be served over HTTP — opening `index.html`
directly as a `file://` URL will not work (browsers block Worker creation from file
origins). From `wasm/web/`:

```
python3 -m http.server 8000
```

then open `http://localhost:8000/`. See [wasm/web/README.md](./wasm/web/README.md) for how to use it.

## File format

The resulting file format looks like following (the input datatype is set to double, the output datatype can be easily changed in code):

* Type
```c++
std::vector<sycl::vec<double, 3>> Points;
```

* Binary:
```bin
x y z
x y z
...
```
