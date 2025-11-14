#include "../include/disk_fs.h"
#include <cstring>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>

DiskFS::DiskFS(const std::string& path) : disk_path(path), is_mounted(false) {}

DiskFS::~DiskFS()
{
    if (is_mounted)
    {
        unmount();
    }
}

uint32_t DiskFS::get_inode_pos(uint32_t inode_num) {
    if (inode_num >= super_block.total_inodes) return 0;
    return super_block.inode_start * BLOCK_SIZE + inode_num * INODE_SIZE;
}

uint32_t DiskFS::get_data_block_pos(uint32_t block_num) {
    if (block_num < super_block.data_start || block_num >= super_block.total_blocks) return 0;
    return super_block.data_start * BLOCK_SIZE + (block_num - super_block.data_start) * BLOCK_SIZE;
}

bool DiskFS::set_block_bitmap(uint32_t block_num, bool used) {
    uint32_t data_end = super_block.data_start + super_block.data_blocks;
    if (block_num < super_block.data_start || block_num >= data_end) {
        return false;
    }

    uint32_t idx = block_num - super_block.data_start;
    uint32_t bits_per_block = BLOCK_SIZE * 8;
    uint32_t bitmap_block_idx = idx / bits_per_block;

    uint32_t block_bitmap_total_bytes = (MAX_BLOCKS + 7) / 8;
    uint32_t block_bitmap_size = (block_bitmap_total_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    if (bitmap_block_idx >= block_bitmap_size) {
        return false;
    }

    uint32_t target_bitmap_block = super_block.block_bitmap + bitmap_block_idx;
    char buffer[BLOCK_SIZE];
    if (!read_block(target_bitmap_block, buffer)) {
        return false;
    }

    uint32_t bit_in_block = idx % bits_per_block;
    uint32_t byte = bit_in_block / 8;
    uint8_t bit = bit_in_block % 8;

    if (byte >= BLOCK_SIZE) {
        return false;
    }

    if (used) {
        bool is_currently_free = !(buffer[byte] & (1 << bit));
        buffer[byte] |= (1 << bit);
        if (is_currently_free) {
            super_block.free_blocks--;
        }
    } else {
        bool is_currently_used = (buffer[byte] & (1 << bit));
        buffer[byte] &= ~(1 << bit);
        if (is_currently_used) {
            super_block.free_blocks++;
        }
    }

    if (!write_block(target_bitmap_block, buffer)) {
        return false;
    }

    if (!write_super_block()) {
        return false;
    }

    return true;
}

bool DiskFS::write_super_block() 
{
    disk_file.seekp(0);
    disk_file.write((char*)&super_block, sizeof(SuperBlock));
    return disk_file.good();
}

bool DiskFS::set_inode_bitmap(uint32_t inode_num, bool used)
{
    if (inode_num >= super_block.total_inodes) {
        return false;
    }

    uint32_t bits_per_block = BLOCK_SIZE * 8;
    uint32_t bitmap_block_idx = inode_num / bits_per_block;

    uint32_t inode_bitmap_total_bytes = (MAX_INODES + 7) / 8;
    uint32_t inode_bitmap_size = (inode_bitmap_total_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    if (bitmap_block_idx >= inode_bitmap_size) {
        return false;
    }

    uint32_t target_bitmap_block = super_block.inode_bitmap + bitmap_block_idx;
    char buffer[BLOCK_SIZE];
    if (!read_block(target_bitmap_block, buffer)) {
        return false;
    }

    uint32_t bit_in_block = inode_num % bits_per_block;
    uint32_t byte = bit_in_block / 8;
    uint8_t bit = bit_in_block % 8;

    if (byte >= BLOCK_SIZE) {
        return false;
    }

    if (used) {
        bool is_currently_free = !(buffer[byte] & (1 << bit));
        buffer[byte] |= (1 << bit);
        if (is_currently_free) {
            super_block.free_inodes--;
        }
    } else {
        bool is_currently_used = (buffer[byte] & (1 << bit));
        buffer[byte] &= ~(1 << bit);
        if (is_currently_used) {
            super_block.free_inodes++;
        }
    }

    if (!write_block(target_bitmap_block, buffer)) {
        return false;
    }

    if (!write_super_block()) {
        return false;
    }

    return true;
}

int DiskFS::find_free_block() {
    char buffer[BLOCK_SIZE];
    if (!read_block(super_block.block_bitmap, buffer)) return -1;

    uint32_t total_data_blocks = super_block.data_blocks;
    
    for (uint32_t i = 0; i < total_data_blocks; i++) {
        uint32_t byte = i / 8;
        uint8_t bit = i % 8;

        if (!(buffer[byte] & (1 << bit))) {
            return super_block.data_start + i;
        }
    }
    return -1;
}

int DiskFS::find_free_inode() {
    char buffer[BLOCK_SIZE];
    if (!read_block(super_block.inode_bitmap, buffer)) return -1;

    for (uint32_t i = 0; i < super_block.total_inodes; i++) {
        uint32_t byte = i / 8;
        uint8_t bit = i % 8;

        if (!(buffer[byte] & (1 << bit))) {
            return i;
        }
    }
    return -1;
}

bool DiskFS::read_block(uint32_t block_num, char* buffer) {
    if (block_num >= super_block.total_blocks) return false;
    disk_file.seekg(block_num * BLOCK_SIZE);
    disk_file.read(buffer, BLOCK_SIZE);
    return disk_file.good();
}

bool DiskFS::write_block(uint32_t block_num, const char* buffer) {
    if (block_num >= super_block.total_blocks) return false;
    disk_file.seekp(block_num * BLOCK_SIZE);
    disk_file.write(buffer, BLOCK_SIZE);
    return disk_file.good();
}

bool DiskFS::format() {
    disk_file.open(disk_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk_file.is_open()) {
        disk_file.open(disk_path, std::ios::trunc | std::ios::in | std::ios::out | std::ios::binary);
        if (!disk_file.is_open()) return false;
    }

    // 初始化超级块
    memset(&super_block, 0, sizeof(SuperBlock));
    strcpy(super_block.magic, "SIMFSv1");
    super_block.block_size = BLOCK_SIZE;
    super_block.total_blocks = MAX_BLOCKS;
    super_block.total_inodes = MAX_INODES;

    // 计算各区域大小
    uint32_t inode_bitmap_blocks = (MAX_INODES + (BLOCK_SIZE * 8 - 1)) / (BLOCK_SIZE * 8);
    uint32_t block_bitmap_blocks = (MAX_BLOCKS + (BLOCK_SIZE * 8 - 1)) / (BLOCK_SIZE * 8);
    super_block.inode_blocks = (MAX_INODES * INODE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // 计算各区域起始位置
    super_block.block_bitmap = 1;  // 超级块占1块
    super_block.inode_bitmap = super_block.block_bitmap + block_bitmap_blocks;
    super_block.inode_start = super_block.inode_bitmap + inode_bitmap_blocks;
    super_block.data_start = super_block.inode_start + super_block.inode_blocks;
    super_block.data_blocks = super_block.total_blocks - super_block.data_start;

    // 初始化空闲计数
    super_block.free_blocks = super_block.data_blocks;
    super_block.free_inodes = super_block.total_inodes;

    // 写超级块
    disk_file.seekp(0);
    disk_file.write((char*)&super_block, sizeof(SuperBlock));

    // 初始化块位图（全部置0）
    char* zero_buf = new char[BLOCK_SIZE]();
    for (uint32_t i = 0; i < block_bitmap_blocks; i++) {
        write_block(super_block.block_bitmap + i, zero_buf);
    }

    // 初始化inode位图（全部置0）
    for (uint32_t i = 0; i < inode_bitmap_blocks; i++) {
        write_block(super_block.inode_bitmap + i, zero_buf);
    }

    // 初始化inode区（全部置0）
    for (uint32_t i = 0; i < super_block.inode_blocks; i++) {
        write_block(super_block.inode_start + i, zero_buf);
    }

    delete[] zero_buf;
    disk_file.close();
    return true;
}

bool DiskFS::mount() {
    if (is_mounted) return true;

    disk_file.open(disk_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk_file.is_open()) return false;

    // 读取超级块
    disk_file.seekg(0);
    disk_file.read((char*)&super_block, sizeof(SuperBlock));

    // 验证文件系统标识
    if (strncmp(super_block.magic, "SIMFSv1", 7) != 0) {
        disk_file.close();
        return false;
    }

    is_mounted = true;
    return true;
}

bool DiskFS::unmount() {
    if (!is_mounted) return true;

    write_super_block();  // 确保超级块更新
    disk_file.close();
    is_mounted = false;
    return true;
}

int DiskFS::create_file(const std::string& name) {
    if (!is_mounted || name.size() >= MAX_FILENAME) return -1;

    // 检查文件是否已存在
    std::vector<DirEntry> entries = list_files();
    for (const auto& e : entries) {
        if (e.valid && name == e.name) return -1;
    }

    // 分配inode
    int inode_num = find_free_inode();
    if (inode_num == -1) return -1;

    // 初始化inode
    Inode inode;
    memset(&inode, 0, sizeof(Inode));
    inode.inode_num = inode_num;
    inode.type = 1;  // 文件类型
    inode.used = 1;
    inode.create_time = inode.modify_time = time(nullptr);

    // 写inode到磁盘
    disk_file.seekp(get_inode_pos(inode_num));
    disk_file.write((char*)&inode, sizeof(Inode));

    // 更新inode位图
    set_inode_bitmap(inode_num, true);

    // 添加目录项
    DirEntry entry;
    memset(&entry, 0, sizeof(DirEntry));
    strncpy(entry.name, name.c_str(), MAX_FILENAME - 1);
    entry.inode_num = inode_num;
    entry.valid = 1;

    // 简化处理：写到第一个数据块
    int data_block = find_free_block();
    if (data_block == -1) {
        set_inode_bitmap(inode_num, false);
        return -1;
    }

    set_block_bitmap(data_block, true);
    
    // 读取现有目录项
    char buffer[BLOCK_SIZE] = {0};
    read_block(data_block, buffer);
    std::vector<DirEntry> dir_entries;
    int entry_count = BLOCK_SIZE / sizeof(DirEntry);
    
    // 修复：循环变量i改为size_t（匹配vector.size()的无符号类型）
    for (size_t i = 0; i < static_cast<size_t>(entry_count); i++) {
        DirEntry* e = (DirEntry*)(buffer + i * sizeof(DirEntry));
        if (e->valid) dir_entries.push_back(*e);
    }

    // 添加新目录项
    dir_entries.push_back(entry);
    
    // 写回目录块（修复循环变量类型）
    memset(buffer, 0, BLOCK_SIZE);
    for (size_t i = 0; i < dir_entries.size() && i < static_cast<size_t>(entry_count); i++) {
        memcpy(buffer + i * sizeof(DirEntry), &dir_entries[i], sizeof(DirEntry));
    }
    write_block(data_block, buffer);

    // 更新根目录inode（简化：假设根目录inode为0）
    Inode root_inode;
    disk_file.seekg(get_inode_pos(0));
    disk_file.read((char*)&root_inode, sizeof(Inode));
    root_inode.blocks[0] = data_block;
    root_inode.size = dir_entries.size() * sizeof(DirEntry);
    root_inode.modify_time = time(nullptr);
    disk_file.seekp(get_inode_pos(0));
    disk_file.write((char*)&root_inode, sizeof(Inode));

    return inode_num;
}

int DiskFS::open_file(const std::string& name) {
    if (!is_mounted) return -1;

    std::vector<DirEntry> entries = list_files();
    for (const auto& e : entries) {
        if (e.valid && name == e.name) {
            return e.inode_num;
        }
    }
    return -1;
}

int DiskFS::read_file(int inode_num, char* buffer, size_t size, off_t offset) {
    if (!is_mounted || inode_num < 0 || (uint32_t)inode_num >= super_block.total_inodes) return -1;

    Inode inode;
    disk_file.seekg(get_inode_pos(inode_num));
    disk_file.read((char*)&inode, sizeof(Inode));
    if (!inode.used) return -1;

    if (offset >= (off_t)inode.size) return 0;
    if (offset + size > (off_t)inode.size) size = inode.size - offset;

    int total_read = 0;
    uint32_t block_idx = offset / BLOCK_SIZE;
    uint32_t block_offset = offset % BLOCK_SIZE;

    while (total_read < (int)size && block_idx < 16) {
        uint32_t block_num = inode.blocks[block_idx];
        if (block_num == 0) break;

        char block_buf[BLOCK_SIZE];
        if (!read_block(block_num, block_buf)) return -1;

        int read_size = std::min((int)size - total_read, (int)(BLOCK_SIZE - block_offset));
        memcpy(buffer + total_read, block_buf + block_offset, read_size);
        
        total_read += read_size;
        block_idx++;
        block_offset = 0;
    }

    return total_read;
}

int DiskFS::write_file(int inode_num, const char* buffer, size_t size, off_t offset) {
    if (!is_mounted || inode_num < 0 || (uint32_t)inode_num >= super_block.total_inodes) return -1;

    Inode inode;
    disk_file.seekg(get_inode_pos(inode_num));
    disk_file.read((char*)&inode, sizeof(Inode));
    if (!inode.used) return -1;

    int total_written = 0;
    uint32_t block_idx = offset / BLOCK_SIZE;
    uint32_t block_offset = offset % BLOCK_SIZE;

    while (total_written < (int)size && block_idx < 16) {
        uint32_t block_num = inode.blocks[block_idx];
        
        // 修复：先以int接收find_free_block()的返回值，避免无符号与-1比较
        if (block_num == 0) {
            int free_block = find_free_block();  // 先用int接收可能的-1
            if (free_block == -1) return -1;     // 检查无效值
            block_num = static_cast<uint32_t>(free_block);  // 安全转换为无符号
            
            set_block_bitmap(block_num, true);
            inode.blocks[block_idx] = block_num;
        }

        char block_buf[BLOCK_SIZE];
        read_block(block_num, block_buf);  // 读取现有数据

        int write_size = std::min((int)size - total_written, (int)(BLOCK_SIZE - block_offset));
        memcpy(block_buf + block_offset, buffer + total_written, write_size);
        write_block(block_num, block_buf);
        
        total_written += write_size;
        block_idx++;
        block_offset = 0;
    }

    // 更新inode信息
    inode.modify_time = time(nullptr);
    if (offset + total_written > (off_t)inode.size) {
        inode.size = offset + total_written;
    }
    
    disk_file.seekp(get_inode_pos(inode_num));
    disk_file.write((char*)&inode, sizeof(Inode));

    return total_written;
}

bool DiskFS::delete_file(const std::string& name) {
    if (!is_mounted) return false;

    // 查找文件（修复循环变量类型）
    std::vector<DirEntry> entries = list_files();
    int target_inode = -1;
    size_t entry_idx = -1;  // 改为size_t匹配vector索引
    
    for (size_t i = 0; i < entries.size(); i++) {  // i改为size_t
        if (entries[i].valid && name == entries[i].name) {
            target_inode = entries[i].inode_num;
            entry_idx = i;
            break;
        }
    }
    
    if (target_inode == -1) return false;

    // 释放inode
    Inode inode;
    disk_file.seekg(get_inode_pos(target_inode));
    disk_file.read((char*)&inode, sizeof(Inode));

    // 释放数据块
    for (uint32_t i = 0; i < 16; i++) {
        if (inode.blocks[i] != 0) {
            set_block_bitmap(inode.blocks[i], false);
        }
    }

    // 标记inode为空闲
    set_inode_bitmap(target_inode, false);

    // 从目录中移除（删除未使用的entry_count变量）
    int data_block = find_free_block();  // 简化：假设目录在第一个数据块
    if (data_block == -1) return false;

    char buffer[BLOCK_SIZE];
    read_block(data_block, buffer);
    // 删除未使用的变量定义：int entry_count = BLOCK_SIZE / sizeof(DirEntry);
    DirEntry* dir_entry = (DirEntry*)(buffer + entry_idx * sizeof(DirEntry));
    dir_entry->valid = 0;
    write_block(data_block, buffer);

    return true;
}

std::vector<DirEntry> DiskFS::list_files() {
    std::vector<DirEntry> entries;
    if (!is_mounted) return entries;

    // 简化：读取根目录（inode 0）的第一个数据块
    Inode root_inode;
    disk_file.seekg(get_inode_pos(0));
    disk_file.read((char*)&root_inode, sizeof(Inode));

    if (root_inode.blocks[0] == 0) return entries;

    char buffer[BLOCK_SIZE];
    read_block(root_inode.blocks[0], buffer);
    int entry_count = BLOCK_SIZE / sizeof(DirEntry);

    for (int i = 0; i < entry_count; i++) {
        DirEntry* e = (DirEntry*)(buffer + i * sizeof(DirEntry));
        if (e->valid && e->inode_num != 0) {  // 排除.目录
            entries.push_back(*e);
        }
    }

    return entries;
}

void DiskFS::print_info() {
    if (!is_mounted) {
        std::cout << "磁盘未挂载\n";
        return;
    }

    std::cout << "磁盘信息:\n";
    std::cout << "  标识: " << super_block.magic << "\n";
    std::cout << "  块大小: " << super_block.block_size << "字节\n";
    std::cout << "  总块数: " << super_block.total_blocks << "\n";
    std::cout << "  数据块数: " << super_block.data_blocks << "\n";
    std::cout << "  空闲块数: " << super_block.free_blocks << "\n";
    std::cout << "  总inode数: " << super_block.total_inodes << "\n";
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