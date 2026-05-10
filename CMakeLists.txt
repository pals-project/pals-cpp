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
  rapidyaml
  GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
  GIT_TAG v0.11.1 
  GIT_SHALLOW FALSE
)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
)

FetchContent_MakeAvailable(rapidyaml)
FetchContent_MakeAvailable(Catch2)

add_subdirectory(tests)

file(COPY ${CMAKE_SOURCE_DIR}/lattice_files/
     DESTINATION ${CMAKE_BINARY_DIR}/lattice_files/)

add_library(yaml_c_wrapper SHARED src/yaml_c_wrapper.cpp)
target_link_libraries(yaml_c_wrapper PUBLIC ryml)

add_executable(example_rw examples/example_rw.cpp)
target_link_libraries(example_rw yaml_c_wrapper ryml)

add_executable(get_lattices src/get_lattices.cpp)
target_link_libraries(get_lattices yaml_c_wrapper ryml)