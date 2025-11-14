#include "../include/disk_fs.h"
#include <iostream>



/**
 * @brief 辅助函数：将内存中的超级块写回磁盘（保证数据一致性）
 */
bool DiskFS::write_super_block() 
{
    disk_file.seekp(0); // 超级块固定在磁盘0号位置
    disk_file.write((char*)&super_block, sizeof(SuperBlock));
    return disk_file.good(); // 检查写入是否成功
}


/**
 * @brief 从磁盘读取一个完整的块
 * @param block_num 目标块的编号（0~总块数-1）
 * @param buffer 接收数据的缓冲区（必须预先分配BLOCK_SIZE大小的空间）
 * @return 读取成功返回true；块编号无效或IO失败返回false
 * 块是磁盘IO的基本单位，所有磁盘读写都以块为单位进行
 */
bool DiskFS::read_block(uint32_t block_num, char* buffer) {
    // 检查块编号是否有效（必须小于总块数）
    if (block_num >= super_block.total_blocks) return false;
    
    // 计算块在磁盘文件中的起始字节位置（块编号 × 块大小）
    uint32_t pos = block_num * BLOCK_SIZE;
    disk_file.seekg(pos);  // 将文件读指针定位到目标块的起始位置
    disk_file.read(buffer, BLOCK_SIZE);  // 读取整个块的数据到缓冲区
    return disk_file.good();  // 返回IO操作状态（true表示成功）
}

/**
 * @brief 向磁盘写入一个完整的块
 * @param block_num 目标块的编号（0~总块数-1）
 * @param buffer 存储待写入数据的缓冲区（大小必须为BLOCK_SIZE）
 * @return 写入成功返回true；块编号无效或IO失败返回false
 */
bool DiskFS::write_block(uint32_t block_num, const char* buffer) {
    // 检查块编号是否有效
    if (block_num >= super_block.total_blocks) return false;
    
    // 计算块在磁盘文件中的起始字节位置
    uint32_t pos = block_num * BLOCK_SIZE;
    disk_file.seekp(pos);  // 将文件写指针定位到目标块的起始位置
    disk_file.write(buffer, BLOCK_SIZE);  // 将缓冲区数据写入整个块
    return disk_file.good();  // 返回IO操作状态
}

