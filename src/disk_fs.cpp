#include "../include/disk_fs.h"
#include <cstring>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>

/**
 * @brief 构造函数：初始化磁盘路径和挂载状态
 * @param path 磁盘文件的路径（如"disk.img"）
 * 初始化时磁盘未挂载，仅记录磁盘文件的路径供后续操作使用
 */
DiskFS::DiskFS(const std::string& path) : disk_path(path), is_mounted(false) {}

/**
 * @brief 析构函数：确保磁盘在对象销毁前正确卸载
 * 若磁盘处于挂载状态，自动调用unmount()将内存数据写回磁盘并关闭文件，避免数据丢失
 */
DiskFS::~DiskFS()
{
    if (is_mounted)
    {
        unmount();
    }
}

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
 * @brief 辅助函数：将内存中的超级块写回磁盘（保证数据一致性）
 */
bool DiskFS::write_super_block() 
{
    disk_file.seekp(0); // 超级块固定在磁盘0号位置
    disk_file.write((char*)&super_block, sizeof(SuperBlock));
    return disk_file.good(); // 检查写入是否成功
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

/**
 * @brief 格式化磁盘：初始化文件系统的所有结构（超级块、位图、inode区、根目录）
 * @return 格式化成功返回true；文件打开失败或IO错误返回false
 * 格式化会清空磁盘原有数据，创建新的文件系统布局，是使用磁盘的前提
 */
bool DiskFS::format() 
{
    // 以读写+二进制模式打开磁盘文件；若文件不存在则创建
    disk_file.open(disk_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk_file) {
        // 若文件不存在，尝试创建新文件（trunc模式会清空文件，确保从头开始）
        disk_file.open(disk_path, std::ios::trunc | std::ios::out | std::ios::in | std::ios::binary);
        if (!disk_file) return false;  // 创建失败则返回错误
    }

    /**
    * 计算文件系统各区域的块数（磁盘布局规划）
    */
    const uint32_t super_block_size = 1;  // 超级块固定占用1个块

    // 块位图所占磁盘块数
    uint32_t block_bitmap_total_bytes = (MAX_BLOCKS + 7) / 8;
    uint32_t block_bitmap_size = (block_bitmap_total_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // inode 位图所占磁盘块数
    uint32_t inode_bitmap_total_bytes = (MAX_INODES + 7) / 8;
    uint32_t inode_bitmap_size = (inode_bitmap_total_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // 存放所有 inode 需要的磁盘块数
    uint32_t inode_area_size = (MAX_INODES * INODE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    // 初始化超级块（文件系统的元数据核心）
    memset(&super_block, 0, sizeof(SuperBlock));  // 先清空所有字段
    strcpy(super_block.magic, "SIMFSv1");  // 设置文件系统标识（用于挂载时验证）
    super_block.block_size = BLOCK_SIZE;   // 块大小（4KB）
    super_block.total_blocks = MAX_BLOCKS; // 总块数（由磁盘大小和块大小决定）
    super_block.inode_blocks = inode_area_size;  // inode区占用的块数
    
    // 数据块总数 = 总块数 - 其他区域（超级块+位图+inode区）占用的块数
    super_block.data_blocks = MAX_BLOCKS - (super_block_size + block_bitmap_size + inode_bitmap_size + inode_area_size);
    super_block.total_inodes = MAX_INODES;  // 总inode数（1024）
    super_block.free_blocks = super_block.data_blocks;  // 初始空闲块=总数据块（全部未使用）
    super_block.free_inodes = MAX_INODES;  // 预留根目录inode（0号），此时空闲inode数先不减1
    
    // 记录各区域的起始块号（磁盘布局的关键参数）
    super_block.block_bitmap = super_block_size;  // 块位图紧跟超级块（起始块1）
    super_block.inode_bitmap = super_block.block_bitmap + block_bitmap_size;  // inode位图紧跟块位图
    super_block.inode_start = super_block.inode_bitmap + inode_bitmap_size;   // inode区紧跟inode位图
    super_block.data_start = super_block.inode_start + inode_area_size;       // 数据区紧跟inode区

    // 将初始化好的超级块写入磁盘（位置0）
    disk_file.seekp(0);
    disk_file.write((char*)&super_block, sizeof(SuperBlock));

    // 初始化块位图（全部置0，表示所有数据块空闲）
    char buffer[BLOCK_SIZE] = {0};  // 用0初始化缓冲区（0表示空闲）
    for (uint32_t i = 0; i < block_bitmap_size; i++) 
    {
        write_block(super_block.block_bitmap + i, buffer);  // 写入每个位图块
    }

    // 初始化inode位图（全部置0，表示所有inode空闲，后续单独标记根目录）
    memset(buffer, 0, BLOCK_SIZE);  // 再次清空缓冲区
    for (uint32_t i = 0; i < inode_bitmap_size; i++) 
    {
        write_block(super_block.inode_bitmap + i, buffer);  // 写入每个inode位图块
    }

    // 标记根目录inode（0号）为已使用（根目录是文件系统的起点）
    set_inode_bitmap(0, true);

    // 初始化所有inode为未使用状态（默认值）
    Inode inode;
    memset(&inode, 0, sizeof(Inode));  // 清空inode结构
    for (uint32_t i = 1; i < MAX_INODES; i++)
    {
        inode.inode_num = i;  // 设置inode编号
        inode.used = 0;       // 标记为未使用
        disk_file.seekp(get_inode_pos(i));  // 定位到该inode在磁盘中的位置
        disk_file.write((char*)&inode, sizeof(Inode));  // 写入inode数据
    }

    // 为根目录分配一个数据块（存储目录项）
    int root_block = find_free_block();
    Inode root_inode;

    if (root_block == -1) {
        disk_file.close();
        return false;  // 根目录块分配失败，格式化失败
    }

    // 初始化根目录inode（0号inode，类型为目录）
    time_t now = time(nullptr);  // 获取当前时间戳（用于创建/修改时间）
    
    memset(&root_inode, 0, sizeof(Inode));
    root_inode.inode_num = 0;
    root_inode.type = 2;  // 类型标识：2表示目录（1表示普通文件）
    root_inode.used = 1;  // 标记为已使用
    root_inode.create_time = now;  // 创建时间
    root_inode.modify_time = now;  // 修改时间（初始与创建时间相同）


    root_inode.blocks[0] = root_block;  // 根目录的数据块指针指向该块
    root_inode.size = BLOCK_SIZE;       // 根目录大小为1个块（4KB）

    // 将初始化好的根目录inode写入磁盘
    disk_file.seekp(get_inode_pos(0));
    disk_file.write((char*)&root_inode, sizeof(Inode));

    // 检查写入是否成功
    if (disk_file.fail()) {
        std::cerr << "根目录inode写入失败！" << std::endl;
    } else {
        std::cerr << "根目录inode写入成功" << std::endl;
    }
    
    // 初始化根目录内容：包含"当前目录"（.）的目录项
    memset(buffer, 0, BLOCK_SIZE);  // 清空缓冲区
    DirEntry* root_entry = (DirEntry*)buffer;  // 将缓冲区视为目录项数组

    // 初始化"."（当前目录）：指向根目录自身的inode（0号）
    strcpy(root_entry[0].name, ".");
    root_entry[0].inode_num = 0;  // 关联0号inode（根目录）
    root_entry[0].valid = 1;      // 标记为有效

    set_block_bitmap(root_block, true);  // 标记该块为已使用（更新块位图）
            
    write_block(root_block, buffer);  // 将根目录数据写入分配的块
    
    disk_file.close();  // 格式化完成，关闭磁盘文件
    return true;
}

/**
 * @brief 挂载磁盘：加载文件系统到内存，准备进行操作
 * @return 挂载成功返回true；文件打开失败或文件系统标识不匹配返回false
 * 挂载是使用磁盘前的必要步骤，会验证文件系统合法性并加载超级块到内存
 */
bool DiskFS::mount()
{
    if (is_mounted) 
    {
        return true;  // 若已挂载，直接返回成功
    }

    // 以读写+二进制模式打开磁盘文件
    disk_file.open(disk_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk_file) 
    {
        return false;  // 打开失败
    }

    // 读取超级块（位于磁盘0号块）到内存
    disk_file.seekg(0);
    disk_file.read((char*)&super_block, sizeof(SuperBlock));

    // 验证文件系统标识（必须为"SIMFSv1"，确保是兼容的文件系统）
    if (strncmp(super_block.magic, "SIMFSv1", 7) != 0) {
        disk_file.close();  // 标识不匹配，关闭文件
        return false;
    }

    is_mounted = true;  // 标记为已挂载状态
    return true;
}

/**
 * @brief 卸载磁盘：将内存中的超级块写回磁盘，关闭文件
 * @return 卸载成功返回true；未挂载或IO失败返回false
 * 卸载确保内存中的元数据（如空闲块数、inode数）同步到磁盘，避免数据不一致
 */
bool DiskFS::unmount() 
{
    if (!is_mounted) return true;  // 若未挂载，直接返回成功

    // 将内存中的超级块写回磁盘（保存最新的元数据）
    disk_file.seekp(0);
    disk_file.write((char*)&super_block, sizeof(SuperBlock));
    
    disk_file.close();  // 关闭磁盘文件
    is_mounted = false;  // 标记为未挂载状态
    return true;
}

/**
 * @brief 创建文件：分配inode并在根目录中添加目录项
 * @param name 文件名（最大长度为MAX_FILENAME-1，含终止符）
 * @return 成功返回新文件的inode编号；失败返回-1（已存在/无空闲inode/未挂载等）
 */
int DiskFS::create_file(const std::string& name)
{
    // 前置条件检查：磁盘已挂载，文件名长度合法（不含终止符不超过MAX_FILENAME-1）
    if (!isMounted() || name.empty() || name.length() >= MAX_FILENAME) 
    {
        std::cerr << "创建文件失败：磁盘未挂载或文件名无效" << std::endl;
        return -1;
    }

    // 清除文件流错误状态，避免之前的错误影响当前操作
    disk_file.clear();

    // 检查文件是否已存在（遍历根目录目录项）
    std::vector<DirEntry> dir_list = list_files();
    for (const auto& entry : dir_list) 
    {
        if (entry.valid && name == entry.name) 
        {
            std::cerr << "创建文件失败：" << name << " 已存在" << std::endl;
            return -1;
        }
    }

    // 读取根目录inode（0号inode），并检查读取结果
    Inode root_inode;
    disk_file.seekg(get_inode_pos(0));
    disk_file.read((char*)&root_inode, sizeof(Inode));

    if (disk_file.fail() || root_inode.type != 2) {  // 检查读取失败或类型错误
        std::cerr << "创建文件失败：根目录inode无效" << std::endl;
        return -1;
    }

    // 检查根目录数据块是否有效（至少分配了一个块）
    if (root_inode.blocks[0] == 0)
    {  // 假设0表示未分配块
        std::cerr << "创建文件失败：根目录数据块未分配" << std::endl;
        return -1;
    }

    // 分配空闲inode
    int inode_num = find_free_inode();
    if (inode_num == -1) {
        std::cerr << "创建文件失败：无空闲inode" << std::endl;
        return -1;
    }

    // 初始化新文件的inode（普通文件类型）
    time_t now = time(nullptr);
    Inode new_inode;
    memset(&new_inode, 0, sizeof(Inode));
    new_inode.type = 1;  // 1：普通文件（确保Inode结构体有type成员）
    new_inode.used = 1;  // 标记为已使用
    new_inode.create_time = now;
    new_inode.modify_time = now;
    new_inode.size = 0;  // 初始大小为0

    // 写入新inode到磁盘，并检查操作结果
    disk_file.seekp(get_inode_pos(inode_num));
    disk_file.write((char*)&new_inode, sizeof(Inode));
    if (disk_file.fail()) {
        std::cerr << "创建文件失败：写入inode " << inode_num << " 失败" << std::endl;
        return -1;  // 写入失败，不标记位图，避免inode泄露
    }
    set_inode_bitmap(inode_num, true);  // 写入成功后再标记位图

    // 读取根目录数据块（简化设计：根目录仅使用1个块）
    char buffer[BLOCK_SIZE];
    if (!read_block(root_inode.blocks[0], buffer)) {
        std::cerr << "创建文件失败：读取根目录数据块失败" << std::endl;
        // 回滚：删除已分配的inode（标记为未使用）
        set_inode_bitmap(inode_num, false);
        return -1;
    }

    // 寻找根目录中第一个空闲目录项（跳过0号的"."）
    DirEntry* dir_entries = (DirEntry*)buffer;
    size_t dir_entry_count = BLOCK_SIZE / sizeof(DirEntry);
    size_t free_index = dir_entry_count;  // 初始化为无效索引
    for (size_t i = 1; i < dir_entry_count; i++) {
        if (!dir_entries[i].valid) {
            free_index = i;
            break;
        }
    }
    if (free_index == dir_entry_count) {  // 无空闲目录项
        std::cerr << "创建文件失败：根目录已满，无空闲目录项" << std::endl;
        // 回滚：删除已分配的inode
        set_inode_bitmap(inode_num, false);
        return -1;
    }

    // 填充空闲目录项
    strncpy(dir_entries[free_index].name, name.c_str(), MAX_FILENAME - 1);
    dir_entries[free_index].name[MAX_FILENAME - 1] = '\0';  // 确保终止符
    dir_entries[free_index].inode_num = inode_num;
    dir_entries[free_index].valid = 1;

    // 写回根目录数据块，并检查结果
    if (!write_block(root_inode.blocks[0], buffer)) {
        std::cerr << "创建文件失败：写回根目录数据块失败" << std::endl;
        // 回滚：删除inode和目录项（目录项未写入，仅需回滚inode）
        set_inode_bitmap(inode_num, false);
        return -1;
    }

    // 更新根目录inode的修改时间，并写回磁盘
    root_inode.modify_time = now;
    disk_file.seekp(get_inode_pos(0));
    disk_file.write((char*)&root_inode, sizeof(Inode));
    if (disk_file.fail()) {
        std::cerr << "警告：根目录修改时间更新失败，但文件已创建" << std::endl;
        // 此处不返回-1，因为文件已成功创建，仅元数据有小问题
    }

    std::cout << "文件 " << name << " 创建成功，inode：" << inode_num << std::endl;
    return inode_num;
}

/**
 * @brief 打开文件：根据文件名查找对应的inode编号
 * @param name 目标文件名
 * @return 成功返回文件的inode编号；失败返回-1（文件不存在或未挂载）
 * 打开文件本质是通过文件名找到inode，后续操作通过inode编号进行
 */
int DiskFS::open_file(const std::string& name) {
    if (!isMounted()) return -1;  // 未挂载则无法操作

    // 获取根目录中的所有文件条目
    std::vector<DirEntry> entries = list_files();
    // 遍历目录项，寻找文件名匹配且有效的条目
    for (const auto& entry : entries) {
        if (entry.valid && name == entry.name) {
            return entry.inode_num;  // 返回对应的inode编号
        }
    }
    return -1;  // 未找到文件
}

/**
 * @brief 读取文件内容
 * @param inode_num 目标文件的inode编号
 * @param buffer 接收数据的缓冲区（需预先分配足够空间）
 * @param size 期望读取的字节数
 * @param offset 读取的起始偏移量（从文件开头计算，单位：字节）
 * @return 成功返回实际读取的字节数；0表示已到文件末尾；-1表示失败（参数无效等）
 */
int DiskFS::read_file(int inode_num, char* buffer, size_t size, off_t offset) {
    // 检查前置条件：磁盘已挂载，inode编号有效
    if (!isMounted() || inode_num < 0 || (uint32_t)inode_num >= super_block.total_inodes) 
        return -1;

    // 读取目标文件的inode信息
    Inode inode;
    disk_file.seekg(get_inode_pos(inode_num));
    disk_file.read((char*)&inode, sizeof(Inode));
    // 检查inode状态：必须是已使用的普通文件（类型1）
    if (!inode.used || inode.type != 1) return -1;

    // 计算实际可读取的字节数（不能超过文件大小 - 偏移量）
    size_t max_read = inode.size - offset;
    if (max_read <= 0) return 0;  // 偏移量已超出文件大小，无数据可读
    size_t read_size = std::min(size, max_read);  // 取期望大小和最大可读取的较小值

    if (read_size == 0) return 0;  // 无需读取

    // 读取数据：按块读取，处理跨块情况
    char block_buffer[BLOCK_SIZE];  // 临时存储块数据的缓冲区
    size_t bytes_read = 0;          // 已读取的总字节数
    off_t current_offset = offset;  // 当前读取偏移量

    while (bytes_read < read_size) {
        // 计算当前偏移量所在的块索引（inode的blocks数组下标）
        uint32_t block_idx = current_offset / BLOCK_SIZE;
        // 若块索引超出inode的块指针范围（最多16个块），读取失败
        if (block_idx >= 16) break;

        uint32_t block_num = inode.blocks[block_idx];  // 数据块编号
        // 若块编号无效（未分配），读取失败
        if (block_num == 0) break;

        // 读取该数据块到临时缓冲区
        if (!read_block(block_num, block_buffer)) return -1;

        // 计算在块内的偏移量（当前偏移量 % 块大小）
        off_t in_block_offset = current_offset % BLOCK_SIZE;
        // 计算当前块可读取的字节数（块内剩余空间 vs 剩余需读取的字节数）
        size_t read_from_block = std::min(
            (size_t)(BLOCK_SIZE - in_block_offset),  // 块内剩余空间
            read_size - bytes_read                   // 还需读取的字节数
        );

        // 从块缓冲区复制数据到用户缓冲区
        memcpy(buffer + bytes_read, block_buffer + in_block_offset, read_from_block);
        bytes_read += read_from_block;       // 更新已读取字节数
        current_offset += read_from_block;   // 更新当前偏移量
    }

    return bytes_read;  // 返回实际读取的字节数
}

/**
 * @brief 写入文件内容
 * @param inode_num 目标文件的inode编号
 * @param buffer 存储待写入数据的缓冲区
 * @param size 待写入的字节数
 * @param offset 写入的起始偏移量（从文件开头计算，单位：字节）
 * @return 成功返回实际写入的字节数；-1表示失败（参数无效等）
 */
int DiskFS::write_file(int inode_num, const char* buffer, size_t size, off_t offset) {
    // 检查前置条件：磁盘已挂载，inode编号有效，缓冲区非空且有数据可写
    if (!isMounted() || inode_num < 0 || (uint32_t)inode_num >= super_block.total_inodes || 
        buffer == nullptr || size == 0) 
        return -1;

    // 读取目标文件的inode信息
    Inode inode;
    disk_file.seekg(get_inode_pos(inode_num));
    disk_file.read((char*)&inode, sizeof(Inode));
    // 检查inode状态：必须是已使用的普通文件（类型1）
    if (!inode.used || inode.type != 1) return -1;

    // 写入数据：按块写入，处理跨块和新块分配
    char block_buffer[BLOCK_SIZE];  // 临时存储块数据的缓冲区
    size_t bytes_written = 0;       // 已写入的总字节数
    off_t current_offset = offset;  // 当前写入偏移量
    time_t now = time(nullptr);     // 当前时间（用于更新修改时间）

    while (bytes_written < size) {
        // 计算当前偏移量所在的块索引（inode的blocks数组下标）
        uint32_t block_idx = current_offset / BLOCK_SIZE;
        // 若块索引超出最大支持的块数（16个），无法写入（简化设计，不支持间接块）
        if (block_idx >= 16) break;

        int block_num = inode.blocks[block_idx];  // 数据块编号
        // 若块未分配，尝试分配新块
        if (block_num == 0) {
            block_num = find_free_block();  // 查找空闲块
            if (block_num == -1) break;     // 无空闲块，写入失败
            inode.blocks[block_idx] = (uint32_t)block_num;  // 更新inode的块指针
            set_block_bitmap(block_num, true);    // 标记块为已使用
            // 初始化新块为0（避免残留数据）
            memset(block_buffer, 0, BLOCK_SIZE);
        } else {
            // 若块已分配，先读取原有数据（避免覆盖）
            if (!read_block(block_num, block_buffer)) return -1;
        }

        // 计算在块内的偏移量
        off_t in_block_offset = current_offset % BLOCK_SIZE;
        // 计算当前块可写入的字节数（块内剩余空间 vs 剩余需写入的字节数）
        size_t write_to_block = std::min(
            (size_t)(BLOCK_SIZE - in_block_offset),  // 块内剩余空间
            size - bytes_written                     // 还需写入的字节数
        );

        // 将数据从用户缓冲区复制到块缓冲区
        memcpy(block_buffer + in_block_offset, buffer + bytes_written, write_to_block);
        // 将更新后的块数据写回磁盘
        if (!write_block(block_num, block_buffer)) return -1;

        bytes_written += write_to_block;   // 更新已写入字节数
        current_offset += write_to_block;  // 更新当前偏移量
    }

    // 更新文件大小（若写入超出原大小）
    if (offset + bytes_written > inode.size) {
        inode.size = offset + bytes_written;
    }
    // 更新文件修改时间
    inode.modify_time = now;
    // 将更新后的inode写回磁盘
    disk_file.seekp(get_inode_pos(inode_num));
    disk_file.write((char*)&inode, sizeof(Inode));

    return bytes_written;  // 返回实际写入的字节数
}

/**
 * @brief 删除文件：释放inode、数据块，并从根目录中移除目录项
 * @param name 目标文件名
 * @return 成功返回true；失败返回false（文件不存在/未挂载等）
 */
bool DiskFS::delete_file(const std::string& name) {
    if (!isMounted()) return false;  // 未挂载则无法操作

    // 读取根目录inode（0号）
    Inode root_inode;
    disk_file.seekg(get_inode_pos(0));
    disk_file.read((char*)&root_inode, sizeof(Inode));
    if (root_inode.type != 2) return false;  // 根目录必须是目录类型

    // 读取根目录数据块，查找目标文件的目录项
    char buffer[BLOCK_SIZE];
    if (!read_block(root_inode.blocks[0], buffer)) return false;
    DirEntry* dir_entries = (DirEntry*)buffer;

    int target_inode = -1;  // 目标文件的inode编号
    size_t target_entry_idx = -1;  // 目标目录项在数组中的索引

    // 遍历目录项，寻找文件名匹配且有效的条目
    for (size_t i = 1; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (dir_entries[i].valid && name == dir_entries[i].name) {
            target_inode = dir_entries[i].inode_num;
            target_entry_idx = i;
            break;
        }
    }

    if (target_inode == -1) return false;  // 未找到文件

    // 读取目标文件的inode
    Inode file_inode;
    disk_file.seekg(get_inode_pos(target_inode));
    disk_file.read((char*)&file_inode, sizeof(Inode));
    if (!file_inode.used || file_inode.type != 1) return false;  // 必须是已使用的文件

    // 释放文件占用的数据块（遍历inode的块指针）
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t block_num = file_inode.blocks[i];
        if (block_num != 0) {
            set_block_bitmap(block_num, false);  // 标记块为空闲
            file_inode.blocks[i] = 0;  // 清空块指针
        }
    }

    // 标记inode为未使用
    file_inode.used = 0;
    disk_file.seekp(get_inode_pos(target_inode));
    disk_file.write((char*)&file_inode, sizeof(Inode));
    set_inode_bitmap(target_inode, false);  // 更新inode位图

    // 从根目录中移除该文件的目录项（标记为无效）
    dir_entries[target_entry_idx].valid = 0;
    write_block(root_inode.blocks[0], buffer);  // 写回根目录数据块

    // 更新根目录的修改时间
    root_inode.modify_time = time(nullptr);
    disk_file.seekp(get_inode_pos(0));
    disk_file.write((char*)&root_inode, sizeof(Inode));

    return true;
}

/**
 * @brief 列出根目录中的所有文件（有效目录项）
 * @return 包含所有有效目录项的向量（不含"."目录）
 */
std::vector<DirEntry> DiskFS::list_files() {
    std::vector<DirEntry> entries;  // 存储结果的向量

    if (!isMounted()) return entries;  // 未挂载则返回空

    // 读取根目录inode（0号）
    Inode root_inode;
    disk_file.seekg(get_inode_pos(0));
    disk_file.read((char*)&root_inode, sizeof(Inode));
    if (root_inode.type != 2) return entries;  // 根目录必须是目录类型

    // 读取根目录数据块
    char buffer[BLOCK_SIZE];
    if (!read_block(root_inode.blocks[0], buffer)) return entries;
    DirEntry* dir_entries = (DirEntry*)buffer;  // 转换为目录项数组

    // 遍历目录项，收集所有有效条目（跳过"."目录）
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (dir_entries[i].valid) {
            // 跳过"."目录（仅根目录有此条目）
            if (i == 0 && std::string(dir_entries[i].name) == ".") {
                continue;
            }
            entries.push_back(dir_entries[i]);  // 添加到结果向量
        }
    }

    return entries;
}

/**
 * @brief 打印磁盘的基本信息（总容量、空闲空间、inode使用情况等）
 */
void DiskFS::print_info()
{
    if (!isMounted())
    {
        std::cout << "请先挂载磁盘（使用mount命令）\n";
        return;
    }

    // 计算总容量和已使用容量（单位：MB）
    uint64_t total_size = (uint64_t)super_block.total_blocks * BLOCK_SIZE;
    uint64_t used_size = (uint64_t)(super_block.data_blocks - super_block.free_blocks) * BLOCK_SIZE;
    uint64_t free_size = (uint64_t)super_block.free_blocks * BLOCK_SIZE;

    std::cout << "磁盘信息:\n";
    std::cout << "  文件系统: " << super_block.magic << "\n";
    std::cout << "  块大小: " << super_block.block_size << " 字节\n";
    std::cout << "  总块数: " << super_block.total_blocks << "\n";
    std::cout << "  总容量: " << std::fixed << std::setprecision(2) 
              << (double)total_size / (1024 * 1024) << " MB\n";
    std::cout << "  已使用容量: " << std::fixed << std::setprecision(2) 
              << (double)used_size / (1024 * 1024) << " MB\n";
    std::cout << "  空闲容量: " << std::fixed << std::setprecision(2) 
              << (double)free_size / (1024 * 1024) << " MB\n";
    std::cout << "  总inode数: " << super_block.total_inodes << "\n";
    std::cout << "  已使用inode数: " << super_block.total_inodes - super_block.free_inodes << "\n";
    std::cout << "  空闲inode数: " << super_block.free_inodes << "\n";
}

int DiskFS::get_file_size(int inode_num) {
    if (!is_mounted || inode_num < 0 || (uint32_t)inode_num >= super_block.total_inodes) {
        return -1;
    }

    Inode inode;
    uint32_t pos = get_inode_pos(inode_num);
    disk_file.seekg(pos);
    disk_file.read((char*)&inode, sizeof(Inode));
    
    if (!disk_file.good() || !inode.used) {
        return -1;
    }

    return inode.size;
}

bool DiskFS::is_inode_used(uint32_t inode_num) const
{
    // 1. 校验inode编号有效性（超出范围视为未使用）
    if (inode_num >= super_block.total_inodes) 
    {
        std::cerr << "inode编号 " << inode_num << " 超出范围（总inode数：" << super_block.total_inodes << "）" << std::endl;
        return false;
    }

    // 2. 校验磁盘是否已挂载（未挂载无法读取inode）
    if (!is_mounted || !disk_file.is_open())
    {
        std::cerr << "磁盘未挂载或文件未打开，无法读取inode" << std::endl;
        return false;
    }

    // 3. 清除文件流错误状态（避免之前的错误影响当前操作）
    disk_file.clear();

    // 4. 调用现有get_inode_pos获取inode在磁盘中的位置（字节偏移量）
    uint32_t inode_offset = get_inode_pos(inode_num);
    // std::cerr << "读取inode " << inode_num << "，偏移量：" << inode_offset << std::endl;  // 调试用

    // 5. 定位到inode位置，并检查seek是否成功
    disk_file.seekg(inode_offset);
    if (disk_file.fail())
    {
        std::cerr << "inode " << inode_num << " 定位失败（偏移：" << inode_offset << "）" << std::endl;
        return false;
    }

    // 6. 读取inode数据（使用sizeof(Inode)确保读取完整，避免INODE_SIZE不一致问题）
    Inode inode;
    disk_file.read((char*)&inode, sizeof(Inode));

    // 7. 校验读取是否成功
    if (disk_file.fail()) {
        // std::cerr << "inode " << inode_num << " 读取失败（实际读取字节：" << disk_file.gcount() << "）" << std::endl;
        return false;
    }

    // 8. 调试输出（确认读取的used值）
    // std::cout << "inode " << inode_num << " 的used状态：" << "["  << (int)inode.used << "]"<<std::endl;

    return inode.used;
}