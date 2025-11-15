CXX = g++
CXXFLAGS = -std=c++11 -Iinclude -Wall -Wextra -pthread -fPIC  # -fPIC用于共享库
LDFLAGS = -pthread

# 目标定义
TARGET = sim_disk               # 主程序
LIB_TARGET = libdiskfs.so       # 共享库
TEST_TARGET = test_disk         # 测试程序

# 源文件拆分
# 共享库源文件（不含main.cpp，避免主程序入口冲突）
LIB_SRCS = src/disk_init.cpp src/bitmap_ops.cpp src/pos_calc.cpp \
           src/block_ops.cpp src/file_ops.cpp src/command_parser.cpp src/task_queue.cpp
# 主程序源文件（仅main.cpp，作为独立入口）
MAIN_SRC = src/main.cpp
# 测试程序源文件
TEST_SRCS = test/stress_test.cpp

# 目标文件转换
LIB_OBJS = $(LIB_SRCS:.cpp=.o)
MAIN_OBJ = $(MAIN_SRC:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

# 默认目标：仅生成主程序和共享库（不包含测试程序）
all: $(TARGET) $(LIB_TARGET)
	@echo "默认编译完成：生成主程序 $(TARGET) 和共享库 $(LIB_TARGET)"

# 生成共享库（共享库是主程序和测试程序的依赖）
$(LIB_TARGET): $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(LIB_OBJS) $(LDFLAGS)
	@echo "共享库 $@ 生成完成"

# 生成主程序（依赖共享库）
$(TARGET): $(MAIN_OBJ) $(LIB_TARGET)
	$(CXX) $(CXXFLAGS) -o $@ $(MAIN_OBJ) -L. -ldiskfs $(LDFLAGS)
	@echo "主程序 $@ 生成完成"

# 测试目标：仅执行make test时生成测试程序（依赖共享库）
test: $(TEST_TARGET)
	@echo "测试程序 $(TEST_TARGET) 生成完成"

$(TEST_TARGET): $(TEST_OBJS) $(LIB_TARGET)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_OBJS) -L. -ldiskfs $(LDFLAGS)

# 通用编译规则（生成所有.o文件）
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理所有产物
clean:
	rm -f $(LIB_OBJS) $(MAIN_OBJ) $(TEST_OBJS) \
	      $(TARGET) $(LIB_TARGET) $(TEST_TARGET) \
	      test_disk.img disk.img
	@echo "所有产物清理完成"

.PHONY: all test clean