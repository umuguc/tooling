#include <H5Cpp.h>
#include <array>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "binary_io.hpp"

struct CompoundData {
	double x;
	double y;
	double z;
};

struct Point {
	double x;
	double y;
	double z;
};

// Function to get dataset (assuming it returns an optional)
std::optional<H5::DataSet> getDataSet(H5::Group& group, const std::string& datasetPath) {
	if(!H5Lexists(group.getId(), datasetPath.c_str(), H5P_DEFAULT)) {
		std::cout << "Dataset does not exist at path: " << datasetPath << std::endl;
		return std::nullopt;
	}

	return group.openDataSet(datasetPath);
}

// Function to print dataset info
void printDataSetInfo(H5::DataSet& dataset) {
	H5::DataSpace dataspace = dataset.getSpace();
	int rank = dataspace.getSimpleExtentNdims();
	std::vector<hsize_t> dims(rank);
	dataspace.getSimpleExtentDims(dims.data());

	std::cout << "DataSpace: ";
	for(auto dim : dims) {
		std::cout << dim << " ";
	}
	std::cout << std::endl;

	H5::DataType datatype = dataset.getDataType();
	std::cout << "Data type: " << datatype.fromClass() << std::endl;
}

// Template function to loop through dataset
template <typename T>
void loopDataSet(H5::DataSet& dataset, std::function<void(T)> func) {
	H5::CompType datatype(sizeof(T));
	datatype.insertMember("x", HOFFSET(T, x), H5::PredType::NATIVE_DOUBLE);
	datatype.insertMember("y", HOFFSET(T, y), H5::PredType::NATIVE_DOUBLE);
	datatype.insertMember("z", HOFFSET(T, z), H5::PredType::NATIVE_DOUBLE);

	H5::DataSpace dataspace = dataset.getSpace();
	std::vector<T> data(dataspace.getSimpleExtentNpoints());
	// create new data type for compound data
	dataset.read(data.data(), datatype);

	for(auto d : data) {
		func(d);
	}
}

// Function to read attribute
template <typename T, size_t N>
void readAttribute(H5::DataSet& dataset, const std::string& attrName, std::array<T, N>& attrData) {
	H5::Attribute attribute = dataset.openAttribute(attrName);
	attribute.read(attribute.getDataType(), attrData.data());
}

// Function to write data to file
bool write_data_to_file(const std::string& datasetPath, H5::Group& group, const std::string& out_file) {
	auto dataset_opt = getDataSet(group, datasetPath);

	if(dataset_opt.has_value()) {
		auto dataset = dataset_opt.value();
		printDataSetInfo(dataset);

		// Read the attribute "Fiber::NumericalShift" which is an array of 192 elements
		std::array<double, 3> shift_value;
		readAttribute(dataset, "Fiber::NumericalShift", shift_value);

		std::vector<CompoundData> data;

		loopDataSet<CompoundData>(
		    dataset, [&data, &shift_value](CompoundData d) { data.push_back({d.x + shift_value[0], d.y + shift_value[1], d.z + shift_value[2]}); });

		binary_io::write_point_file(out_file, data, std::ios::app | std::ios::binary);
	}

	return true;
}

bool write_data_to_file(const std::vector<std::string>& datasetPath, H5::Group& group, const std::string& out_file) {
	for(const auto& path : datasetPath) {
		write_data_to_file(path, group, out_file);
	}

	return true;
}

// write grid file to file
bool write_grid_file(const std::vector<std::vector<std::string>>& datasetPath, H5::Group& group, const std::string& out_file) {
	std::vector<std::vector<std::vector<Point>>> data = std::vector<std::vector<std::vector<Point>>>();

	for(const auto& row : datasetPath) {
		data.push_back(std::vector<std::vector<Point>>());
		for(const auto& path : row) {
			auto dataset_opt = getDataSet(group, path);

			if(dataset_opt.has_value()) {
				auto dataset = dataset_opt.value();
				printDataSetInfo(dataset);

				// Read the attribute "Fiber::NumericalShift" which is an array of 192 elements
				std::array<double, 3> shift_value;
				readAttribute(dataset, "Fiber::NumericalShift", shift_value);

				std::vector<Point> points;
				loopDataSet<CompoundData>(
				    dataset, [&points, &shift_value](CompoundData d) { points.push_back({d.x + shift_value[0], d.y + shift_value[1], d.z + shift_value[2]}); });

				data.back().push_back(points);
			}
		}
	}

	binary_io::write_grid_file(out_file, data);

	std::vector<std::vector<std::vector<double>>> amountGrid(data.size(), std::vector<std::vector<double>>(data[0].size(), std::vector<double>(0)));

	binary_io::write_grid_file("out_shape.bin", amountGrid);

	return true;
}


// find recursively all groups with the name "Positions" be very careful with loops and append the group name to the old one shuch that we can have the full
// path check for infinite loops and stop if the subgroup was looked at already
std::vector<std::string> find_group_by_name_recursive(H5::Group& group, const std::string& name) {
	std::vector<std::string> group_names;

	hsize_t num_objs;
	num_objs = group.getNumObjs();

	for(int i = 0; i < num_objs; i++) {
		if(group.getObjTypeByIdx(i) == H5G_LINK) {
			continue;
		} else if(group.getObjTypeByIdx(i) == H5G_GROUP) {
			std::string obj_name = group.getObjnameByIdx(i);
			H5::Group subgroup = group.openGroup(obj_name);

			if(obj_name == name) { group_names.push_back(obj_name); }

			std::string path = group.getObjName();

			// split the path by "/"
			std::set<std::string> path_set;
			std::string delimiter = "/";
			size_t pos = 0;
			std::string token;

			while((pos = path.find(delimiter)) != std::string::npos) {
				token = path.substr(0, pos);
				path_set.insert(token);
				path.erase(0, pos + delimiter.length());
			}

			path_set.insert(path);

			if(path_set.find(obj_name) != path_set.end()) {
				// std::cout << "Infinite loop detected" << std::endl;
				continue;
			}


			std::vector<std::string> sub_group_names = find_group_by_name_recursive(subgroup, name);

			for(const auto& sub_group_name : sub_group_names) {
				group_names.push_back(obj_name + "/" + sub_group_name);
			}
		}
	}

	return group_names;
}

std::vector<std::string> get_all_datasets_in_group(H5::Group& group) {
	std::vector<std::string> dataset_names;

	hsize_t num_objs;
	num_objs = group.getNumObjs();

	for(int i = 0; i < num_objs; i++) {
		if(group.getObjTypeByIdx(i) == H5G_LINK) {
			continue;
		} else if(group.getObjTypeByIdx(i) == H5G_DATASET) {
			std::string obj_name = group.getObjnameByIdx(i);
			dataset_names.push_back(obj_name);
		}
	}

	return dataset_names;
}

int main(int argc, char* argv[]) {
	if(argc < 4) {
		std::cerr << "Usage: " << argv[0] << " <group_name> <output_file_name> <input_file1> [input_file2] ..." << std::endl;
		return 1;
	}

	std::string group_name = argv[1];
	std::string output_file_name = argv[2];

	for(int i = 3; i < argc; i++) {
		std::string file_name = argv[i];

		std::cout << "File name: " << file_name << std::endl;

		unsigned int plugin_flags = H5PL_ALL_PLUGIN;
		H5PLset_loading_state(plugin_flags);

		H5::H5File file(file_name, H5F_ACC_RDONLY);

		H5::Group root_group = file.openGroup("/");

		std::vector<std::string> group_names = find_group_by_name_recursive(root_group, group_name);

		for(int i = 0; i < group_names.size(); i++) {
			std::cout << "[" << i << "] " << "Group name: " << group_names[i] << std::endl;
		}

		int group_name;
		std::cout << "Enter the group id to extract: ";
		std::string group_name_str;
		std::getline(std::cin, group_name_str);
		group_name = std::stoi(group_name_str);

		H5::Group group = file.openGroup(group_names[group_name]);

		std::vector<std::string> dataset_names = get_all_datasets_in_group(group);

		for(int i = 0; i < dataset_names.size(); i++) {
			std::cout << "[" << i << "] " << "Dataset name: " << dataset_names[i] << std::endl;
		}

		// let the user select multiple datasets to extract
		std::set<int> dataset_paths;
		std::cout << "Enter the dataset id to extract (separated by space): ";
		std::string dataset_path;
		std::getline(std::cin, dataset_path);

		std::istringstream iss(dataset_path);
		int dataset_id;
		while(iss >> dataset_id) {
			dataset_paths.insert(dataset_id);
		}

		std::vector<std::string> selected_dataset_paths;
		for(const auto& path : dataset_paths) {
			selected_dataset_paths.push_back(dataset_names[path]);
		}

		write_data_to_file(selected_dataset_paths, group, output_file_name);
	}

	// write grid file can also be used like this:
	// the grid dimensions are given by the number of datasets in the first dimension and the number of datasets in the second dimension
	// how to divide the datasets into the grid is up to the user.
	// For testing purposes, this also writes a empty shape file to out_shape.bin.
	// write_grid_file({selected_dataset_paths}, group, "output_grid.bin");
	return 0;
}