add_executable(gridvis gridvis.cpp)

target_compile_options(gridvis PRIVATE -std=c++17)

target_link_options(gridvis PRIVATE
    -sWASM=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864      # 64 MB
    -sMAXIMUM_MEMORY=1073741824    # 1 GB
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
