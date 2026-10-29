CXX ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -MMD -MP
LDLIBS = -lz -pthread
SRC = $(wildcard src/*.cpp src/pkg/*.cpp)
OBJ = $(SRC:src/%.cpp=build/%.o)
BIN = pb

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS) -s

build/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

static: $(OBJ)
	@echo 'int main(){}' | $(CXX) -x c++ - -o /dev/null $(LDFLAGS) -static $(LDLIBS) 2>/dev/null || { \
		echo "static libraries are missing (zlib, libc, libstdc++):"; \
		echo "  Arch:          pacman -S zlib-static"; \
		echo "  Fedora:        dnf install zlib-ng-compat-static glibc-static libstdc++-static"; \
		echo "  Debian/Ubuntu: apt install zlib1g-dev"; \
		exit 1; }
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ) $(LDFLAGS) -static $(LDLIBS) -s

clean:
	rm -rf build $(BIN)

.PHONY: static clean

-include $(OBJ:.o=.d)
