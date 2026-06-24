add_executable(explorer
    explorer.cpp
    ${LZ4_PLUGIN_SRC}/H5Zlz4.c
    ${LZ4_PLUGIN_SRC}/lib/lz4.c
)

add_dependencies(explorer hdf5_wasm)

target_include_directories(explorer PRIVATE
    "${HDF5_INSTALL}/include"
    "${LZ4_PLUGIN_SRC}/lib"
    "${LZ4_PLUGIN_CFG}"
)

target_link_libraries(explorer PRIVATE
    hdf5_cpp_imported
    hdf5_imported
    zlib_imported
)

target_compile_options(explorer PRIVATE -fexceptions)

target_link_options(explorer PRIVATE
    -fexceptions
    -sWASM=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864      # 64 MB
    -sMAXIMUM_MEMORY=1073741824    # 1 GB
    -sDISABLE_EXCEPTION_CATCHING=0
    -sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,ccall
    -sEXPORTED_FUNCTIONS=_explore,_free_result,_find_groups,_get_datasets,_convert_to_binary_file,_convert_sf_to_binary_file,_get_dataset_csv,_binary_to_hdf5,_binary_sf_to_hdf5,_convert_bins_to_hdf5,_scan_bin_tiles,_get_file_tree
    -sNO_EXIT_RUNTIME=1
    -sMODULARIZE=1
    -sEXPORT_NAME=createExplorer
    -sSINGLE_FILE=1
    --no-entry
)

# Output .js + .wasm directly into web/ so index.html can load them
set_target_properties(explorer PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${WEB_DIR}"
)
