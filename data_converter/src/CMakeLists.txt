add_executable(converter ./converter.cpp)  # Replace "main.cpp" with your source files

# Add dependencies

target_include_directories(converter PRIVATE 
    ${hdf5_INSTALL_PREFIX}/include
)

target_link_libraries(converter PRIVATE
    ${zlib_INSTALL_PREFIX}/lib/libz.a
    hdf5
)

add_dependencies(converter hdf5 zlib hdf5_plugins)

add_custom_command(TARGET converter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${hdf5_plugins_INSTALL_PREFIX}/lib/plugin/libh5lz4.so $<TARGET_FILE_DIR:converter>
)
