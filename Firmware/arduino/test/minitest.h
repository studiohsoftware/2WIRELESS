#pragma once
// Minimal header-only test harness for the desktop tests. CHECK() counts
// failures and reports each one on stderr. A test file keeps the count in
// minitest_failures and returns it from main (0 = pass).
#include <cstdio>

inline int minitest_failures = 0;

#define CHECK(expr)                                                        \
  do {                                                                     \
    if (!(expr)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
      ++minitest_failures;                                                 \
    }                                                                      \
  } while (0)
