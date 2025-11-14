#include "../include/disk_fs.h"
#include <cassert>
#include <iostream>

void test_bitmap_ops() {
    std::cout << "\n===== 测试位图操作 =====" << std::endl;

    DiskFS disk("test_disk.img");
    assert(disk.format() == true && "格式化磁盘失败");
    assert(disk.mount() == true && "挂载磁盘失败");

    int free_block = disk.find_free_block();
    assert(free_block != -1 && "查找空闲块失败");
    assert(disk.set_block_bitmap(free_block, true) == true && "标记块为使用失败");
    assert(disk.find_free_block() != free_block && "块位图标记后仍能找到已使用块");
    assert(disk.set_block_bitmap(free_block, false) == true && "释放块失败");
    assert(disk.find_free_block() == free_block && "释放块后未恢复为空闲");

    int free_inode = disk.find_free_inode();
    assert(free_inode != -1 && "查找空闲inode失败");
    assert(disk.set_inode_bitmap(free_inode, true) == true && "标记inode为使用失败");
    assert(disk.find_free_inode() != free_inode && "inode位图标记后仍能找到已使用inode");
    assert(disk.set_inode_bitmap(free_inode, false) == true && "释放inode失败");
    assert(disk.find_free_inode() == free_inode && "释放inode后未恢复为空闲");

    disk.unmount();
    std::cout << "位图操作测试通过" << std::endl;
}