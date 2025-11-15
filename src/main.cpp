#include "../include/disk_fs.h"
#include "../include/command_parser.h"
#include "../include/task_queue.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

// 命令解析函数（将输入字符串转换为Task对象）
Task parse_command(const std::string& input) {
    Task task;
    task.completed = false;
    std::vector<std::string> tokens;
    std::stringstream ss(input);
    std::string token;

    // 分割输入为令牌（支持空格分隔）
    while (ss >> token) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        task.type = CommandType::EMPTY;
        return task;
    }

    // 判断命令类型
    std::string cmd = tokens[0];
    if (cmd == "ls") {
        task.type = CommandType::LS;
    } else if (cmd == "cat") {
        task.type = CommandType::CAT;
        if (tokens.size() > 1) task.args.push_back(tokens[1]);
    } else if (cmd == "rm") {
        task.type = CommandType::RM;
        if (tokens.size() > 1) task.args.push_back(tokens[1]);
    } else if (cmd == "copy") {
        task.type = CommandType::COPY;
        if (tokens.size() > 2) {
            task.args.push_back(tokens[1]);
            task.args.push_back(tokens[2]);
        }
    } else if (cmd == "write") {
        task.type = CommandType::WRITE;
        if (tokens.size() > 1) {
            task.args.push_back(tokens[1]);
            // 合并剩余部分作为内容（支持空格）
            for (size_t i = 2; i < tokens.size(); ++i) {
                task.args.push_back(tokens[i]);
            }
        }
    } else if (cmd == "touch" || cmd == "create") {
        task.type = CommandType::TOUCH;
        if (tokens.size() > 1) task.args.push_back(tokens[1]);
    } else if (cmd == "exit") {
        task.type = CommandType::EXIT;
    } else {
        task.type = CommandType::UNKNOWN;
    }

    return task;
}

int main() {
    DiskFS disk("disk.img");
    
    // 初始化磁盘
    std::cout << "格式化磁盘..." << std::endl;
    if (!disk.format()) {
        std::cerr << "磁盘格式化失败" << std::endl;
        return 1;
    }
    
    std::cout << "挂载磁盘..." << std::endl;
    if (!disk.mount()) {
        std::cerr << "磁盘挂载失败" << std::endl;
        return 1;
    }

    // 创建线程池（使用4个工作线程）
    ThreadPool pool(&disk, 4);
    std::cout << "多线程磁盘模拟器启动成功，支持命令：ls/cat/rm/copy/write/touch/exit" << std::endl;
    std::cout << "> " << std::flush;

    // 命令输入循环
    std::string input;
    while (std::getline(std::cin, input)) {
        Task task = parse_command(input);
        pool.add_task(task);

        // 若为退出命令，等待所有任务完成后退出
        if (task.type == CommandType::EXIT) {
            pool.wait_for_completion();
            break;
        }
    }

    // 卸载磁盘
    disk.unmount();
    return 0;
}