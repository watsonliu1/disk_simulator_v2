#include "../include/disk_fs.h"
#include <cassert>
#include <cstring>
#include <iostream>

void test_file_ops() {
    std::cout << "\n===== 测试文件操作 =====" << std::endl;

    DiskFS disk("test_disk.img");
    assert(disk.format() == true);
    assert(disk.mount() == true);

    const std::string filename = "test_file.txt";
    int inode = disk.create_file(filename);
    assert(inode != -1 && "创建文件失败");
    assert(disk.open_file(filename) == inode && "打开文件失败（inode不匹配）");

    const char* data = "Hello, Disk Simulator!";
    int data_len = strlen(data);
    int written = disk.write_file(inode, data, data_len, 0);
    assert(written == data_len && "文件写入长度不匹配");
    assert(disk.get_file_size(inode) == data_len && "文件大小更新失败");

    char read_buf[1024] = {0};
    int read = disk.read_file(inode, read_buf, data_len, 0);
    assert(read == data_len && "文件读取长度不匹配");
    assert(strcmp(read_buf, data) == 0 && "文件内容读写不一致");

    assert(disk.delete_file(filename) == true && "删除文件失败");
    assert(disk.open_file(filename) == -1 && "文件删除后仍可打开");

    disk.unmount();
    std::cout << "文件操作测试通过" << std::endl;
}