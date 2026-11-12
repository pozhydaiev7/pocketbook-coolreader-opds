# CMake toolchain file for building Pocketbook Free SDK software using

# this one is important
SET(CMAKE_SYSTEM_NAME Linux)
#this one not so much
SET(CMAKE_SYSTEM_VERSION 1)
# libjpeg-turbo chooses its code by the processor
SET(CMAKE_SYSTEM_PROCESSOR arm)


# the SDK is next to the folder of the sources: <PBDEV>/SDK_481, <PBDEV>/sources/<this repository>
SET (TOOLCHAIN_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../SDK_481")
SET (TOOLCHAIN_PREFIX "arm-obreey-linux-gnueabi")
SET (TOOLCHAIN_INSTALL "sysroot/usr")
ADD_DEFINITIONS(-DPLATFORM_FC)


# specify the cross compiler
SET(CMAKE_INSTALL_PREFIX "${TOOLCHAIN_PATH}/${TOOLCHAIN_PREFIX}/${TOOLCHAIN_INSTALL}" CACHE PATH "Install path prefix" FORCE)

SET (CMAKE_C_COMPILER ${TOOLCHAIN_PATH}/bin/${TOOLCHAIN_PREFIX}-gcc)
SET (CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/bin/${TOOLCHAIN_PREFIX}-g++)
SET (CMAKE_LINK ${TOOLCHAIN_PATH}/bin/${TOOLCHAIN_PREFIX}-g++)
SET (CMAKE_ARR ${TOOLCHAIN_PATH}/bin/${TOOLCHAIN_PREFIX}-arr)
SET (CMAKE_STRIP ${TOOLCHAIN_PATH}/bin/${TOOLCHAIN_PREFIX}-strip)

# where is the target environment 
SET(CMAKE_FIND_ROOT_PATH  ${TOOLCHAIN_PATH}/${TOOLCHAIN_PREFIX}/${TOOLCHAIN_INSTALL})

include_directories(${TOOLCHAIN_PATH}/include/c++/4.1.2)
include_directories(${TOOLCHAIN_PATH}/include)

# search for programs in the build host directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
