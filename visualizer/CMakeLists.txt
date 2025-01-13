cmake_minimum_required(VERSION 3.24.0)
set (CMAKE_CXX_STANDARD 17)

project(Visualizer)

if(POLICY CMP0091)
    # https://stackoverflow.com/a/56490614
    cmake_policy(SET CMP0091 NEW)
endif()

find_package(Open3D)

if (NOT OPEN3D_FOUND)
    include(FetchContent)
    FetchContent_Declare (
        open3d
        GIT_REPOSITORY https://github.com/isl-org/Open3D.git
        GIT_TAG v0.19.0
    )

    SET(BUILD_PYTHON_MODULE OFF)
    SET(BUILD_WEBRTC OFF)
    SET(BUILD_EXAMPLES OFF)

    FetchContent_MakeAvailable(open3d)
endif()

add_subdirectory(src)

