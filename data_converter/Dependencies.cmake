include(ExternalProject)

set(zlib_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/zlib CACHE PATH "" FORCE)

ExternalProject_Add(
    zlib_source
    GIT_REPOSITORY "https://github.com/madler/zlib.git"
    GIT_TAG "v1.3.1"
    CMAKE_ARGS 
        -DCMAKE_INSTALL_PREFIX=${zlib_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
    BUILD_BYPRODUCTS ${zlib_INSTALL_PREFIX}/lib/libz.a
)

message(STATUS "zlib_INSTALL_PREFIX: ${zlib_INSTALL_PREFIX}")

set(hdf5_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/hdf5)

ExternalProject_Add(
    hdf5_source
    GIT_REPOSITORY "https://github.com/HDFGroup/hdf5.git"
    GIT_TAG "hdf5_1.14.5"
    CMAKE_ARGS 
            -DCMAKE_INSTALL_PREFIX=${hdf5_INSTALL_PREFIX}
            -DCMAKE_BUILD_TYPE=Release
    CMAKE_CASHE_ARGS
            -DZLIB_LIBRARY:FILEPATH=${zlib_INSTALL_PREFIX}/lib/libz.so 
            -DZLIB_INCLUDE_DIR:PATH=${zlib_INSTALL_PREFIX}/include
            -DHDF5_BUILD_FORTRAN:BOOL=OFF
            -DHDF5_BUILD_GENERATORS:BOOL=OFF
            -DHDF5_BUILD_HL_LIB:BOOL=OFF
            -DHDF5_BUILD_CPP_LIB:BOOL=ON
            -DHDF5_ENABLE_Z_LIB_SUPPORT:BOOL=ON
            -DHDF5_ENABLE_SZIP_SUPPORT:BOOL=OFF
            -DHDF5_ENABLE_SZIP_ENCODING:BOOL=OFF
            -DHDF5_ENABLE_Z_LIB_SUPPORT:BOOL=ON
            -DHDF5_ALLOW_EXTERNAL_SUPPORT:STRING=NO
    BUILD_BYPRODUCTS ${hdf5_INSTALL_PREFIX}/lib/libhdf5.a ${hdf5_INSTALL_PREFIX}/lib/libhdf5_cpp.a
    DEPENDS zlib_source
)

add_library(zlib INTERFACE)
add_dependencies(zlib zlib_source)

set_target_properties(zlib PROPERTIES IMPORTED_LOCATION "${zlib_INSTALL_PREFIX}/lib/libz.a")
target_include_directories(zlib INTERFACE ${zlib_INSTALL_PREFIX}/include)

foreach(_lib hdf5 hdf5_cpp)
  add_library(${_lib} STATIC IMPORTED GLOBAL)
  add_dependencies(${_lib} hdf5_source)
  set_target_properties(${_lib} PROPERTIES
    IMPORTED_LOCATION ${hdf5_INSTALL_PREFIX}/lib/lib${_lib}.a)
endforeach()

# Create an interface library for hdf5 include directories
add_library(hdf5_includes INTERFACE)
add_dependencies(hdf5_includes hdf5_source)
# target_include_directories(hdf5_includes INTERFACE ${hdf5_INSTALL_PREFIX}/include)

# set_target_properties(hdf5_cpp PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${hdf5_INSTALL_PREFIX}/include)

target_link_libraries(hdf5 INTERFACE hdf5_cpp hdf5_includes)


# add_library(hdf5 INTERFACE IMPORTED GLOBAL)
# add_dependencies(hdf5 hdf5_source)

# set_target_properties(hdf5 PROPERTIES IMPORTED_LOCATION "${hdf5_INSTALL_PREFIX}/lib/libhdf5_cpp.a;${hdf5_INSTALL_PREFIX}/lib/libhdf5.a")
# set_target_properties(hdf5 PROPERTIES IMPORTED_LOCATION ${hdf5_INSTALL_PREFIX}/lib)
# target_include_directories(hdf5 INTERFACE ${hdf5_INSTALL_PREFIX}/include)

message("hdf5_INSTALL_PREFIX: ${hdf5_INSTALL_PREFIX}")

set(hdf5_plugins_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/src/hdf5_plugins)

ExternalProject_Add(
    hdf5_plugins
    GIT_REPOSITORY "https://github.com/HDFGroup/hdf5_plugins.git"
    GIT_TAG "hdf5-1.14.5"
    CMAKE_ARGS 
            -DCMAKE_INSTALL_PREFIX=${hdf5_plugins_INSTALL_PREFIX}
            -DCMAKE_BUILD_TYPE=Release 
    CMAKE_CACHE_ARGS 
            -DPL_PACKAGE_NAME:STRING=pl
            -DLZ4_PACKAGE_NAME:STRING=lz4
            -DH5PL_ALLOW_EXTERNAL_SUPPORT:STRING=NO 
            -DBUILD_LZ4_LIBRARY_SOURCE:BOOL=ON 
            -DBUILD_TESTING:BOOL=OFF 
            -DBUILD_EXAMPLES:BOOL=OFF 
            -DENABLE_BSHUF:BOOL=OFF
            -DENABLE_BLOSC:BOOL=OFF
            -DENABLE_BLOSC2:BOOL=OFF
            -DENABLE_BZIP2:BOOL=OFF
            -DENABLE_JPEG:BOOL=OFF
            -DENABLE_LZ4:BOOL=ON
            -DENABLE_LZF:BOOL=OFF
            -DENABLE_ZSTD:BOOL=OFF
            -DENABLE_ZFP:BOOL=OFF
            -DH5PL_CPACK_ENABLE:BOOL=OFF
            -DHDF5_DIR:PATH=${CMAKE_BINARY_DIR}/hdf5
            -DHDF5_ROOT:PATH=${CMAKE_BINARY_DIR}/hdf5
    DEPENDS hdf5_source
)

# add_library(hdf5_plugins INTERFACE)
# add_dependencies(hdf5_plugins hdf5_plugins_source)

# target_link_libraries(hdf5_plugins INTERFACE ${hdf5_plugins_INSTALL_PREFIX}/libh5lz4.so)
# target_include_directories(hdf5_plugins INTERFACE ${hdf5_plugins_INSTALL_PREFIX}/include)
