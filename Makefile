CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
TARGET   := satisfy
SRC      := satisfy.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

debug: $(SRC)
	$(CXX) -std=c++17 -O0 -g -Wall -Wextra -pedantic -o $(TARGET) $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) satisfy_*.ans

.PHONY: all debug run clean
