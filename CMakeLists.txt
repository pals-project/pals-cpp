cmake_minimum_required(VERSION 3.16)
project(YAMLWrapper)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()
include(FetchContent)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
)

FetchContent_MakeAvailable(yaml-cpp)
FetchContent_MakeAvailable(Catch2)
add_subdirectory(tests)

file(COPY ${CMAKE_SOURCE_DIR}/lattice_files/
     DESTINATION ${CMAKE_BINARY_DIR}/lattice_files/)

# if(APPLE)
#     # Check for package managers
#     execute_process(COMMAND which brew OUTPUT_VARIABLE HOMEBREW_EXISTS OUTPUT_STRIP_TRAILING_WHITESPACE)
#     execute_process(COMMAND which port OUTPUT_VARIABLE MACPORTS_EXISTS OUTPUT_STRIP_TRAILING_WHITESPACE)
#     execute_process(COMMAND which conda OUTPUT_VARIABLE CONDA_EXISTS OUTPUT_STRIP_TRAILING_WHITESPACE)
    
#     # Error if both Homebrew and MacPorts exist
#     if(HOMEBREW_EXISTS AND MACPORTS_EXISTS)
#         message(FATAL_ERROR 
#             "Both Homebrew and MacPorts detected. This can cause conflicts.\n"
#             "Please use only one package manager:\n"
#             "  - Homebrew found at: ${HOMEBREW_EXISTS}\n"
#             "  - MacPorts found at: ${MACPORTS_EXISTS}\n"
#             "Consider uninstalling one to avoid library conflicts."
#         )
#     endif()
    
#     # Set RPATH based on which package manager is found
#     set(BASE_RPATH "@executable_path/../lib;@loader_path")
    
#     if(HOMEBREW_EXISTS)
#         message(STATUS "Using Homebrew package manager")
#         # Check if Apple Silicon or Intel
#         execute_process(
#             COMMAND uname -m
#             OUTPUT_VARIABLE ARCH
#             OUTPUT_STRIP_TRAILING_WHITESPACE
#         )
#         if(ARCH STREQUAL "arm64")
#             set(CMAKE_INSTALL_RPATH "${BASE_RPATH};/opt/homebrew/lib")
#             list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
#         else()
#             set(CMAKE_INSTALL_RPATH "${BASE_RPATH};/usr/local/lib")
#             list(APPEND CMAKE_PREFIX_PATH "/usr/local")
#         endif()
#     elseif(MACPORTS_EXISTS)
#         message(STATUS "Using MacPorts package manager")
#         set(CMAKE_INSTALL_RPATH "${BASE_RPATH};/opt/local/lib")
#         list(APPEND CMAKE_PREFIX_PATH "/opt/local")
#     elseif(CONDA_EXISTS)
#         message(STATUS "Using Conda package manager")
#         # Get conda prefix
#         execute_process(
#             COMMAND conda info --base
#             OUTPUT_VARIABLE CONDA_PREFIX
#             OUTPUT_STRIP_TRAILING_WHITESPACE
#         )
#         set(CMAKE_INSTALL_RPATH "${BASE_RPATH};${CONDA_PREFIX}/lib")
#         list(APPEND CMAKE_PREFIX_PATH "${CONDA_PREFIX}")
#     else()
#         message(STATUS "No package manager detected, using default paths")
#         set(CMAKE_INSTALL_RPATH "${BASE_RPATH};/usr/local/lib")
#     endif()
    
#     set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
#     message(STATUS "RPATH set to: ${CMAKE_INSTALL_RPATH}")
# endif()

add_library(yaml_c_wrapper SHARED src/yaml_c_wrapper.cpp)
target_link_libraries(yaml_c_wrapper PUBLIC yaml-cpp)

add_executable(yaml_reader examples/yaml_reader.cpp)
target_link_libraries(yaml_reader yaml_c_wrapper)
