CXX=g++

main: main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

CXXFLAGS=-O3 -std=c++17 -march=native -mtune=native -pthread
LDFLAGS=-pthread

main-imp: main-imp.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test-suite: test-suite.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
