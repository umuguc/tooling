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
    -sINITIAL_MEMORY=268435456     # 256 MB (higher start = fewer grow-and-copy pauses)
    -sMAXIMUM_MEMORY=3758096384    # 3.5 GB — WORKERFS means the source file no longer
                                    # needs to be memory-resident, so nearly the whole
                                    # heap is available for conversion output. Left some
                                    # headroom below wasm32's hard 4GB address-space cap.
    -sDISABLE_EXCEPTION_CATCHING=0
    -lworkerfs.js                  # mounts the user's File in a Worker for on-demand
                                    # HDF5 reads, so opening a file doesn't require
                                    # loading it fully into the WASM heap first
    -sFORCE_FILESYSTEM=1           # keep WORKERFS in the build — nothing in the
                                    # compiled C++ calls it, only explorer-worker.js,
                                    # so JS-side DCE would otherwise strip it
    -sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,ccall
    -sEXPORTED_FUNCTIONS=_explore,_free_result,_find_groups,_get_datasets,_convert_to_binary_file,_convert_sf_to_binary_file,_convert_to_points_binary,_get_dataset_csv,_binary_to_hdf5,_binary_sf_to_hdf5,_convert_bins_to_hdf5,_scan_bin_tiles,_get_file_tree,_get_last_error
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
