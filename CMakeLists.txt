# set required cmake version
cmake_minimum_required(VERSION 3.19)

# This avoids googletest complaining that this (IPO) policy is not set
cmake_policy(SET CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

project(
  flatdd
  LANGUAGES CXX
  DESCRIPTION "FlatDD - A quantum simulator based on decision diagram gate matrices and array state vectors")

if (NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE Release)
    SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3")
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
endif()
message("Build type: " ${CMAKE_BUILD_TYPE})
set(CMAKE_CXX_FLAGS "-mavx")
# check whether `modulename` is correctly cloned in the `extern` directory.
macro(CHECK_SUBMODULE_PRESENT modulename)
  if(NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/${modulename}/CMakeLists.txt")
    message(
      FATAL_ERROR
        "${modulename} submodule not cloned properly. \
        Please run `git submodule update --init --recursive` \
        from the main project directory")
  endif()
endmacro()

find_package(CUDA REQUIRED)

include_directories(${CUDA_INCLUDE_DIRS})
message(STATUS "CUDA_INCLUDE_DIRS: " ${CUDA_INCLUDE_DIRS})
# check_submodule_present(cxxopts)
check_submodule_present(taskflow)

add_subdirectory(src)
add_subdirectory(apps)
# add_subdirectory(extern/MemoryAllocator.KanameShiki)
# add_subdirectory(extern/cxxopts)
# add_subdirectory(extern/taskflow)
if(NOT BUILD_DDSIM_TESTS)
  set(BUILD_MQT_CORE_TESTS
      OFF
      CACHE BOOL "")
endif()

# if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
#   check_submodule_present(cxxopts)
#   add_subdirectory(apps)
# endif()
