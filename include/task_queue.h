#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <queue>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <iostream>
#include <sstream>
#include "disk_fs.h"
#include "command_parser.h"

// 任务结构体：封装命令信息与执行状态
struct Task {
    CommandType type;               // 命令类型（如LS、COPY）
    std::vector<std::string> args;  // 命令参数
    std::string result;             // 执行结果
    bool completed;                 // 完成标记
    std::chrono::steady_clock::time_point start_time; // 任务开始时间（用于性能统计）
};

// 线程池类：管理多线程任务执行
class ThreadPool {
private:
    std::queue<Task> task_queue;    // 任务队列（生产者-消费者模型）
    std::vector<std::thread> workers; // 工作线程数组
    std::mutex queue_mutex;         // 队列操作互斥锁
    std::condition_variable cv;     // 条件变量（用于线程唤醒）
    std::atomic<bool> running;      // 线程池运行状态
    DiskFS* disk_ptr;               // 磁盘操作实例指针
    std::mutex disk_mutex;          // 磁盘操作互斥锁（保证线程安全）
    std::atomic<size_t> active_tasks; // 活跃任务计数器

    // 工作线程执行函数
    void worker() {
        while (running) {
            Task task;
            {
                // 加锁等待任务
                std::unique_lock<std::mutex> lock(queue_mutex);
                // 等待条件：队列非空或线程池停止
                cv.wait(lock, [this] { return !task_queue.empty() || !running; });
                // 线程池停止且队列为空时退出
                if (!running && task_queue.empty()) break;
                if (task_queue.empty()) continue;

                // 取出任务并递增活跃计数器
                task = task_queue.front();
                task_queue.pop();
                active_tasks++;
            }

            // 执行任务
            task.start_time = std::chrono::steady_clock::now();
            try {
                std::lock_guard<std::mutex> disk_lock(disk_mutex); // 磁盘操作加锁
                execute_task(task); // 执行具体命令
            } catch (...) {
                task.result = "错误：任务执行异常";
            }

            // 任务完成处理
            active_tasks--;
            task.completed = true;

            // 输出结果（空结果不输出）
            if (!task.result.empty()) {
                std::cout << task.result;
            }

            // 非退出命令则打印提示符
            if (task.type != CommandType::EXIT) {
                std::cout << "> " << std::flush;
            } else {
                // 退出命令时停止线程池
                running = false;
            }
        }
    }

    // 执行具体任务（迁移自main.cpp的consumer_thread逻辑）
    void execute_task(Task& task) {
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
                break;
            }

            case CommandType::CAT: {
                if (task.args.size() < 1) {
                    task.result = "错误: 缺少文件名参数（用法：cat <文件名>）\n";
                    break;
                }
                std::string filename = task.args[0];
                int inode = disk_ptr->open_file(filename);
                if (inode == -1) {
                    task.result = "错误: 文件不存在\n";
                    break;
                }
                
                int file_size = disk_ptr->get_file_size(inode);
                if (file_size <= 0) {
                    task.result = "文件为空\n";
                    break;
                }

                char* buffer = new char[file_size + 1];
                int bytes_read = disk_ptr->read_file(inode, buffer, file_size, 0);
                if (bytes_read < 0) {
                    task.result = "错误: 读取文件失败\n";
                } else {
                    buffer[bytes_read] = '\0';
                    task.result = "文件内容:\n" + std::string(buffer) + "\n";
                }
                delete[] buffer;
                break;
            }

            case CommandType::RM: {
                if (task.args.size() < 1) {
                    task.result = "错误: 缺少文件名参数（用法：rm <文件名>）\n";
                    break;
                }
                std::string filename = task.args[0];
                bool success = disk_ptr->delete_file(filename);
                task.result = success ? "删除成功\n" : "删除失败（文件不存在或已删除）\n";
                break;
            }

            case CommandType::COPY: {
                if (task.args.size() < 2) {
                    task.result = "错误: 缺少参数（用法：copy <源文件> <目标文件>）\n";
                    break;
                }
                std::string src = task.args[0];
                std::string dest = task.args[1];

                int src_inode = disk_ptr->open_file(src);
                if (src_inode == -1) {
                    task.result = "错误: 源文件不存在\n";
                    break;
                }

                int dest_inode = disk_ptr->create_file(dest);
                if (dest_inode == -1) {
                    task.result = "错误: 目标文件创建失败（可能已存在）\n";
                    break;
                }

                int file_size = disk_ptr->get_file_size(src_inode);
                if (file_size <= 0) {
                    task.result = "源文件为空，复制完成\n";
                    break;
                }

                char* buffer = new char[file_size];
                int bytes_read = disk_ptr->read_file(src_inode, buffer, file_size, 0);
                
                if (bytes_read < 0) {
                    task.result = "错误: 读取源文件失败\n";
                    disk_ptr->delete_file(dest);
                    delete[] buffer;
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
                break;
            }

            case CommandType::WRITE: {
                if (task.args.size() < 2) {
                    task.result = "错误: 缺少参数（用法：write <文件名> <内容>，内容可加引号）\n";
                    break;
                }
                std::string filename = task.args[0];
                std::string content;

                // 合并参数为内容（支持空格）
                for (size_t i = 1; i < task.args.size(); ++i) {
                    if (i > 1) content += " ";
                    content += task.args[i];
                }

                // 去除首尾引号
                if (content.size() >= 2 && content.front() == '"' && content.back() == '"') {
                    content = content.substr(1, content.size() - 2);
                }

                int inode = disk_ptr->open_file(filename);
                if (inode == -1) {
                    inode = disk_ptr->create_file(filename);
                    if (inode == -1) {
                        task.result = "错误: 创建文件失败\n";
                        break;
                    }
                }

                int bytes_written = disk_ptr->write_file(inode, content.c_str(), content.size(), 0);
                if (bytes_written != (int)content.size()) {
                    task.result = "错误: 写入文件失败\n";
                } else {
                    task.result = "写入成功（文件大小：" + std::to_string(content.size()) + "字节）\n";
                }
                break;
            }

            case CommandType::TOUCH: {
                if (task.args.size() < 1) {
                    task.result = "错误: 缺少文件名（用法：touch/create <文件名>）\n";
                    break;
                }
                std::string filename = task.args[0];

                int inode = disk_ptr->open_file(filename);
                if (inode != -1) {
                    task.result = "文件已存在（修改时间已更新）\n";
                    break;
                }

                inode = disk_ptr->create_file(filename);
                if (inode == -1) {
                    task.result = "错误: 创建文件失败（可能文件名过长或根目录已满）\n";
                } else {
                    task.result = "空文件创建成功（inode: " + std::to_string(inode) + "）\n";
                }
                break;
            }

            case CommandType::EMPTY:
                task.result = ""; // 空输入不输出
                break;

            case CommandType::EXIT:
                task.result = "退出程序\n";
                break;

            default:
                task.result = "未知命令，支持命令：ls/cat/rm/copy/write/touch/exit \n";
        }
    }

public:
    // 构造函数：初始化线程池
    ThreadPool(DiskFS* disk, size_t thread_count = std::thread::hardware_concurrency()) 
        : running(true), disk_ptr(disk), active_tasks(0) {
        // 创建工作线程（默认使用CPU核心数）
        for (size_t i = 0; i < thread_count; ++i) {
            workers.emplace_back(&ThreadPool::worker, this);
        }
    }

    // 析构函数：停止线程池并回收资源
    ~ThreadPool() {
        running = false;
        cv.notify_all(); // 唤醒所有等待的线程
        for (auto& t : workers) {
            if (t.joinable()) t.join(); // 等待线程结束
        }
    }

    // 添加任务到队列
    void add_task(const Task& task) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(task);
        cv.notify_one(); // 唤醒一个工作线程（避免惊群效应）
    }

    // 获取当前活跃任务数
    size_t get_active_tasks() const {
        return active_tasks.load();
    }

    // 等待所有任务完成
    void wait_for_completion() {
        while (active_tasks > 0 || !task_queue.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

#endif // TASK_QUEUE_H