cmake_minimum_required(VERSION 3.10)
project(YAMLWrapper)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(APPLE)
## add macports
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib;@loader_path;/usr/local/lib;/opt/homebrew/lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

find_package(yaml-cpp REQUIRED)

add_library(yaml_c_wrapper SHARED src/yaml_c_wrapper.cpp)
target_link_libraries(yaml_c_wrapper yaml-cpp)

add_executable(yaml_reader src/yaml_reader.cpp)
target_link_libraries(yaml_reader yaml-cpp)