# 定义编译器（使用g++）
CXX = g++
# 编译选项：
# -std=c++11：使用C++11标准
# -Iinclude：指定头文件目录为include
# -Wall -Wextra：开启额外警告，帮助检查代码问题
# -pthread：支持多线程
CXXFLAGS = -std=c++11 -Iinclude -Wall -Wextra -pthread
# 链接选项
LDFLAGS = -pthread

# 目标可执行文件名
TARGET = sim_disk

# 源文件列表（主程序和磁盘文件系统实现）
SRCS = main.cpp src/disk_fs.cpp
# 目标文件列表（将源文件后缀.cpp替换为.o）
OBJS = $(SRCS:.cpp=.o)

# 默认目标（执行make时默认构建）
all: $(TARGET)

# 链接目标：将所有目标文件链接为可执行文件
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# 编译规则：将.cpp文件编译为.o目标文件
# $<：依赖文件（源文件），$@：目标文件（.o文件）
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理目标：删除生成的目标文件和可执行文件
clean:
	rm -f $(OBJS) $(TARGET)

# 声明伪目标（避免与同名文件冲突）
.PHONY: all clean