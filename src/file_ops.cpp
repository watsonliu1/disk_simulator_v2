#include "../include/disk_fs.h"
#include <cstring>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>

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
