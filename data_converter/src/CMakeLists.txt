add_executable(converter ./converter.cpp)

target_include_directories(converter PRIVATE
    ${hdf5_INSTALL_PREFIX}/include
)

target_link_libraries(converter PRIVATE
    hdf5_cpp
    hdf5
    ${zlib_INSTALL_PREFIX}/lib/libz.a
)

add_dependencies(converter hdf5 zlib hdf5_plugins)

add_custom_command(TARGET converter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${hdf5_plugins_INSTALL_PREFIX}/lib/plugin/libh5lz4.so $<TARGET_FILE_DIR:converter>
)
