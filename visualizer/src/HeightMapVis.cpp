#include <fstream>
#include <iostream>
#include <open3d/Open3D.h>
#include <string>
#include <vector>

using DataTY = double;

template <typename Point>
std::vector<Point> read_point_file(const std::string &filename) {
  std::vector<Point> points;

  std::ifstream binaryFile(filename, std::ios::binary);
  if (!binaryFile) {
    std::cerr << "Cannot open the binary file for reading." << std::endl;
    return points;
  }

  Point point;
  while (
      binaryFile.read(reinterpret_cast<char *>(&point), sizeof(DataTY) * 3)) {
    points.push_back(point);
  }

  return points;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <filename> [--downsample]"
              << std::endl;
    return 1;
  }

  std::string filename = argv[1];
  bool downsample = (argc > 2 && std::string(argv[2]) == "--downsample");

  try {
    std::vector<Eigen::Vector3d> points;
    if (filename.find(".bin") != std::string::npos) {
      points = read_point_file<Eigen::Vector3d>(filename);
    } else {
      std::cerr << "Unsupported file format. Only binary files are supported."
                << std::endl;
      return 1;
    }

    auto point_cloud = std::make_shared<open3d::geometry::PointCloud>();
    point_cloud->points_ = points;

    if (downsample) {
      std::cout << "Further downsampling point cloud for visualization..."
                << std::endl;
      point_cloud = point_cloud->VoxelDownSample(1.0);
      std::cout << "Point cloud size after further downsampling: "
                << point_cloud->points_.size() << std::endl;
    }

    std::cout
        << "Launching Open3D visualizer. Close the window to save the image..."
        << std::endl;
    open3d::visualization::Visualizer vis;
    vis.CreateVisualizerWindow();
    vis.AddGeometry(point_cloud);
    vis.Run();
    vis.DestroyVisualizerWindow();
    std::cout << "Visualizer window closed." << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "An error occurred during file reading or conversion: "
              << e.what() << std::endl;
    return 1;
  }

  return 0;
}