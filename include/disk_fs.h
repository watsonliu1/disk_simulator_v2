#ifndef DISK_FS_H
#define DISK_FS_H

#include <string>
#include <cstdint>
#include <fstream>
#include <vector>

// 常量定义
const int BLOCK_SIZE = 4096;               // 磁盘块大小（4KB）
const int INODE_SIZE = 128;                // 每个inode的大小（字节）
const int MAX_FILENAME = 28;               // 最大文件名长度（含终止符）
const int MAX_INODES = 1024;               // 最大inode数量
const int MAX_BLOCKS = (1024 * 1024 * 100) / BLOCK_SIZE;  // 总块数（100MB磁盘）

/**
 * @brief inode结构：存储文件/目录的元数据
 */
struct Inode 
{
    uint32_t inode_num;      // inode编号
    uint32_t size;           // 文件大小（字节）
    uint32_t blocks[16];     // 数据块指针数组（直接块）
    uint8_t type;            // 类型：1表示文件，2表示目录
    uint8_t used;            // 使用状态：1表示已使用，0表示未使用
    time_t create_time;      // 创建时间
    time_t modify_time;      // 最后修改时间
};

/**
 * @brief 目录项结构：存储文件名与inode的映射
 */
struct DirEntry
{
    char name[MAX_FILENAME]; // 文件名
    uint32_t inode_num;      // 对应的inode编号
    uint8_t valid;           // 有效性：1表示有效，0表示已删除
};

/**
 * @brief 超级块结构：存储文件系统的元数据
 */
struct SuperBlock
{
    char magic[8];           // 文件系统标识（"SIMFSv1"）
    uint32_t block_size;     // 块大小
    uint32_t total_blocks;   // 磁盘总块数
    uint32_t inode_blocks;   // inode区占用的块数
    uint32_t data_blocks;    // 数据区可用的块数
    uint32_t total_inodes;   // 总inode数量
    uint32_t free_blocks;    // 当前空闲块数
    uint32_t free_inodes;    // 当前空闲inode数
    uint32_t block_bitmap;   // 块位图起始块号
    uint32_t inode_bitmap;   // inode位图起始块号
    uint32_t inode_start;    // inode区起始块号
    uint32_t data_start;     // 数据区起始块号
};

/**
 * @brief 磁盘文件系统类：实现模拟磁盘的各种操作
 */
class DiskFS
{
private:
    std::fstream disk_file;  // 磁盘文件流
    std::string disk_path;   // 磁盘文件路径
    SuperBlock super_block;  // 超级块（内存中的副本）
    bool is_mounted;         // 挂载状态

    // 计算各区域在磁盘中的位置
    uint32_t get_super_block_pos() { return 0; }
    uint32_t get_block_bitmap_pos() { return super_block.block_bitmap * BLOCK_SIZE; }
    uint32_t get_inode_bitmap_pos() { return super_block.inode_bitmap * BLOCK_SIZE; }
    uint32_t get_inode_pos(uint32_t inode_num);
    uint32_t get_data_block_pos(uint32_t block_num);

    // 位图操作
    bool set_block_bitmap(uint32_t block_num, bool used);
    bool set_inode_bitmap(uint32_t inode_num, bool used);
    int find_free_block();
    int find_free_inode();

    bool write_super_block(); // 写超级块到磁盘

    // 块读写操作
    bool read_block(uint32_t block_num, char* buffer);
    bool write_block(uint32_t block_num, const char* buffer);

public:
    DiskFS(const std::string& path);
    ~DiskFS();

    // 磁盘操作
    bool format();
    bool mount();
    bool unmount();

    // 文件操作
    int create_file(const std::string& name);
    int open_file(const std::string& name);
    int read_file(int inode_num, char* buffer, size_t size, off_t offset);
    int write_file(int inode_num, const char* buffer, size_t size, off_t offset);
    bool delete_file(const std::string& name);
    std::vector<DirEntry> list_files();

    // 信息查询
    void print_info();
    bool isMounted() const { return is_mounted; }
    int get_file_size(int inode_num); // 新增：获取文件大小
};

#endif // DISK_FS_H