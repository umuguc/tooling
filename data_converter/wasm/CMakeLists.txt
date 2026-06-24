cmake_minimum_required(VERSION 3.20)
project(hdf5_web_explorer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Guard: must be built with emcmake ─────────────────────────────────────────
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR
    "\nThis project must be configured with Emscripten:\n\n"
    "  mkdir build && cd build\n"
    "  emcmake cmake ..\n"
    "  cmake --build .\n\n"
    "Install the emsdk first: https://emscripten.org/docs/getting_started/downloads.html\n"
    "  git clone https://github.com/emscripten-core/emsdk.git\n"
    "  cd emsdk && ./emsdk install latest && ./emsdk activate latest\n"
    "  source ./emsdk_env.sh\n"
  )
endif()

include(ExternalProject)

# ── Re-use already-downloaded sources from the native build ───────────────────
# NATIVE_BUILD_DIR can be overridden from a parent super-build so that it
# points to wherever the native data_converter project was built.
set(NATIVE_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build"
    CACHE PATH "Build directory of the native data_converter (contains downloaded sources)")

set(HDF5_SRC "${NATIVE_BUILD_DIR}/hdf5_source-prefix/src/hdf5_source")

set(HDF5_INSTALL "${CMAKE_BINARY_DIR}/hdf5")

# ── zlib — use Emscripten's bundled port ──────────────────────────────────────
# embuilder pre-compiles the port into the Emscripten sysroot, so we never
# need to build zlib ourselves.  CMAKE_SYSROOT is set by the Emscripten
# toolchain file before this file is processed.
message(STATUS "Pre-building Emscripten zlib port via embuilder...")
execute_process(
    COMMAND embuilder build zlib
    RESULT_VARIABLE _embuilder_result
)
if(_embuilder_result)
    message(FATAL_ERROR "embuilder build zlib failed (exit ${_embuilder_result})")
endif()

set(EM_ZLIB_LIB "${EMSCRIPTEN_SYSROOT}/lib/wasm32-emscripten/libz.a")
set(EM_ZLIB_INC "${EMSCRIPTEN_SYSROOT}/include")

if(NOT EXISTS "${EM_ZLIB_LIB}")
    message(FATAL_ERROR
        "Emscripten zlib not found at ${EM_ZLIB_LIB} after embuilder run.")
endif()

# ── HDF5 (C + C++ API, no tools/tests/Fortran) ────────────────────────────────
ExternalProject_Add(hdf5_wasm
    SOURCE_DIR "${HDF5_SRC}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/hdf5_build"
    INSTALL_DIR "${HDF5_INSTALL}"
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_INSTALL_PREFIX=${HDF5_INSTALL}
        -DCMAKE_BUILD_TYPE=Release
        # Emscripten's fenv.h declares the functions but only defines FE_ALL_EXCEPT=0,
        # not the individual flag macros.  Define them as 0 so HDF5's float-init
        # code compiles; feclearexcept(0) is a no-op stub in WASM which is fine.
        "-DCMAKE_C_FLAGS=-fexceptions -DFE_INVALID=0 -DFE_DIVBYZERO=0 -DFE_OVERFLOW=0 -DFE_UNDERFLOW=0 -DFE_INEXACT=0"
        "-DCMAKE_CXX_FLAGS=-fexceptions -DFE_INVALID=0 -DFE_DIVBYZERO=0 -DFE_OVERFLOW=0 -DFE_UNDERFLOW=0 -DFE_INEXACT=0"
        -DBUILD_SHARED_LIBS=OFF
        -DHDF5_BUILD_FORTRAN=OFF
        -DHDF5_BUILD_GENERATORS=OFF
        -DHDF5_BUILD_HL_LIB=OFF
        -DHDF5_BUILD_CPP_LIB=ON
        -DHDF5_BUILD_TOOLS=OFF
        -DHDF5_BUILD_EXAMPLES=OFF
        -DBUILD_TESTING=OFF
        -DHDF5_ENABLE_PARALLEL=OFF
        -DHDF5_ENABLE_THREADSAFE=OFF
        -DHDF5_ENABLE_Z_LIB_SUPPORT=ON
        -DHDF5_ENABLE_SZIP_SUPPORT=OFF
        -DHDF5_ENABLE_SZIP_ENCODING=OFF
        -DZLIB_LIBRARY=${EM_ZLIB_LIB}
        -DZLIB_INCLUDE_DIR=${EM_ZLIB_INC}
        # Emscripten-specific: disable file locking (not supported in WASM)
        -DH5_HAVE_FLOCK=FALSE
        -DH5_HAVE_FCNTL=FALSE
    BUILD_BYPRODUCTS
        "${HDF5_INSTALL}/lib/libhdf5.a"
        "${HDF5_INSTALL}/lib/libhdf5_cpp.a"
)

# ── Imported targets ───────────────────────────────────────────────────────────
file(MAKE_DIRECTORY "${HDF5_INSTALL}/include")

# zlib lives in the Emscripten sysroot — already built, no ExternalProject dep
add_library(zlib_imported STATIC IMPORTED GLOBAL)
set_target_properties(zlib_imported PROPERTIES
    IMPORTED_LOCATION "${EM_ZLIB_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${EM_ZLIB_INC}"
)

foreach(_lib hdf5 hdf5_cpp)
    add_library(${_lib}_imported STATIC IMPORTED GLOBAL)
    add_dependencies(${_lib}_imported hdf5_wasm)
    set_target_properties(${_lib}_imported PROPERTIES
        IMPORTED_LOCATION "${HDF5_INSTALL}/lib/lib${_lib}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${HDF5_INSTALL}/include"
    )
endforeach()

# LZ4 filter plugin sources (already downloaded as part of the native build)
set(LZ4_PLUGIN_SRC
    "${NATIVE_BUILD_DIR}/hdf5_plugins-prefix/src/hdf5_plugins/LZ4/src")
set(LZ4_PLUGIN_CFG
    "${NATIVE_BUILD_DIR}/hdf5_plugins-prefix/src/hdf5_plugins-build/LZ4")

set(WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
add_subdirectory(src)
