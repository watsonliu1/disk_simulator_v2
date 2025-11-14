#include "include/disk_fs.h"
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <sstream>
#include <atomic>

// 命令类型枚举
enum class CommandType {
    LS,
    CAT,
    RM,
    COPY,
    EXIT,
    UNKNOWN
};

// 任务结构体
struct Task {
    CommandType type;
    std::vector<std::string> args;
    std::string result;  // 存储命令执行结果
    bool completed;      // 任务是否完成
};

// 全局任务队列及同步机制
std::queue<Task> task_queue;
std::mutex queue_mutex;
std::condition_variable cv;
std::atomic<bool> running(true);  // 控制线程运行状态
DiskFS* disk_ptr = nullptr;       // 全局磁盘指针（线程安全访问）
std::mutex disk_mutex;            // 磁盘操作互斥锁

// 消费者线程处理函数
void consumer_thread() {
    while (running) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // 等待任务或退出信号
        cv.wait(lock, []{ return !task_queue.empty() || !running; });
        
        if (!running && task_queue.empty()) break;
        if (task_queue.empty()) continue;

        // 取出任务
        Task task = task_queue.front();
        task_queue.pop();
        lock.unlock();

        // 执行任务
        try {
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            switch (task.type) {
                case CommandType::LS: {
                    std::vector<DirEntry> entries = disk_ptr->list_files();
                    std::stringstream ss;
                    ss << "文件列表:\n";
                    for (const auto& entry : entries) {
                        if (entry.valid && entry.inode_num != 0) {
                            ss << "  " << entry.name << " (inode: " << entry.inode_num << ")\n";
                        }
                    }
                    task.result = ss.str();
                    task.completed = true;
                    break;
                }

                case CommandType::CAT: {
                    if (task.args.size() < 1) {
                        task.result = "错误: 缺少文件名参数\n";
                        task.completed = true;
                        break;
                    }
                    std::string filename = task.args[0];
                    int inode = disk_ptr->open_file(filename);
                    if (inode == -1) {
                        task.result = "错误: 文件不存在\n";
                        task.completed = true;
                        break;
                    }
                    
                    int file_size = disk_ptr->get_file_size(inode);
                    if (file_size <= 0) {
                        task.result = "文件为空\n";
                        task.completed = true;
                        break;
                    }

                    char* buffer = new char[file_size + 1];
                    int bytes_read = disk_ptr->read_file(inode, buffer, file_size, 0);
                    if (bytes_read < 0) {
                        task.result = "错误: 读取文件失败\n";
                    } else {
                        buffer[bytes_read] = '\0';
                        task.result = "文件内容:\n" + std::string(buffer);
                    }
                    delete[] buffer;
                    task.completed = true;
                    break;
                }

                case CommandType::RM: {
                    if (task.args.size() < 1) {
                        task.result = "错误: 缺少文件名参数\n";
                        task.completed = true;
                        break;
                    }
                    std::string filename = task.args[0];
                    bool success = disk_ptr->delete_file(filename);
                    task.result = success ? "删除成功\n" : "删除失败\n";
                    task.completed = true;
                    break;
                }

                case CommandType::COPY: {
                    if (task.args.size() < 2) {
                        task.result = "错误: 缺少源文件或目标文件参数\n";
                        task.completed = true;
                        break;
                    }
                    std::string src = task.args[0];
                    std::string dest = task.args[1];

                    // 打开源文件
                    int src_inode = disk_ptr->open_file(src);
                    if (src_inode == -1) {
                        task.result = "错误: 源文件不存在\n";
                        task.completed = true;
                        break;
                    }

                    // 创建目标文件
                    int dest_inode = disk_ptr->create_file(dest);
                    if (dest_inode == -1) {
                        task.result = "错误: 目标文件创建失败\n";
                        task.completed = true;
                        break;
                    }

                    // 读取源文件内容
                    int file_size = disk_ptr->get_file_size(src_inode);
                    if (file_size <= 0) {
                        task.result = "源文件为空，复制完成\n";
                        task.completed = true;
                        break;
                    }

                    char* buffer = new char[file_size];
                    int bytes_read = disk_ptr->read_file(src_inode, buffer, file_size, 0);
                    
                    if (bytes_read < 0) {
                        task.result = "错误: 读取源文件失败\n";
                        disk_ptr->delete_file(dest);  // 清理
                        delete[] buffer;
                        task.completed = true;
                        break;
                    }

                    // 写入目标文件
                    int bytes_written = disk_ptr->write_file(dest_inode, buffer, bytes_read, 0);
                    delete[] buffer;

                    if (bytes_written != bytes_read) {
                        task.result = "错误: 写入目标文件失败\n";
                        disk_ptr->delete_file(dest);  // 清理
                    } else {
                        task.result = "复制成功\n";
                    }
                    task.completed = true;
                    break;
                }

                case CommandType::EXIT:
                    task.result = "退出程序\n";
                    task.completed = true;
                    break;

                default:
                    task.result = "未知命令\n";
                    task.completed = true;
            }
        } catch (...) {
            task.result = "命令执行出错\n";
            task.completed = true;
        }

        // 打印执行结果
        std::cout << "\n" << task.result << "> ";
        std::cout.flush();
    }
}

// 原有 print_help() 函数
void print_help() {
    std::cout << "多线程磁盘模拟文件系统命令:\n";
    std::cout << "  ls          - 列出文件\n";
    std::cout << "  cat <文件>  - 查看文件内容\n";
    std::cout << "  rm <文件>   - 删除文件\n";
    std::cout << "  copy <源> <目标> - 复制文件\n";
    std::cout << "  format      - 格式化磁盘\n";
    std::cout << "  mount       - 挂载磁盘\n";
    std::cout << "  umount      - 卸载磁盘\n";
    std::cout << "  info        - 显示磁盘信息\n";
    std::cout << "  create <文件名> - 创建文件\n";
    std::cout << "  help        - 显示帮助\n";
    std::cout << "  exit        - 退出\n";
}

// 测试用例实现
bool run_tests(DiskFS& disk) {
    int test_count = 0;
    int pass_count = 0;
    std::cout << "\n===== 开始自动测试 =====" << std::endl;

    // 测试1: 格式化磁盘
    test_count++;
    bool format_ok = disk.format();
    std::cout << "测试" << test_count << "(格式化): " << (format_ok ? "通过" : "失败") << std::endl;
    if (format_ok) pass_count++;

    // 测试2: 挂载磁盘
    test_count++;
    bool mount_ok = disk.mount();
    std::cout << "测试" << test_count << "(挂载): " << (mount_ok ? "通过" : "失败") << std::endl;
    if (mount_ok) pass_count++;

    // 测试3: 创建文件
    test_count++;
    int temp_inode = disk.create_file("test1.txt");
    uint32_t inode1 = static_cast<uint32_t>(temp_inode);
    bool create_ok = (temp_inode != -1);
    std::cout << "测试" << test_count << "(创建文件): " << (create_ok ? "通过" : "失败") << std::endl;
    if (create_ok) pass_count++;

    // 测试4: 禁止创建同名文件
    test_count++;
    int inode_dup = disk.create_file("test1.txt");
    bool no_dup_ok = (inode_dup == -1);
    std::cout << "测试" << test_count << "(禁止同名文件): " << (no_dup_ok ? "通过" : "失败") << std::endl;
    if (no_dup_ok) pass_count++;

    // 测试5: 写入文件
    test_count++;
    std::string content = "hello, disk fs!";
    int write_size = disk.write_file(inode1, content.c_str(), content.size(), 0);
    bool write_ok = (write_size == (int)content.size());
    std::cout << "测试" << test_count << "(写入文件): " << (write_ok ? "通过" : "失败") << std::endl;
    if (write_ok) pass_count++;

    // 测试6: 读取文件
    test_count++;
    char* read_buf = new char[content.size() + 1];
    int read_size = disk.read_file(inode1, read_buf, content.size(), 0);
    read_buf[read_size] = '\0';
    bool read_ok = (read_size == (int)content.size() && std::string(read_buf) == content);
    std::cout << "测试" << test_count << "(读取文件): " << (read_ok ? "通过" : "失败") << std::endl;
    if (read_ok) pass_count++;
    delete[] read_buf;

    // 测试7: 列出文件
    test_count++;
    std::vector<DirEntry> entries = disk.list_files();
    bool ls_ok = false;
    for (const auto& entry : entries) {
        if (entry.valid && std::string(entry.name) == "test1.txt" && entry.inode_num == inode1) {
            ls_ok = true;
            break;
        }
    }
    std::cout << "测试" << test_count << "(列出文件): " << (ls_ok ? "通过" : "失败") << std::endl;
    if (ls_ok) pass_count++;

    // 测试8: 删除文件
    test_count++;
    bool delete_ok = disk.delete_file("test1.txt");
    std::cout << "测试" << test_count << "(删除文件): " << (delete_ok ? "通过" : "失败") << std::endl;
    if (delete_ok) pass_count++;

    // 测试9: 验证文件已删除
    test_count++;
    int deleted_inode = disk.open_file("test1.txt");
    bool verify_delete_ok = (deleted_inode == -1);
    std::cout << "测试" << test_count << "(验证删除): " << (verify_delete_ok ? "通过" : "失败") << std::endl;
    if (verify_delete_ok) pass_count++;

    // 测试10: 卸载磁盘
    test_count++;
    bool unmount_ok = disk.unmount();
    std::cout << "测试" << test_count << "(卸载): " << (unmount_ok ? "通过" : "失败") << std::endl;
    if (unmount_ok) pass_count++;

    // 输出测试总结
    std::cout << "\n===== 测试总结 =====" << std::endl;
    std::cout << "总测试数: " << test_count << std::endl;
    std::cout << "通过数: " << pass_count << std::endl;
    std::cout << "失败数: " << (test_count - pass_count) << std::endl;

    return (test_count == pass_count);
}

int main(int argc, char* argv[]) {
    // 支持测试模式: ./sim_disk <磁盘文件> --test
    if (argc == 3 && std::string(argv[2]) == "--test") {
        DiskFS disk(argv[1]);
        bool all_passed = run_tests(disk);
        return all_passed ? 0 : 1;
    }

    // 交互模式
    if (argc != 2) {
        std::cerr << "用法: " << argv[0] << " <磁盘文件>\n";
        std::cerr << "测试模式: " << argv[0] << " <磁盘文件> --test\n";
        return 1;
    }

    DiskFS disk(argv[1]);
    disk_ptr = &disk;

    // 尝试挂载磁盘
    if (!disk.mount()) {
        std::cerr << "挂载磁盘失败，尝试格式化...\n";
        if (!disk.format() || !disk.mount()) {
            std::cerr << "格式化并挂载磁盘失败，退出\n";
            return 1;
        }
    }

    // 启动消费者线程（3个消费者）
    const int num_consumers = 3;
    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back(consumer_thread);
    }

    // 打印帮助信息
    print_help();
    std::cout << "> ";
    std::cout.flush();

    // 生产者逻辑：读取命令并放入队列
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string token;

        while (iss >> token) {
            tokens.push_back(token);
        }

        if (tokens.empty()) {
            std::cout << "> ";
            std::cout.flush();
            continue;
        }

        Task task;
        task.completed = false;

        // 解析命令
        if (tokens[0] == "ls") {
            task.type = CommandType::LS;
        } else if (tokens[0] == "cat" && tokens.size() >= 2) {
            task.type = CommandType::CAT;
            task.args.push_back(tokens[1]);
        } else if (tokens[0] == "rm" && tokens.size() >= 2) {
            task.type = CommandType::RM;
            task.args.push_back(tokens[1]);
        } else if (tokens[0] == "copy" && tokens.size() >= 3) {
            task.type = CommandType::COPY;
            task.args.push_back(tokens[1]);
            task.args.push_back(tokens[2]);
        } else if (tokens[0] == "exit") {
            task.type = CommandType::EXIT;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                task_queue.push(task);
            }
            cv.notify_one();
            break;  // 退出循环
        } else if (tokens[0] == "format") {
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            bool success = disk.format();
            std::cout << (success ? "格式化成功\n" : "格式化失败\n") << "> ";
            continue;
        } else if (tokens[0] == "mount") {
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            bool success = disk.mount();
            std::cout << (success ? "挂载成功\n" : "挂载失败\n") << "> ";
            continue;
        } else if (tokens[0] == "umount") {
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            bool success = disk.unmount();
            std::cout << (success ? "卸载成功\n" : "卸载失败\n") << "> ";
            continue;
        } else if (tokens[0] == "info") {
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            disk.print_info();
            std::cout << "> ";
            continue;
        } else if (tokens[0] == "create" && tokens.size() >= 2) {
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            int inode = disk.create_file(tokens[1]);
            std::cout << (inode != -1 ? "创建成功，inode: " + std::to_string(inode) + "\n" : "创建失败\n") << "> ";
            continue;
        } else if (tokens[0] == "help") {
            print_help();
            std::cout << "> ";
            continue;
        } else {
            std::cout << "未知命令，请输入help查看帮助\n> ";
            std::cout.flush();
            continue;
        }

        // 将任务加入队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            task_queue.push(task);
        }
        cv.notify_one();

        std::cout << "> ";
        std::cout.flush();
    }

    // 等待所有任务完成并退出线程
    running = false;
    cv.notify_all();
    for (auto& t : consumers) {
        if (t.joinable()) {
            t.join();
        }
    }

    // 卸载磁盘
    if (disk.isMounted()) {
        disk.unmount();
    }

    return 0;
}