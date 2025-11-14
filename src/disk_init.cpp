#include "../include/disk_fs.h"
#include <cstring>
#include <iostream>
#include <ctime>

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
