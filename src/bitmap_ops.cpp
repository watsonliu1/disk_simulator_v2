#include "../include/disk_fs.h"
#include <cstring>
#include <iostream>

/**
 * @brief 更新块位图（标记数据块为"已使用"或"空闲"）
 * @param block_num 目标数据块的编号
 * @param used true表示标记为"已使用"，false表示标记为"空闲"
 * @return 操作成功返回true；块编号无效或IO失败返回false
 * 块位图是管理数据块分配的核心结构，1位代表1个数据块的状态
 */
bool DiskFS::set_block_bitmap(uint32_t block_num, bool used) {
    // 1. 精确检查块编号是否在数据区范围内（[data_start, data_start + data_blocks)）
    uint32_t data_end = super_block.data_start + super_block.data_blocks;

    if (block_num < super_block.data_start || block_num >= data_end) {
        return false; // 块编号超出数据区范围，无效
    }

    // 2. 计算目标块在数据区的相对索引（数据区第0块对应idx=0）
    uint32_t idx = block_num - super_block.data_start;

    // 3. 计算目标位所在的块位图块（处理块位图跨多个块的情况）
    uint32_t bits_per_block = BLOCK_SIZE * 8; // 每个块位图块可存储的位数（1字节=8位）
    uint32_t bitmap_block_idx = idx / bits_per_block; // 目标位所在的块位图块索引（0开始）

    // 块位图所占磁盘块数
    uint32_t block_bitmap_total_bytes = (MAX_BLOCKS + 7) / 8;
    uint32_t block_bitmap_size = (block_bitmap_total_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    // 检查块位图块索引是否有效（不超过块位图总块数）
    if (bitmap_block_idx >= block_bitmap_size) {
        return false;
    }

    // 目标块位图块的实际磁盘块号（起始块 + 索引）
    uint32_t target_bitmap_block = super_block.block_bitmap + bitmap_block_idx;

    // 4. 读取目标块位图块到缓冲区
    char buffer[BLOCK_SIZE];
    if (!read_block(target_bitmap_block, buffer)) {
        return false; // 读取失败
    }

    // 5. 计算目标位在当前块位图块内的位置
    uint32_t bit_in_block = idx % bits_per_block; // 目标位在当前块内的位索引
    uint32_t byte = bit_in_block / 8; // 所在字节下标（0~BLOCK_SIZE-1）
    uint8_t bit = bit_in_block % 8;   // 所在字节内的位下标（0~7）

    // 检查字节索引是否超出缓冲区范围（避免越界）
    if (byte >= BLOCK_SIZE) {
        return false;
    }

    // 6. 更新位图位状态，并修正空闲块计数
    if (used) {
        // 从"空闲"转为"使用"时才减空闲数
        bool is_currently_free = !(buffer[byte] & (1 << bit));
        buffer[byte] |= (1 << bit); // 置位（1表示使用）
        if (is_currently_free) {
            super_block.free_blocks--;
        }
    } else {
        // 从"使用"转为"空闲"时才增空闲数
        bool is_currently_used = (buffer[byte] & (1 << bit));
        buffer[byte] &= ~(1 << bit); // 清位（0表示空闲）
        if (is_currently_used) {
            super_block.free_blocks++;
        }
    }

    // 7. 将更新后的块位图块写回磁盘
    if (!write_block(target_bitmap_block, buffer)) {
        return false; // 写入失败
    }

    // 8. 同步内存中的超级块到磁盘（保证数据一致性）
    if (!write_super_block()) {
        return false; // 超级块同步失败
    }

    return true;
}



/**
 * @brief 更新inode位图（标记inode为"已使用"或"空闲"）
 * @param inode_num 目标inode的编号
 * @param used true表示标记为"已使用"，false表示标记为"空闲"
 * @return 操作成功返回true；inode编号无效或IO失败返回false
 * inode位图与块位图逻辑类似，1位代表1个inode的状态
 */
bool DiskFS::set_inode_bitmap(uint32_t inode_num, bool used)
{
    // 1. 检查inode编号是否在有效范围内（0~总inode数-1）
    if (inode_num >= super_block.total_inodes) {
        return false;
    }

    // 2. 计算inode位图每个块可存储的位数（1块=BLOCK_SIZE字节=BLOCK_SIZE*8位）
    uint32_t bits_per_block = BLOCK_SIZE * 8;

    // 3. 计算目标inode所在的inode位图块索引（处理跨多个块的情况）
    uint32_t bitmap_block_idx = inode_num / bits_per_block;

    // inode 位图所占磁盘块数
    uint32_t inode_bitmap_total_bytes = (MAX_INODES + 7) / 8;
    uint32_t inode_bitmap_size = (inode_bitmap_total_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // 检查索引是否超出inode位图总块数（确保在有效范围内）
    if (bitmap_block_idx >= inode_bitmap_size) {
        return false;
    }

    // 目标inode位图块的实际磁盘块号（起始块 + 索引）
    uint32_t target_bitmap_block = super_block.inode_bitmap + bitmap_block_idx;

    // 4. 读取目标inode位图块到缓冲区
    char buffer[BLOCK_SIZE];
    if (!read_block(target_bitmap_block, buffer)) {
        return false; // 读取失败
    }

    // 5. 计算目标inode在当前位图块内的位置
    uint32_t bit_in_block = inode_num % bits_per_block; // 块内位索引
    uint32_t byte = bit_in_block / 8; // 所在字节下标（0~BLOCK_SIZE-1）
    uint8_t bit = bit_in_block % 8;   // 字节内的位下标（0~7）

    // 检查字节索引是否超出缓冲区范围（避免越界访问）
    if (byte >= BLOCK_SIZE) {
        return false;
    }

    // 6. 更新位图位状态，并修正空闲inode计数
    if (used) {
        // 从"空闲"转为"使用"时才减空闲数
        bool is_currently_free = !(buffer[byte] & (1 << bit));
        buffer[byte] |= (1 << bit); // 置位（1表示使用）
        if (is_currently_free) {
            super_block.free_inodes--;
        }
    } else {
        // 从"使用"转为"空闲"时才增空闲数
        bool is_currently_used = (buffer[byte] & (1 << bit));
        buffer[byte] &= ~(1 << bit); // 清位（0表示空闲）
        if (is_currently_used) {
            super_block.free_inodes++;
        }
    }

    // 7. 将更新后的inode位图块写回磁盘
    if (!write_block(target_bitmap_block, buffer)) {
        return false; // 写入失败
    }

    // 8. 同步内存中的超级块到磁盘（保证数据一致性）
    if (!write_super_block()) {
        return false; // 超级块同步失败
    }

    return true;
}


/**
 * @brief 查找第一个空闲的数据块（从块位图中寻找未使用的块）
 * @return 找到的空闲块编号；无空闲块或IO失败返回-1
 * 遍历块位图，返回第一个位为0（空闲）的块编号
 */
int DiskFS::find_free_block() {
    char buffer[BLOCK_SIZE];  // 存储块位图数据的缓冲区

    // 读取块位图所在的块（简化为1个块）
    if (!read_block(super_block.block_bitmap, buffer)) return -1;

    uint32_t total_data_blocks = super_block.data_blocks;  // 总数据块数（从超级块获取）
    
    // 遍历位图的每一位，寻找第一个空闲块（位为0）
    for (uint32_t i = 0; i < total_data_blocks; i++) {
        uint32_t byte = i / 8;    // 计算当前索引对应的字节
        uint8_t bit = i % 8;      // 计算当前索引对应的位

        // 若位为0，说明该块空闲
        if (!(buffer[byte] & (1 << bit))) {
            // 转换为绝对块编号（相对索引 + 数据区起始块号）
            return super_block.data_start + i;
        }
    }
    return -1;  // 没有找到空闲块
}

/**
 * @brief 查找第一个空闲的inode（从inode位图中寻找未使用的inode）
 * @return 找到的空闲inode编号；无空闲inode或IO失败返回-1
 * 遍历inode位图（支持跨多个块），返回第一个位为0（空闲）的inode编号
 */
int DiskFS::find_free_inode() {
    // 1. 计算关键参数
    uint32_t bits_per_block = BLOCK_SIZE * 8;  // 每个块能存储的inode数（1字节=8位）
    // 动态计算inode位图总块数（也可从超级块添加inode_bitmap_size字段直接获取）
    uint32_t inode_bitmap_size = (super_block.total_inodes + bits_per_block - 1) / bits_per_block;
    char buffer[BLOCK_SIZE];

    // 2. 遍历所有inode位图块
    for (uint32_t bm_block_idx = 0; bm_block_idx < inode_bitmap_size; bm_block_idx++) {
        // 当前inode位图块的实际磁盘块号（起始块 + 索引）
        uint32_t target_bm_block = super_block.inode_bitmap + bm_block_idx;
        
        // 读取当前inode位图块到缓冲区
        if (!read_block(target_bm_block, buffer)) {
            continue;  // 读取失败，尝试下一个块（实际应处理错误）
        }

        // 3. 遍历当前块内的每一位
        for (uint32_t byte = 0; byte < BLOCK_SIZE; byte++) {
            for (uint8_t bit = 0; bit < 8; bit++) {
                // 计算当前位对应的全局inode编号（整个位图中的绝对索引）
                uint32_t global_inode_num = bm_block_idx * bits_per_block + byte * 8 + bit;
                
                // 边界检查：不超出总inode数（避免无效inode）
                if (global_inode_num >= super_block.total_inodes) {
                    return -1;
                }

                // 检查当前位是否为0（空闲inode）
                if (!(buffer[byte] & (1 << bit))) {
                    return global_inode_num;  // 返回找到的第一个空闲inode编号
                }
            }
        }
    }

    // 遍历完所有inode位图块，无空闲inode
    return -1;
}