#include <cstdio>

#include "minitest.h"
#include "wpm_loader_logic.h"

int main() {
  // A pending record always wins: the staged image is copied (and CRC checked)
  // before anything is jumped to, whatever the current flash holds.
  CHECK(wpm_loader_decide(true, true) == WPM_LOADER_COPY);
  CHECK(wpm_loader_decide(true, false) == WPM_LOADER_COPY);
  // No pending record: jump if the flash app looks valid, else blink and hand off to SAM-BA.
  CHECK(wpm_loader_decide(false, true) == WPM_LOADER_JUMP);
  CHECK(wpm_loader_decide(false, false) == WPM_LOADER_BLINK);

  std::printf("test_loader_logic: %d failures\n", minitest_failures);
  return minitest_failures;
}
