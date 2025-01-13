#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using DataTY = double;

namespace binary_io {

namespace detail {
struct GridIndex {
  int x, y;
};
} // namespace detail

template <typename Point, typename Grid, typename SizeRetriever>
void write_grid_file_internal(const std::string &filename, const Grid &grid,
                              const std::pair<int, int> &xy,
                              const SizeRetriever &retrieve_size) {
  std::ofstream binaryFile(filename, std::ios::binary);
  if (!binaryFile) {
    std::cerr << "Cannot open the binary file for writing." << std::endl;
    return;
  }

  for (size_t i = 0; i < xy.first; i++) {
    for (size_t j = 0; j < xy.second; j++) {
      detail::GridIndex gridIndex;
      gridIndex.x = i;
      gridIndex.y = j;

      binaryFile.write(reinterpret_cast<char *>(&gridIndex),
                       sizeof(detail::GridIndex));
      int size = retrieve_size(i, j);
      binaryFile.write(reinterpret_cast<char *>(&size), sizeof(int));
      for (size_t k = 0; k < retrieve_size(i, j); k++) {
        const Point point = grid[i][j][k];
        binaryFile.write(reinterpret_cast<const char *>(&point),
                         sizeof(DataTY) * 3);
      }
    }
  }
}

template <typename Point, typename Grid, typename Sizes>
void write_grid_file(const std::string &filename, const Grid &grid,
                     const std::pair<int, int> &xy, Sizes &size) {
  write_grid_file_internal<Point>(filename, grid, xy,
                                  [&size](size_t i, size_t j) {
                                    return size[{i, j}];
                                  });
}

template <typename Point>
void write_grid_file(const std::string &filename,
                     const std::vector<std::vector<std::vector<Point>>> &grid) {
  write_grid_file_internal<Point>(
      filename, grid, {grid.size(), grid.empty() ? 0 : grid[0].size()},
      [&grid](size_t i, size_t j) { return grid[i][j].size(); });
}

template <typename Point>
std::vector<std::vector<std::vector<Point>>>
read_grid_file(const std::string &filename) {
  std::vector<std::vector<std::vector<Point>>> grid{};

  std::ifstream binaryFile(filename, std::ios::binary);
  if (!binaryFile) {
    std::cerr << "Cannot open the binary file for reading." << std::endl;
    return grid;
  }

  while (!binaryFile.eof()) {
    int amount;
    detail::GridIndex gridIndex;
    binaryFile.read(reinterpret_cast<char *>(&gridIndex),
                    sizeof(detail::GridIndex));
    binaryFile.read(reinterpret_cast<char *>(&amount),
                    sizeof(int)); // Read the closing parenthesis

    if (static_cast<size_t>(gridIndex.x) >= grid.size()) {
      grid.push_back(std::vector<std::vector<Point>>());
    }

    if (static_cast<size_t>(gridIndex.y) >= grid[gridIndex.x].size()) {
      grid[gridIndex.x].push_back(std::vector<Point>());
    }

    for (int i = 0; i < amount; i++) {
      Point point;
      binaryFile.read(reinterpret_cast<char *>(&point), sizeof(DataTY) * 3);

      grid[gridIndex.x][gridIndex.y].push_back(point);
    }
  }

  return grid;
}

template <typename Point, typename PointGrid, typename AmountGrid>
void read_grid_file(const std::string &filename, PointGrid &grid,
                    AmountGrid &amountGrid) {
  std::ifstream binaryFile(filename, std::ios::binary);
  if (!binaryFile) {
    std::cerr << "Cannot open the binary file for reading." << std::endl;
    return;
  }

  while (!binaryFile.eof()) {
    int amount;
    detail::GridIndex gridIndex;
    binaryFile.read(reinterpret_cast<char *>(&gridIndex),
                    sizeof(detail::GridIndex));
    binaryFile.read(reinterpret_cast<char *>(&amount),
                    sizeof(int)); // Read the closing parenthesis

    amountGrid[gridIndex.x][gridIndex.y] = amount;

    for (int i = 0; i < amount; i++) {
      Point point;
      binaryFile.read(reinterpret_cast<char *>(&point), sizeof(DataTY) * 3);

      grid[gridIndex.x][gridIndex.y][i] = point;
    }
  }

  return;
}

template <typename Point>
void write_point_file(const std::string &filename,
                      const std::vector<Point> &points) {
  std::ofstream binaryFile(filename, std::ios::binary);
  if (!binaryFile) {
    std::cerr << "Cannot open the binary file for writing." << std::endl;
    return;
  }

  for (const Point &point : points) {
    binaryFile.write(reinterpret_cast<const char *>(&point),
                     sizeof(DataTY) * 3);
  }
}

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

} // namespace binary_io