add_executable(HeightMapVis HeightMapVis.cpp)
add_executable(GridVis GridVis.cpp)

target_link_libraries(HeightMapVis Open3D::Open3D)
target_link_libraries(GridVis Open3D::Open3D)

