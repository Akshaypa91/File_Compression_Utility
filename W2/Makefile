CXX = g++

CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -Iinclude -Ishared/include

TARGET = lz77

SOURCES = \
	main.cpp \
	src/Token.cpp \
	src/HashChain.cpp \
	src/LZ77.cpp

OBJECTS = \
	main.o \
	src/Token.o \
	src/HashChain.o \
	src/LZ77.o

TEST_TARGET = test_lz77

TEST_SOURCES = \
	tests/test_lz77.cpp \
	src/Token.cpp \
	src/HashChain.cpp \
	src/LZ77.cpp

TEST_OBJECTS = \
	tests/test_lz77.o \
	src/Token.o \
	src/HashChain.o \
	src/LZ77.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

src/Token.o: src/Token.cpp include/Token.h
	$(CXX) $(CXXFLAGS) -c src/Token.cpp -o src/Token.o

src/HashChain.o: src/HashChain.cpp include/HashChain.h
	$(CXX) $(CXXFLAGS) -c src/HashChain.cpp -o src/HashChain.o

src/LZ77.o: src/LZ77.cpp include/LZ77.h include/Token.h include/HashChain.h
	$(CXX) $(CXXFLAGS) -c src/LZ77.cpp -o src/LZ77.o

test: $(TEST_TARGET)
		./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(TEST_OBJECTS) -o $(TEST_TARGET)

tests/test_lz77.o: tests/test_lz77.cpp include/LZ77.h include/Token.h
	$(CXX) $(CXXFLAGS) -c tests/test_lz77.cpp -o tests/test_lz77.o

clean:
	rm -f $(OBJECTS)
	rm -f $(TEST_OBJECTS)
	rm -f $(TARGET)
	rm -f $(TEST_TARGET)

run: $(TARGET)
	./$(TARGET)