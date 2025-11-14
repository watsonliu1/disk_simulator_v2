#ifndef DISK_FS_H
#define DISK_FS_H

#include <string>
#include <cstdint>
#include <fstream>
#include <vector>

// 常量定义
const int BLOCK_SIZE = 4096;               // 磁盘块大小（4KB，常见的块大小选择）
const int INODE_SIZE = 96;                // 每个inode的大小（字节）
const int MAX_FILENAME = 28;               // 最大文件名长度（含终止符，共28字节）
const int MAX_INODES = 1024;               // 最大inode数量（支持最多1024个文件/目录）
const int MAX_BLOCKS = (1024 * 1024 * 100) / BLOCK_SIZE;  // 总块数（100MB磁盘）

/**
 * @brief inode结构：存储文件/目录的元数据
 */
struct Inode 
{
    uint32_t inode_num;      // inode编号（唯一标识）
    uint32_t size;           // 文件大小（字节）
    uint32_t blocks[16];     // 数据块指针数组（直接块，最多16个块）
    uint8_t type;            // 类型：1表示文件，2表示目录
    uint8_t used;            // 使用状态：1表示已使用，0表示未使用
    time_t create_time;      // 创建时间（时间戳）
    time_t modify_time;      // 最后修改时间（时间戳）
};

/**
 * @brief 目录项结构：存储文件名与inode的映射
 */
struct DirEntry
{
    char name[MAX_FILENAME]; // 文件名（最大28字节，含'\0'）
    uint32_t inode_num;      // 对应的inode编号
    uint8_t valid;           // 有效性：1表示有效，0表示已删除
};

/**
 * @brief 超级块结构：存储文件系统的元数据
 */
struct SuperBlock
{
    char magic[8];           // 文件系统标识（"SIMFSv1"，用于验证）
    uint32_t block_size;     // 块大小（字节，应等于BLOCK_SIZE）
    uint32_t total_blocks;   // 磁盘总块数
    uint32_t inode_blocks;   // inode区占用的块数
    uint32_t data_blocks;    // 数据区可用的块数
    uint32_t total_inodes;   // 总inode数量
    uint32_t free_blocks;    // 当前空闲块数
    uint32_t free_inodes;    // 当前空闲inode数
    uint32_t block_bitmap;   // 块位图起始块号（管理数据块分配）
    uint32_t inode_bitmap;   // inode位图起始块号（管理inode分配）
    uint32_t inode_start;    // inode区起始块号
    uint32_t data_start;     // 数据区起始块号
};

/**
 * @brief 磁盘文件系统类：实现模拟磁盘的各种操作
 */
class DiskFS
{
    // 声明测试函数为友元，允许它们访问私有成员
    friend void test_bitmap_ops();    // 位图测试函数
    friend void test_block_ops();     // 块操作测试函数
    friend void test_file_ops();      // 文件操作测试函数

private:
    mutable std::fstream disk_file;  // 磁盘文件流（用于读写磁盘文件）
    std::string disk_path;   // 磁盘文件路径
    SuperBlock super_block;  // 超级块（内存中的副本）
    bool is_mounted;         // 挂载状态：true表示已挂载

    // 计算各区域在磁盘中的位置（字节偏移量）
    uint32_t get_super_block_pos() { return 0; }  // 超级块固定在0位置
    uint32_t get_block_bitmap_pos() { return super_block.block_bitmap * BLOCK_SIZE; }
    uint32_t get_inode_bitmap_pos() { return super_block.inode_bitmap * BLOCK_SIZE; }
    uint32_t get_inode_pos(uint32_t inode_num) const;   // 计算inode的位置
    uint32_t get_data_block_pos(uint32_t block_num);  // 计算数据块的位置

    // 位图操作（内部使用，管理块和inode的分配）
    bool set_block_bitmap(uint32_t block_num, bool used);  // 更新块位图
    bool set_inode_bitmap(uint32_t inode_num, bool used);  // 更新inode位图
    int find_free_block();  // 查找空闲数据块
    int find_free_inode();  // 查找空闲inode

    bool write_super_block(); // 辅助函数：将内存中的超级块写回磁盘（保证数据一致性）

    // 块读写操作（内部使用，读写指定块）
    bool read_block(uint32_t block_num, char* buffer);   // 读取块
    bool write_block(uint32_t block_num, const char* buffer);  // 写入块

public:
    /**
     * @brief 构造函数
     * @param path 磁盘文件的路径
     */
    DiskFS(const std::string& path);

    /**
     * @brief 析构函数：确保卸载磁盘
     */
    ~DiskFS();

    // 磁盘操作
    bool format();    // 格式化磁盘（初始化文件系统）
    bool mount();     // 挂载磁盘（加载文件系统）
    bool unmount();   // 卸载磁盘（保存并关闭）

    // 文件操作
    int create_file(const std::string& name);  // 创建文件，返回inode
    int open_file(const std::string& name);    // 打开文件，返回inode
    int read_file(int inode_num, char* buffer, size_t size, off_t offset);  // 读取文件
    int write_file(int inode_num, const char* buffer, size_t size, off_t offset);  // 写入文件
    bool delete_file(const std::string& name);  // 删除文件
    std::vector<DirEntry> list_files();         // 列出所有文件

    // 信息查询
    void print_info();                // 打印磁盘信息
    bool isMounted() const { return is_mounted; }  // 判断是否已挂载

    int get_file_size(int inode_num); // 新增：获取文件大小

     // 新增：判断inode是否被使用（测试专用）
    bool is_inode_used(uint32_t inode_num) const;

    // 新增：获取当前内存中的超级块数据（供测试用）
    const SuperBlock& get_super_block() const 
    {
        return super_block;  // 返回超级块的常量引用（避免拷贝，确保只读）
    }
};

#endif // DISK_FS_H