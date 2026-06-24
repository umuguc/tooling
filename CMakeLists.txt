cmake_minimum_required(VERSION 3.24)
project(Tooling NONE)

# ── Super-build: orchestrates all subprojects ─────────────────────────────────
#
# Native targets build with the host compiler; WASM targets require Emscripten
# and are only configured when emcmake is found on PATH.
#
# Quick-start:
#   mkdir build && cd build
#   cmake ..                   # configure (finds emcmake automatically)
#   cmake --build . --parallel # build everything
#
# CI example (separate jobs):
#   cmake --preset native      # or: cmake -B build/native ..
#   cmake --preset wasm        # or: emcmake cmake -B build/wasm ..  (not this file)
#
# See CMakePresets.json for named presets.
# ─────────────────────────────────────────────────────────────────────────────

include(ExternalProject)

set(_generator "${CMAKE_GENERATOR}")
set(_build_type "${CMAKE_BUILD_TYPE}")
if(NOT _build_type)
  set(_build_type "Release")
endif()

# ── Native: HDF5 converter ────────────────────────────────────────────────────
ExternalProject_Add(data_converter
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data_converter"
    BINARY_DIR "${CMAKE_BINARY_DIR}/data_converter"
    CMAKE_ARGS
        -G "${_generator}"
        -DCMAKE_BUILD_TYPE=${_build_type}
        -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
    INSTALL_COMMAND ""
    # Sources (zlib, hdf5, hdf5_plugins) are downloaded into this build dir;
    # the WASM data_converter build reuses them via NATIVE_BUILD_DIR below.
)

# ── Native: Open3D visualizer ─────────────────────────────────────────────────
ExternalProject_Add(visualizer
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/visualizer"
    BINARY_DIR "${CMAKE_BINARY_DIR}/visualizer"
    CMAKE_ARGS
        -G "${_generator}"
        -DCMAKE_BUILD_TYPE=${_build_type}
        -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
    INSTALL_COMMAND ""
)

# ── WASM targets (require Emscripten) ────────────────────────────────────────
find_program(EMCMAKE emcmake)

if(EMCMAKE)
    message(STATUS "emcmake found at ${EMCMAKE} — WASM targets enabled")

    # data_converter/wasm reuses sources downloaded by the native data_converter
    # build.  NATIVE_BUILD_DIR points there so the WASM CMakeLists can locate
    # the already-fetched zlib/hdf5/hdf5_plugins source trees.
    ExternalProject_Add(wasm_data_converter
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data_converter/wasm"
        BINARY_DIR "${CMAKE_BINARY_DIR}/wasm_data_converter"
        CONFIGURE_COMMAND
            "${EMCMAKE}" "${CMAKE_COMMAND}"
                -S "${CMAKE_CURRENT_SOURCE_DIR}/data_converter/wasm"
                -B "${CMAKE_BINARY_DIR}/wasm_data_converter"
                -DCMAKE_BUILD_TYPE=${_build_type}
                -DNATIVE_BUILD_DIR=${CMAKE_BINARY_DIR}/data_converter
        BUILD_COMMAND
            "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}/wasm_data_converter" --parallel
        INSTALL_COMMAND ""
        DEPENDS data_converter
    )

    # visualizer/wasm has no dependency on the native visualizer build
    ExternalProject_Add(wasm_visualizer
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/wasm"
        BINARY_DIR "${CMAKE_BINARY_DIR}/wasm_visualizer"
        CONFIGURE_COMMAND
            "${EMCMAKE}" "${CMAKE_COMMAND}"
                -S "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/wasm"
                -B "${CMAKE_BINARY_DIR}/wasm_visualizer"
                -DCMAKE_BUILD_TYPE=${_build_type}
        BUILD_COMMAND
            "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}/wasm_visualizer" --parallel
        INSTALL_COMMAND ""
    )
else()
    message(STATUS "emcmake not found — WASM targets skipped")
    message(STATUS "  Install emsdk: https://emscripten.org/docs/getting_started/downloads.html")
    message(STATUS "    git clone https://github.com/emscripten-core/emsdk.git")
    message(STATUS "    cd emsdk && ./emsdk install latest && ./emsdk activate latest")
    message(STATUS "    source ./emsdk_env.sh")
endif()
