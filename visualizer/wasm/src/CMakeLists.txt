add_executable(gridvis gridvis.cpp)

target_compile_options(gridvis PRIVATE -std=c++17)

target_link_options(gridvis PRIVATE
    -sWASM=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=268435456     # 256 MB (higher start = fewer grow-and-copy pauses)
    -sMAXIMUM_MEMORY=3758096384    # 3.5 GB — WORKERFS means the input file(s) never
                                    # need to be memory-resident, and vertex data streams
                                    # straight to the GPU instead of being buffered as one
                                    # base64 HTML string, so nearly the whole heap is just
                                    # for parsing. Left headroom below wasm32's 4GB cap.
    -lworkerfs.js                  # mounts the user's File(s) in a Worker for on-demand
                                    # reads, so opening a file doesn't require loading it
                                    # fully into the WASM heap/MEMFS first
    -sFORCE_FILESYSTEM=1           # keep WORKERFS in the build — nothing in the compiled
                                    # C++ calls it, only gridvis-worker.js, so JS-side DCE
                                    # would otherwise strip it
    -sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,ccall
    -sEXPORTED_FUNCTIONS=_visualize,_free_result
    -sNO_EXIT_RUNTIME=1
    -sMODULARIZE=1
    -sEXPORT_NAME=createGridVis
    -sSINGLE_FILE=1
    --no-entry
)

# Output gridvis.js (with embedded .wasm) directly into web/
set_target_properties(gridvis PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${WEB_DIR}"
)