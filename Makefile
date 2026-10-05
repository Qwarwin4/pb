CXX ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra
LDLIBS = -lz -pthread
SRC = $(wildcard src/*.cpp src/pkg/*.cpp)
HDR = $(wildcard src/*.hpp src/pkg/*.hpp)
BIN = pb

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS) -s

static: $(SRC) $(HDR)
	@echo 'int main(){}' | $(CXX) $(CXXFLAGS) -x c++ - -o /dev/null $(LDFLAGS) -static $(LDLIBS) 2>/dev/null || { \
		echo "static libraries are missing (zlib, libc, libstdc++):"; \
		echo "  Arch:          pacman -S zlib-static"; \
		echo "  Fedora:        dnf install zlib-ng-compat-static glibc-static libstdc++-static"; \
		echo "  Debian/Ubuntu: apt install zlib1g-dev"; \
		exit 1; }
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC) $(LDFLAGS) -static $(LDLIBS) -s

clean:
	rm -f $(BIN)

.PHONY: static clean
