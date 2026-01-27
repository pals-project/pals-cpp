include(CTest)
add_executable(tests
  test_yaml_c_wrapper.cpp
)

target_link_libraries(tests
  PRIVATE
  yaml_c_wrapper
  Catch2::Catch2WithMain
)

include(Catch)
Catch_discover_tests(tests)
target_compile_features(tests PRIVATE cxx_std_17)
