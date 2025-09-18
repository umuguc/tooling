#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include <open3d/Open3D.h>
#include <string>
#include <vector>

#include "binary_io.hpp"

using Point = Eigen::Vector3d;

std::pair<std::vector<std::vector<std::vector<Point>>>,
          std::vector<std::vector<std::vector<Point>>>>
read_points_optimized(const std::string &tiles_file_name,
                      const std::string &shape_factors_file_name = "") {
  auto points = binary_io::read_grid_file<Point>(tiles_file_name);
  auto shape_factors =
      shape_factors_file_name.empty()
          ? std::vector<std::vector<std::vector<Point>>>()
          : binary_io::read_grid_file<Point>(shape_factors_file_name);
  return {points, shape_factors};
}

std::vector<Eigen::Vector3d> generate_distinct_colors(size_t num_colors) {
  std::vector<Eigen::Vector3d> colors;
  for (int i = 0; i < num_colors; ++i) {
    double hue = static_cast<double>(i) / num_colors;
    Eigen::Vector3d color = {(std::sin(hue * 2 * M_PI) + 1) / 2,
                             (std::sin((hue + 1.0 / 3) * 2 * M_PI) + 1) / 2,
                             (std::sin((hue + 2.0 / 3) * 2 * M_PI) + 1) / 2};
    colors.push_back(color);
  }
  return colors;
}

std::vector<Eigen::Vector3d>
flatten_points(const std::vector<std::vector<std::vector<Point>>> &points) {
  std::vector<Eigen::Vector3d> all_points;
  for (const auto &tile : points) {
    for (const auto &row : tile) {
      for (const auto &point : row) {
        all_points.push_back(point);
      }
    }
  }
  return all_points;
}

void set_colors(std::vector<Eigen::Vector3d> &all_colors,
                const std::vector<std::vector<std::vector<Point>>> &points,
                const std::vector<Eigen::Vector3d> &tile_colors,
                size_t num_tiles) {
  std::fill(all_colors.begin(), all_colors.end(), Eigen::Vector3d{0, 0, 0});
  size_t start_idx = 0;
  for (size_t i = 0; i < points.size(); ++i) {
    for (size_t j = 0; j < points[i].size(); ++j) {
      for (const auto &point : points[i][j]) {
        all_colors[start_idx++] =
            tile_colors[(i * points[i].size() + j) % num_tiles];
      }
    }
  }
}

void set_shape_factor_colors(
    std::vector<Eigen::Vector3d> &all_colors,
    const std::vector<Eigen::Vector3d> &all_shape_factors) {
  if (!all_shape_factors.empty()) {
    for (size_t i = 0; i < all_shape_factors.size(); ++i) {
      if (std::all_of(all_shape_factors[i].begin(), all_shape_factors[i].end(),
                      [](double v) { return v >= 0 && v <= 1; })) {
        all_colors[i] = all_shape_factors[i];
      }
    }
  }
}

void visualize_points_with_open3d(
    const std::pair<std::vector<std::vector<std::vector<Point>>>,
                    std::vector<std::vector<std::vector<Point>>>> &points) {
  auto &[p, s] = points;

  auto all_points = flatten_points(p);
  auto all_shape_factors = flatten_points(s);

  auto pcd = std::make_shared<open3d::geometry::PointCloud>();
  pcd->points_ = all_points;

  auto num_tiles = p.size() * p[0].size();
  auto tile_colors = generate_distinct_colors(num_tiles);

  std::vector<Eigen::Vector3d> all_colors(all_points.size(), {0, 0, 0});
  std::vector<Eigen::Vector3d> shape_factor_colors = all_colors;
  std::vector<Eigen::Vector3d> tile_colors_vec = all_colors;

  set_shape_factor_colors(shape_factor_colors, all_shape_factors);
  set_colors(tile_colors_vec, p, tile_colors, num_tiles);

  auto toggle_colors = [&](open3d::visualization::Visualizer *vis) {
    static int color_mode = 0;
    if (color_mode == 0) {
      std::cout << "Setting tile colors" << std::endl;
      pcd->colors_ = tile_colors_vec;
      color_mode = 1;
    } else if (color_mode == 1) {
      std::cout << "Setting shape factor colors" << std::endl;
      pcd->colors_ = shape_factor_colors;
      color_mode = 2;
    } else {
      std::cout << "Resetting to black colors" << std::endl;
      pcd->colors_ = std::vector<Eigen::Vector3d>(all_points.size(), {0, 0, 0});
      color_mode = 0;
    }
    vis->UpdateGeometry(pcd);
    return true;
  };

  std::cout << "Launching Open3D visualizer. Press 'C' to toggle colors."
            << std::endl;
  open3d::visualization::VisualizerWithKeyCallback vis;
  vis.CreateVisualizerWindow();
  vis.AddGeometry(pcd);
  vis.RegisterKeyCallback('C', toggle_colors);
  vis.PollEvents();
  vis.Run();
}

void save_images(
    const std::pair<std::vector<std::vector<std::vector<Point>>>,
                    std::vector<std::vector<std::vector<Point>>>> &points,
    const std::string &output_prefix) {
  const auto &[p, s] = points;

  auto all_points = flatten_points(p);
  auto all_shape_factors = flatten_points(s);

  auto pcd = std::make_shared<open3d::geometry::PointCloud>();
  pcd->points_ = all_points;

  auto num_tiles = p.size() * p[0].size();
  auto tile_colors = generate_distinct_colors(num_tiles);

  std::vector<Eigen::Vector3d> all_colors(all_points.size(), {0, 0, 0});
  set_shape_factor_colors(all_colors, all_shape_factors);

  open3d::visualization::Visualizer vis;
  vis.CreateVisualizerWindow("", 640, 480, 0, 0, false);
  vis.AddGeometry(pcd);

  // Save shape factor colors image
  pcd->colors_ = all_colors;
  vis.UpdateGeometry(pcd);
  vis.PollEvents();
  vis.CaptureScreenImage(output_prefix + "_shape_factors.png");

  // Save tile colors image
  set_colors(all_colors, p, tile_colors, num_tiles);
  pcd->colors_ = all_colors;
  vis.UpdateGeometry(pcd);
  vis.PollEvents();
  vis.CaptureScreenImage(output_prefix + "_tiles.png");

  vis.DestroyVisualizerWindow();
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " <tile_file> [shape_factor_file] [--interactive] "
                 "[--save-images <prefix>]"
              << std::endl;
    return 1;
  }

  std::string tile_file = argv[1];
  std::string shape_factor_file =
      (argc > 2 && argv[2][0] != '-') ? argv[2] : "";
  bool interactive = false;
  bool gui = false;
  std::string save_images_prefix;

  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--interactive") {
      interactive = true;
    } else if (std::string(argv[i]) == "--gui") {
      gui = true;
    } else if (std::string(argv[i]) == "--save-images" && i + 1 < argc) {
      save_images_prefix = argv[++i];
    }
  }

  auto points = read_points_optimized(tile_file, shape_factor_file);

  if (interactive) {
    visualize_points_with_open3d(points);
  } else if (gui) {
    std::cout << "GUI visualization is not supported yet." << std::endl;
  } else if (!save_images_prefix.empty()) {
    save_images(points, save_images_prefix);
  } else {
    std::cout << "No visualization option selected. Use --interactive to "
                 "visualize with Open3D or --save-images to save images."
              << std::endl;
  }

  return 0;
}