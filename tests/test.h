// Minimal unit test harness for host tests.
//
//   TEST(Name) { EXPECT_EQ(1 + 1, 2); }
//
// Every TEST in the executable runs; the process exits non-zero if any expectation failed.
#pragma once

#include <cstring>
#include <iostream>

namespace test {

using TestFunction = void (*)();

void Register(const char* name, TestFunction function);
void Fail(const char* file, int line, const char* message);

struct Registrar {
  Registrar(const char* name, TestFunction function) { Register(name, function); }
};

// Prints a value if it is streamable; lib::Fixed prints as raw/256.
template <typename T>
void Print(std::ostream& os, const T& value) {
  if constexpr (requires { os << value; }) {
    os << value;
  } else if constexpr (requires { value.raw(); }) {
    os << value.raw() << "/256";
  } else {
    os << "(value not printable)";
  }
}

template <typename A, typename B>
void ExpectEq(const A& actual, const B& expected, const char* expr, const char* file, int line) {
  if (!(actual == expected)) {
    std::cerr << file << ":" << line << ": expected " << expr << "\n  actual:   ";
    Print(std::cerr, actual);
    std::cerr << "\n  expected: ";
    Print(std::cerr, expected);
    std::cerr << "\n";
    Fail(file, line, nullptr);
  }
}

}  // namespace test

#define TEST(name)                                            \
  static void name();                                         \
  static const test::Registrar name##_registrar(#name, name); \
  static void name()

#define EXPECT_TRUE(cond)                    \
  do {                                       \
    if (!(cond)) {                           \
      test::Fail(__FILE__, __LINE__, #cond); \
    }                                        \
  } while (0)

#define EXPECT_EQ(actual, expected) test::ExpectEq((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)

#define EXPECT_STREQ(actual, expected) \
  test::ExpectEq(std::string(actual), std::string(expected), #actual " == " #expected, __FILE__, __LINE__)
