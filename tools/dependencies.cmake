# Keep application libraries private: InkView loads its own firmware copies.
# Sources are pinned in thirdparty/versions.json; the build never downloads code.
set(SKIP_INSTALL_ALL ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build private static dependencies" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/zlib)
add_library(ZLIB::ZLIB ALIAS zlibstatic)
set(ZLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/zlib CACHE PATH "" FORCE)
set(ZLIB_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/thirdparty/zlib ${ZLIB_INCLUDE_DIR})
set(ZLIB_LIBRARY_RELEASE zlibstatic CACHE STRING "" FORCE)
set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_LIBRARIES zlibstatic)

set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/libpng)
add_library(PNG::PNG ALIAS png_static)
set(PNG_PNG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/libpng CACHE PATH "" FORCE)
set(PNG_INCLUDE_DIR ${CMAKE_BINARY_DIR}/thirdparty/libpng ${PNG_PNG_INCLUDE_DIR})
set(PNG_LIBRARY_RELEASE png_static CACHE STRING "" FORCE)
set(PNG_LIBRARY png_static)
set(PNG_LIBRARIES png_static)

# libjpeg-turbo explicitly requires a separate CMake project.
include(ExternalProject)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/thirdparty/libjpeg)
ExternalProject_Add(jpeg_external
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/libjpeg
  BINARY_DIR ${CMAKE_BINARY_DIR}/thirdparty/libjpeg
  CMAKE_ARGS
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    "-DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}"
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON
    -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF
  BUILD_ALWAYS ON
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/thirdparty/libjpeg/libjpeg.a)
add_library(jpeg STATIC IMPORTED GLOBAL)
set_target_properties(jpeg PROPERTIES
  IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/thirdparty/libjpeg/libjpeg.a)
add_dependencies(jpeg jpeg_external)
set(JPEG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/libjpeg/src ${CMAKE_BINARY_DIR}/thirdparty/libjpeg)
set(JPEG_LIBRARIES jpeg)

set(FT_REQUIRE_PNG ON CACHE BOOL "" FORCE)
set(FT_REQUIRE_ZLIB ON CACHE BOOL "" FORCE)
# libbz2 of the firmware must not be needed: firmware 6.11 has no libbz2.so.1.0
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HVF ON CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/freetype)
set(FREETYPE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/freetype/include)
set(FREETYPE_LIBRARIES freetype)

# libcurl of the firmware is used (cr3gui links "curl"): a private curl would need the OpenSSL
# of one firmware generation and would not start on the other.

add_subdirectory(thirdparty/qimagescale)
