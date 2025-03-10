#include <gtest/gtest.h>

TEST(Canary, TestIntegerOne_One) {
  constexpr int expected = 1;
  constexpr int actual = 1 * 1;
  ASSERT_EQ(expected, actual);
}
