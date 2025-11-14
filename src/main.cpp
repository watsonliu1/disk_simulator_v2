#include "../include/disk_fs.h"
#include "../include/command_parser.h"
#include "../include/task_queue.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <sstream>
#include <atomic>

void consumer_thread()
{
    while (running)
    {
        Task task;
        if (!get_task(task)) {
            break;
        }

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
                        task.result = "错误: 缺少文件名参数（用法：cat <文件名>）\n";
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
                        // 确保内容后加换行，避免和提示符连在一起
                        task.result = "文件内容:\n" + std::string(buffer) + "\n";
                    }
                    delete[] buffer;
                    task.completed = true;
                    break;
                }



                case CommandType::RM: {
                    if (task.args.size() < 1) {
                        task.result = "错误: 缺少文件名参数（用法：rm <文件名>）\n";
                        task.completed = true;
                        break;
                    }
                    std::string filename = task.args[0];
                    bool success = disk_ptr->delete_file(filename);
                    task.result = success ? "删除成功\n" : "删除失败（文件不存在或已删除）\n";
                    task.completed = true;
                    break;
                }

                case CommandType::COPY: {
                    if (task.args.size() < 2) {
                        task.result = "错误: 缺少参数（用法：copy <源文件> <目标文件>）\n";
                        task.completed = true;
                        break;
                    }
                    std::string src = task.args[0];
                    std::string dest = task.args[1];

                    int src_inode = disk_ptr->open_file(src);
                    if (src_inode == -1) {
                        task.result = "错误: 源文件不存在\n";
                        task.completed = true;
                        break;
                    }

                    int dest_inode = disk_ptr->create_file(dest);
                    if (dest_inode == -1) {
                        task.result = "错误: 目标文件创建失败（可能已存在）\n";
                        task.completed = true;
                        break;
                    }

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
                        disk_ptr->delete_file(dest);
                        delete[] buffer;
                        task.completed = true;
                        break;
                    }

                    int bytes_written = disk_ptr->write_file(dest_inode, buffer, bytes_read, 0);
                    delete[] buffer;

                    if (bytes_written != bytes_read) {
                        task.result = "错误: 写入目标文件失败\n";
                        disk_ptr->delete_file(dest);
                    } else {
                        task.result = "复制成功\n";
                    }
                    task.completed = true;
                    break;
                }
               
                // 新增：处理WRITE命令
                case CommandType::WRITE: {
                    if (task.args.size() < 2) {
                        task.result = "错误: 缺少参数（用法：write <文件名> <内容>，内容可加引号）\n";
                        task.completed = true;
                        break;
                    }
                    std::string filename = task.args[0];
                    std::string content;

                    // 处理带空格的内容（如果有多个参数，合并为一个字符串，支持引号内空格）
                    // 例如：write demo "hello world" → 合并args[1]和args[2]为"hello world"
                    for (size_t i = 1; i < task.args.size(); ++i) {
                        if (i > 1) content += " ";  // 多个参数间加空格
                        content += task.args[i];
                    }

                    // 去除首尾引号（如果有）
                    if (content.size() >= 2 && content.front() == '"' && content.back() == '"') {
                        content = content.substr(1, content.size() - 2);  // 正确截取引号内内容
                    }

                    // 后续写入逻辑不变...
                    int inode = disk_ptr->open_file(filename);
                    if (inode == -1) {
                        inode = disk_ptr->create_file(filename);
                        if (inode == -1) {
                            task.result = "错误: 创建文件失败\n";
                            task.completed = true;
                            break;
                        }
                    }

                    int bytes_written = disk_ptr->write_file(inode, content.c_str(), content.size(), 0);
                    if (bytes_written != (int)content.size()) {
                        task.result = "错误: 写入文件失败\n";
                    } else {
                        task.result = "写入成功（文件大小：" + std::to_string(content.size()) + "字节）\n";
                    }
                    task.completed = true;
                    break;
                }

                case CommandType::TOUCH: {
                    if (task.args.size() < 1) {
                        task.result = "错误: 缺少文件名（用法：touch/create <文件名>）\n";
                        task.completed = true;
                        break;
                    }
                    std::string filename = task.args[0];

                    int inode = disk_ptr->open_file(filename);
                    if (inode != -1) {
                        task.result = "文件已存在（修改时间已更新）\n";  // 精简提示
                        task.completed = true;
                        break;
                    }

                    inode = disk_ptr->create_file(filename);
                    if (inode == -1) {
                        task.result = "错误: 创建文件失败（可能文件名过长或根目录已满）\n";
                    } else {
                        // 只保留简洁提示，去除重复信息
                        task.result = "空文件创建成功（inode: " + std::to_string(inode) + "）\n";
                    }
                    task.completed = true;
                    break;
                }

                case CommandType::EMPTY:
                {
                    // 空输入（仅回车）：不显示任何信息，或提示"请输入命令"
                    task.result = "";  // 空结果，不输出
                    task.completed = true;
                    break;
                }

                case CommandType::EXIT:
                    task.result = "退出程序\n";
                    task.completed = true;
                    break;

                default:
                    task.result = "未知命令，支持命令：ls/cat/rm/copy/write/touch/exit \n";
                    task.completed = true;
            }
        } catch (...) {
            task.result = "错误: 命令执行异常\n";
            task.completed = true;
        }

         // 输出结果（空输入不输出）
        if (!task.result.empty()) {
            std::cout << task.result;
        }

        // 关键：任务执行完，打印下一个>（EXIT命令除外）
        if (task.type != CommandType::EXIT) {
            std::cout << "> " << std::flush;
        }

        if (task.type == CommandType::EXIT) {
            running = false;
            cv.notify_all();
        }
    }
}

int main() {
    std::string disk_path = "disk.img";
    DiskFS disk(disk_path);
    disk_ptr = &disk;

    std::cout << "正在初始化磁盘..." << std::endl;
    if (!disk.format()) {
        std::cerr << "磁盘初始化失败，退出程序" << std::endl;
        return 1;
    }
    if (!disk.mount()) {
        std::cerr << "磁盘挂载失败，退出程序" << std::endl;
        return 1;
    }

    std::thread consumer(consumer_thread);

    std::string input;
    std::cout << "磁盘模拟器就绪（支持命令：ls/cat/rm/copy/write/touch/exit）" << std::endl;
    std::cout << "> " << std::flush; 
    while (running) {
       
        std::getline(std::cin, input);

        std::vector<std::string> args;
        CommandType cmd_type = parse_command(input, args);

        Task task;
        task.type = cmd_type;
        task.args = args;
        task.completed = false;

        add_task(task);

        if (cmd_type == CommandType::EXIT) {
            break;
        }
    }

    if (consumer.joinable()) {
        consumer.join();
    }

    disk.unmount();
    return 0;
}