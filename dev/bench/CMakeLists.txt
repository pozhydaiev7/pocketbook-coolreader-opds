# Test bench of the CoolReader engine (CREngine 3.2) on a PC. This file is put in the root of a
# copy of the source tree instead of its CMakeLists.txt: the engine and the bundled libraries are
# built the way the PocketBook pro5 build does, with bench/bench.cpp instead of the PocketBook GUI.
cmake_minimum_required(VERSION 3.18)
project(cr3bench C CXX)

if(NOT DEFINED DOC_BUFFER_SIZE)
  set(DOC_BUFFER_SIZE 0xA00000)
endif()
set(CMAKE_BUILD_TYPE Release)
set(GUI BENCH)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w -fcommon")
# The engine tests "this == NULL" in its node methods: a new compiler removes such tests
# unless told not to. Aliasing and overflow are kept the way the old compiler treated them.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 -w -fno-delete-null-pointer-checks -fno-strict-aliasing -fwrapv")

if(BENCH_PROF)
  # gprof: call counts and time by function
  # -g: gprof -l gives the time by source line
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg -g")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg -g -fno-inline-functions")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
endif()

if(BENCH_G)
  # debug information only: the code is the same as in the usual build. For BENCH_SAMPLE
  # (sample_report.py names the functions and the inlined ones by it)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
endif()

add_definitions(
  -DLBOOK=1 -DCR_POCKETBOOK=1 -DLINUX=1 -D_LINUX=1 -DNDEBUG=1
  -DCHM_SUPPORT_ENABLED=1 -DENABLE_ANTIWORD=1 -DCR3_ANTIWORD_PATCH=1
  -DCOLOR_BACKBUFFER=0 -DGRAY_BACKBUFFER_BITS=2
  -DDOC_BUFFER_SIZE=${DOC_BUFFER_SIZE} -DDOC_DATA_COMPRESSION_LEVEL=1
  -DMAX_IMAGE_SCALE_MUL=2 -DBIG_PAGE_MARGINS=0 -DUSE_EXTERNAL_EDICT_DICTIONARY=0
  -DLDOM_USE_OWN_MEM_MAN=0 -DCR_INTERNAL_PAGE_ORIENTATION=0
  -DCR_USE_INVERT_FOR_SELECTION_MARKS=1 -DENABLE_UPDATE_MODE_SETTING=1 -DENABLE_DBUS_VIEWER_EVENTS=0)

include(tools/dependencies.cmake)
add_subdirectory(thirdparty/chmlib)
add_subdirectory(thirdparty/antiword)
# old C code
set_target_properties(chmlib antiword PROPERTIES COMPILE_FLAGS "-std=gnu89")

include_directories(
  ${PNG_INCLUDE_DIR} ${JPEG_INCLUDE_DIR} ${ZLIB_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS}
  thirdparty/chmlib/src thirdparty/antiword crengine/include tinydict)
add_subdirectory(crengine)

add_executable(cr3bench bench/bench.cpp)
target_link_libraries(cr3bench crengine antiword chmlib ${FREETYPE_LIBRARIES} ${PNG_LIBRARIES}
  ${JPEG_LIBRARIES} ${ZLIB_LIBRARIES} qimagescale)
