#include "../include/disk_fs.h"


/**
 * @brief 计算inode在磁盘中的字节偏移量
 * @param inode_num 目标inode的编号（0~MAX_INODES-1）
 * @return 若inode编号有效，返回其在磁盘中的起始字节位置；否则返回0（无效位置）
 * 计算逻辑：inode区起始块 × 块大小 + inode编号 × 单个inode大小
 */
uint32_t DiskFS::get_inode_pos(uint32_t inode_num) const {
    // 检查inode编号是否超出允许范围（总inode数由超级块定义）
    if (inode_num >= super_block.total_inodes) return 0;
    // inode区起始位置 = 超级块中记录的inode起始块 × 块大小
    // 目标inode位置 = inode区起始位置 + inode编号 × 单个inode大小（INODE_SIZE）
    //return super_block.inode_start * BLOCK_SIZE + inode_num * INODE_SIZE;
    return super_block.inode_start * BLOCK_SIZE + inode_num * sizeof(struct Inode);
}

/**
 * @brief 计算数据块在磁盘中的字节偏移量
 * @param block_num 目标数据块的编号（数据区范围内的块号）
 * @return 若块编号有效，返回其在磁盘中的起始字节位置；否则返回0（无效位置）
 * 计算逻辑：数据区起始块 × 块大小 + (块编号 - 数据区起始块) × 块大小
 */
uint32_t DiskFS::get_data_block_pos(uint32_t block_num) {
    // 检查块编号是否在数据区范围内（数据区起始块~总块数-1）
    if (block_num < super_block.data_start || block_num >= super_block.total_blocks) return 0;
    // 数据区起始位置 = 超级块中记录的数据区起始块 × 块大小
    // 目标块位置 = 数据区起始位置 + (块编号 - 数据区起始块) × 块大小
    return super_block.data_start * BLOCK_SIZE + (block_num - super_block.data_start) * BLOCK_SIZE;
}
