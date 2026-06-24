cmake_minimum_required(VERSION 3.20)
project(gridvis_wasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR
    "\nThis project must be configured with Emscripten:\n\n"
    "  mkdir build && cd build\n"
    "  emcmake cmake ..\n"
    "  cmake --build .\n\n"
    "Install emsdk: https://emscripten.org/docs/getting_started/downloads.html\n"
  )
endif()

set(WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
add_subdirectory(src)
