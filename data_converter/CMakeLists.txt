cmake_minimum_required(VERSION 3.20)

project(HDF5Project LANGUAGES CXX)

# Enable C++ standards
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_GENERATOR STREQUAL "Ninja")
  # Force colored warnings in Ninja's output, if the compiler has -fdiagnostics-color support.
  # Rationale in https://github.com/ninja-build/ninja/issues/814
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.cmake)

# Target setup
add_subdirectory(src)