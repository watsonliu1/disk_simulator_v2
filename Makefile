CXX = g++
CXXFLAGS = -std=c++11 -Iinclude -Wall -Wextra -pthread
LDFLAGS = -pthread

TARGET = sim_disk
SRCS = src/main.cpp src/disk_init.cpp src/bitmap_ops.cpp src/pos_calc.cpp \
       src/block_ops.cpp src/file_ops.cpp src/command_parser.cpp src/task_queue.cpp
OBJS = $(SRCS:.cpp=.o)

TEST_TARGET = test_disk
TEST_SRCS = test/test_main.cpp
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

test: $(TEST_OBJS) $(filter-out src/main.o, $(OBJS))
	$(CXX) $(CXXFLAGS) -o $(TEST_TARGET) $(TEST_OBJS) $(filter-out src/main.o, $(OBJS)) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_OBJS) $(TEST_TARGET) test_disk.img disk.img

.PHONY: all test clean