#include <catch2/catch_test_macros.hpp>

int add(int a, int b) {
    return a + b;
}

TEST_CASE("Addition works", "[math]") {
    REQUIRE(add(2, 3) == 5);
    CHECK(add(-1, 1) == 0);
}