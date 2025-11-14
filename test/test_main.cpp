#include "test_bitmap_ops.cpp"
#include "test_block_ops.cpp"
#include "test_file_ops.cpp"
#include <iostream>

int main() {
    std::cout << "===== 开始磁盘模拟器测试 =====" << std::endl;

    test_bitmap_ops();
    test_block_ops();
    test_file_ops();

    std::cout << "\n===== 所有测试通过 =====" << std::endl;
    return 0;
}