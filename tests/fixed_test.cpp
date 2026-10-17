#include "lib/fixed.h"

#include "test.h"

using lib::Fixed;

TEST(FixedConversions) {
  EXPECT_EQ(Fixed::FromInt(3).raw(), 768);
  EXPECT_EQ(Fixed::FromRatio(1, 4).raw(), 64);
  EXPECT_EQ(Fixed::FromInt(-2).Floor(), -2);
  EXPECT_EQ(Fixed::FromRaw(-1).Floor(), -1);  // floor, not truncation
  EXPECT_EQ(Fixed::FromRaw(255).Floor(), 0);
}

TEST(FixedArithmetic) {
  Fixed a = Fixed::FromInt(2) + Fixed::FromRatio(1, 2);
  EXPECT_EQ(a.raw(), 640);
  a -= Fixed::FromInt(3);
  EXPECT_EQ(a.raw(), -128);
  EXPECT_EQ((Fixed::FromInt(2) * -3).raw(), Fixed::FromInt(-6).raw());
  EXPECT_TRUE(-Fixed::FromInt(1) < Fixed() && Fixed::FromRaw(1) > Fixed());
}
