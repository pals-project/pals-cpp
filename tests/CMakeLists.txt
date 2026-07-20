include(CTest)
add_executable(tests
  test_yaml_api.cpp
  test_expansion.cpp
  test_correspondence.cpp
  test_key_order.cpp
  test_matching.cpp
  test_parameters.cpp
  test_expressions.cpp
  test_multipass.cpp
  test_controllers.cpp
)

target_link_libraries(tests
  PRIVATE
  yaml_c_wrapper
  Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(tests)
target_compile_features(tests PRIVATE cxx_std_17)
