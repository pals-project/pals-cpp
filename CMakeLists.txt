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

# PCRE2 provides the PCRE2 regex syntax used by the {beamline} and {element}
# components of lattice parameter name matching (see match_lattice_parameters).
FetchContent_Declare(
  pcre2
  GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git
  GIT_TAG pcre2-10.44
)
# Build only the 8-bit library, statically, and skip the tooling/tests so the
# regex code is embedded directly into libyaml_c_wrapper.
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT OFF CACHE BOOL "" FORCE)
set(PCRE2_STATIC_PIC ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(rapidyaml)
FetchContent_MakeAvailable(Catch2)
FetchContent_MakeAvailable(pcre2)

add_subdirectory(tests)

file(COPY ${CMAKE_SOURCE_DIR}/lattice_files/
     DESTINATION ${CMAKE_BINARY_DIR}/lattice_files/)

add_library(yaml_c_wrapper SHARED src/yaml_c_wrapper.cpp)
target_link_libraries(yaml_c_wrapper PUBLIC ryml PRIVATE pcre2-8-static)

add_executable(example_rw examples/example_rw.cpp)
target_link_libraries(example_rw yaml_c_wrapper ryml)

add_executable(print_lattices examples/print_lattices.cpp)
target_link_libraries(print_lattices yaml_c_wrapper ryml)