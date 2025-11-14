文件划分

超级块 ｜ 块位图 ｜ inode 位图 ｜ inode 区 ｜ 数据区

简易磁盘文件系统（Simple Disk File System）
一个基于 C++ 实现的简化版磁盘文件系统，模拟真实文件系统的核心机制（inode 管理、数据块分配、目录管理等），适用于操作系统课程实践或文件系统原理学习。
项目概述
核心定位：模拟磁盘文件系统的基础功能，支持磁盘格式化、挂载 / 卸载、文件创建 / 删除 / 读写、目录索引等核心操作。
设计目标：简化真实文件系统的复杂逻辑，聚焦核心原理（inode 与数据块分离、超级块元数据管理、目录项映射）。
适用场景：操作系统课程实践、文件系统原理学习、基础存储机制验证。
核心功能
磁盘格式化（format）：初始化超级块、inode 区、数据区、根目录。
磁盘挂载 / 卸载（mount/unmount）：加载磁盘元数据到内存，同步内存数据到磁盘。
文件操作：支持文件创建（create_file）、删除（delete_file）、随机读写（read_file/write_file）。
目录管理：根目录下文件索引，基于目录项（DirEntry）维护文件名与 inode 映射。
资源管理：自动分配 / 释放 inode 和数据块，通过位图标记资源使用状态。
技术选型
开发语言：C++11 及以上
编译工具：GCC 7.0+ / Clang 6.0+
构建工具：CMake 3.10+
依赖：无第三方库，仅依赖 C++ 标准库
项目结构
plaintext
simple-disk-fs/
├── include/                # 头文件目录
│   ├── disk_fs.h           # 核心类与结构体定义
│   └── const.h             # 常量宏定义（可选，若拆分常量）
├── src/                    # 源文件目录
│   ├── disk_fs.cpp         # 核心功能实现
│   └── main.cpp            # 测试/示例入口
├── test/                   # 测试代码目录（可选）
│   └── fs_test.cpp         # 功能测试用例
├── CMakeLists.txt          # CMake 构建配置
├── disk.img                # 模拟磁盘文件（运行后生成）
└── README.md               # 项目说明文档

快速上手
1. 环境准备
安装依赖：确保系统已安装 GCC/Clang、CMake。
bash
# Ubuntu/Debian 示例
sudo apt update && sudo apt install gcc cmake
2. 编译项目
bash
# 克隆仓库（本地项目可跳过）
git clone https://github.com/your-username/simple-disk-fs.git
cd simple-disk-fs

# 创建构建目录
mkdir build && cd build

# 编译
cmake .. && make
3. 运行示例
编译完成后，生成可执行文件（如 disk_fs_demo），运行即可测试核心功能：
bash
# 运行示例程序
./disk_fs_demo

# 预期输出（示例）
磁盘格式化成功
磁盘挂载成功
文件 test.txt 创建成功，inode：1
数据写入成功，写入字节数：18
文件读取成功，内容：hello, disk fs! 12345
文件删除成功
磁盘卸载成功


配置说明
核心配置参数定义在 include/disk_fs.h 或 include/const.h 中，可根据需求修改：
配置项	含义	默认值
BLOCK_SIZE	数据块大小（字节）	4096
INODE_SIZE	单个 inode 大小（字节）	90（自动适配）
MAX_INODES	最大 inode 数量	1024
MAX_FILENAME	最大文件名长度（含终止符）	28
MAX_BLOCKS	总数据块数	25600（100MB）
修改配置后需重新编译项目（cd build && make clean && make）。
常见问题（FAQ）
Q1：编译报错「undefined reference to DiskFS::xxx」？
A1：确认 disk_fs.cpp 中实现了对应的成员函数，且 CMakeLists.txt 已包含 src/disk_fs.cpp 编译。
Q2：写入文件后读取不到数据？
A2：检查以下几点：
写入前是否调用 mount() 挂载磁盘；
写入后是否调用 disk_file.flush() 同步数据；
读取时 inode 编号是否正确，偏移量是否超出文件大小。
Q3：输出 inode.used 或 type 为空 / 乱码？
A3：used 和 type 是 uint8_t 类型，直接输出会解析为 ASCII 字符，需强制转为 int 输出：
cpp
std::cout << "type: " << static_cast<int>(inode.type) << std::endl;
Q4：结构体对齐导致数据读写错位？
A4：已通过 #pragma pack(push, 1) 强制 1 字节对齐，若仍有问题，检查 static_assert(sizeof(Inode) == INODE_SIZE) 是否编译通过。
许可证
本项目采用 MIT 许可证，允许自由使用、修改和分发，商用需保留原作者版权声明。
联系方式
作者：watson