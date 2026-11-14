CXX ?= g++
CPPFLAGS := -Itask4/include -Ithird_party
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -pthread
LDLIBS := -pthread -l:libpq.so.5
SOURCES := $(wildcard task4/src/*.cpp)
OBJECTS := $(patsubst task4/src/%.cpp,build/%.o,$(SOURCES))

.PHONY: all test package
all: build/metro

build/%.o: task4/src/%.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/metro: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

build/integration: tests/integration.cpp $(filter-out build/main.o,$(OBJECTS))
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDLIBS) -o $@

test: build/metro build/integration
	./build/integration

package: build/metro
	bash package.sh

-include $(OBJECTS:.o=.d)
