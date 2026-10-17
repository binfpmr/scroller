#include <vector>

#include "test.h"

namespace test {
namespace {

struct Entry {
  const char* name;
  TestFunction function;
};

std::vector<Entry>& Registry() {
  static std::vector<Entry> registry;
  return registry;
}

int failures = 0;

}  // namespace

void Register(const char* name, TestFunction function) { Registry().push_back({name, function}); }

void Fail(const char* file, int line, const char* message) {
  if (message != nullptr) {
    std::cerr << file << ":" << line << ": expectation failed: " << message << "\n";
  }
  ++failures;
}

}  // namespace test

int main() {
  int failed_tests = 0;
  for (const test::Entry& entry : test::Registry()) {
    const int before = test::failures;
    entry.function();
    const bool ok = test::failures == before;
    failed_tests += ok ? 0 : 1;
    std::cout << (ok ? "[ ok ] " : "[FAIL] ") << entry.name << "\n";
  }
  std::cout << test::Registry().size() - failed_tests << "/" << test::Registry().size() << " tests passed\n";
  return failed_tests == 0 ? 0 : 1;
}
