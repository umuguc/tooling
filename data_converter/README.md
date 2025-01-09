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
