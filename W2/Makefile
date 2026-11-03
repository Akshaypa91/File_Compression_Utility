CXX = g++

# CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -Iinclude -Ishared/include
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic \
           -Iinclude \
           -Ishared/include \
           -I../shared/include \
           -I../W3/include \
           -I../W1/include

TARGET = lz77

SOURCES = \
	main.cpp \
	src/Token.cpp \
	src/HashChain.cpp \
	src/LZ77.cpp \
	src/LZ77BlockProducer.cpp

OBJECTS = \
	main.o \
	src/Token.o \
	src/HashChain.o \
	src/LZ77.o \
	src/LZ77BlockProducer.o

TEST_TARGET = test_lz77

TEST_SOURCES = \
	tests/test_lz77.cpp \
	src/Token.cpp \
	src/HashChain.cpp \
	src/LZ77.cpp \
	tests/test_lz77_block_producer.cpp

TEST_OBJECTS = \
	tests/test_lz77.o \
	src/Token.o \
	src/HashChain.o \
	src/LZ77.o

TEST_BLOCK_PRODUCER_TARGET = test_lz77_block_producer

TEST_BLOCK_PRODUCER_SOURCES = \
	tests/test_lz77_block_producer.cpp \
	src/LZ77BlockProducer.cpp \
	src/LZ77.cpp \
	src/Token.cpp \
	src/HashChain.cpp

all: $(TARGET)
# Build LZ77 Block Producer test
$(TEST_BLOCK_PRODUCER_TARGET): $(TEST_BLOCK_PRODUCER_SOURCES)
	$(CXX) $(CXXFLAGS) $(TEST_BLOCK_PRODUCER_SOURCES) -o $(TEST_BLOCK_PRODUCER_TARGET)

# Run LZ77 Block Producer test
test-block-producer: $(TEST_BLOCK_PRODUCER_TARGET)
	./$(TEST_BLOCK_PRODUCER_TARGET)

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

src/LZ77BlockProducer.o: src/LZ77BlockProducer.cpp include/LZ77BlockProducer.h include/LZ77.h
	$(CXX) $(CXXFLAGS) -c src/LZ77BlockProducer.cpp -o src/LZ77BlockProducer.o

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
	rm -f $(TEST_BLOCK_PRODUCER_TARGET)

run: $(TARGET)
	./$(TARGET)
.PHONY: all test test-block-producer clean run