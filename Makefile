# Plain-make build for machines without CMake (the CMake build is the reference one). Produces build/openautoroute.
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Iinclude
HEADERS := $(wildcard include/openautoroute/*.hpp)
CORE := src/cost_grid.cpp src/pathfinder.cpp src/gpx.cpp src/s57.cpp src/chart_grid.cpp

all: build/openautoroute build/test_core

build/openautoroute: $(CORE) tools/openautoroute.cpp $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@

build/test_core: $(CORE) tests/test_core.cpp $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@

test: build/test_core
	build/test_core

clean:
	rm -rf build

.PHONY: all test clean
