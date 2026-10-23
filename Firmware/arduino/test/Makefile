# Desktop tests for the WPM firmware libraries. `make` builds and runs every
# test_*.cpp; `make clean` removes build/.
CXX      = clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I../libraries/WpmCommon/src
LIB_SRCS := $(wildcard ../libraries/WpmCommon/src/*.cpp)
LIB_HDRS := $(wildcard ../libraries/WpmCommon/src/*.h) minitest.h
TESTS    := $(patsubst test_%.cpp,build/test_%,$(wildcard test_*.cpp))

.PHONY: all run clean

all: run

run: $(TESTS)
	@status=0; for t in $(TESTS); do echo "== $$t"; ./$$t || status=1; done; exit $$status

build/test_%: test_%.cpp $(LIB_SRCS) $(LIB_HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB_SRCS)

clean:
	rm -rf build
