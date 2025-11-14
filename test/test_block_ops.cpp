#include "../include/disk_fs.h"
#include <cassert>
#include <cstring>
#include <iostream>

void test_block_ops() {
    std::cout << "\n===== 测试块读写 =====" << std::endl;

    DiskFS disk("test_disk.img");
    assert(disk.format() == true);
    assert(disk.mount() == true);

    int block_num = disk.find_free_block();
    assert(block_num != -1);
    assert(disk.set_block_bitmap(block_num, true) == true);

    char write_buf[BLOCK_SIZE];
    memset(write_buf, 'A', BLOCK_SIZE);
    assert(disk.write_block(block_num, write_buf) == true && "写入块失败");

    char read_buf[BLOCK_SIZE];
    assert(disk.read_block(block_num, read_buf) == true && "读取块失败");
    assert(memcmp(write_buf, read_buf, BLOCK_SIZE) == 0 && "块数据读写不一致");

    disk.set_block_bitmap(block_num, false);
    disk.unmount();
    std::cout << "块读写测试通过" << std::endl;
}