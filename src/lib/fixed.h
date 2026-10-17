// 24.8 signed fixed-point number: positions and velocities in 1/256 pixel steps, identical on host and target.
#pragma once

#include <stdint.h>

namespace lib {

class Fixed {
 public:
  static constexpr int kFractionBits = 8;
  static constexpr int32_t kOne = 1 << kFractionBits;

  constexpr Fixed() = default;

  static constexpr Fixed FromRaw(int32_t raw) {
    Fixed f;
    f.raw_ = raw;
    return f;
  }
  static constexpr Fixed FromInt(int32_t value) { return FromRaw(value * kOne); }
  // num / den, rounded toward zero: FromRatio(3, 10) is 0.3 (76/256).
  static constexpr Fixed FromRatio(int32_t num, int32_t den) { return FromRaw(num * kOne / den); }

  constexpr int32_t raw() const { return raw_; }
  // Largest integer <= value (arithmetic shift rounds toward negative infinity).
  constexpr int32_t Floor() const { return raw_ >> kFractionBits; }

  constexpr Fixed operator-() const { return FromRaw(-raw_); }
  constexpr Fixed operator+(Fixed o) const { return FromRaw(raw_ + o.raw_); }
  constexpr Fixed operator-(Fixed o) const { return FromRaw(raw_ - o.raw_); }
  constexpr Fixed operator*(int32_t k) const { return FromRaw(raw_ * k); }
  constexpr Fixed& operator+=(Fixed o) {
    raw_ += o.raw_;
    return *this;
  }
  constexpr Fixed& operator-=(Fixed o) {
    raw_ -= o.raw_;
    return *this;
  }

  constexpr bool operator==(const Fixed&) const = default;
  constexpr bool operator<(Fixed o) const { return raw_ < o.raw_; }
  constexpr bool operator>(Fixed o) const { return raw_ > o.raw_; }
  constexpr bool operator<=(Fixed o) const { return raw_ <= o.raw_; }
  constexpr bool operator>=(Fixed o) const { return raw_ >= o.raw_; }

 private:
  int32_t raw_ = 0;
};

}  // namespace lib
