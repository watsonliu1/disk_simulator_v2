// 包含必要的头文件：标准库+文件系统接口+多线程库
#include "include/disk_fs.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>       // 多线程支持
#include <mutex>        // 互斥锁
#include <condition_variable>  // 条件变量
#include <queue>        // 任务队列
#include <sstream>      // 字符串解析
#include <atomic>       // 原子变量（线程安全的状态标记）

// 命令类型枚举：定义支持的所有命令，便于后续switch-case处理
enum class CommandType 
{
    LS,         // 列出文件
    CAT,        // 查看文件内容
    RM,         // 删除文件
    COPY,       // 复制文件
    EXIT,       // 退出程序
    UNKNOWN     // 未知命令
};

// 任务结构体：封装一个命令的所有信息，作为生产者和消费者之间的数据载体
struct Task 
{
    CommandType type;               // 命令类型（如LS、COPY）
    std::vector<std::string> args;  // 命令参数（如COPY的源文件和目标文件）
    std::string result;             // 命令执行结果（用于反馈给用户）
    bool completed;                 // 任务是否完成（标记执行状态）
};

// 全局任务队列及同步机制（生产者和消费者共享）
std::queue<Task> task_queue;               // 存储待执行的任务
std::mutex queue_mutex;                    // 保护任务队列的互斥锁（避免并发读写冲突）
std::condition_variable cv;                // 条件变量（用于线程间通知：有任务时唤醒消费者）
std::atomic<bool> running(true);           // 原子变量：控制线程运行状态（线程安全的开关）
DiskFS* disk_ptr = nullptr;                // 全局磁盘指针（指向文件系统实例，供消费者调用）
std::mutex disk_mutex;                     // 磁盘操作互斥锁（保护DiskFS的所有方法，避免多线程冲突）

/**
 * 消费者线程处理函数：每个消费者线程循环从任务队列取任务并执行
 * 核心逻辑：等待任务→取出任务→执行任务→反馈结果→循环
 */
void consumer_thread()
{
    // 线程循环运行，直到running被设为false且队列空
    while (running)
    {
        // 1. 加锁并等待任务（若队列为空则阻塞）
        std::unique_lock<std::mutex> lock(queue_mutex);  // unique_lock支持手动解锁，比lock_guard灵活
        // 条件变量等待：当队列空且running为true时阻塞，被唤醒后检查条件
        cv.wait(lock, []{ return !task_queue.empty() || !running; });
        
        // 2. 检查是否需要退出（running为false且队列空）
        if (!running && task_queue.empty()) {
            break;  // 退出循环，线程结束
        }
        // 若队列仍为空（可能被虚假唤醒），继续等待
        if (task_queue.empty()) {
            continue;
        }

        // 3. 取出任务（解锁前完成，减少锁持有时间）
        Task task = task_queue.front();  // 取队首任务
        task_queue.pop();                // 移除队首任务
        lock.unlock();                   // 立即解锁队列，让生产者可以继续放任务

        // 4. 执行任务（调用DiskFS接口，加锁保护磁盘操作）
        try {
            // 对磁盘操作加锁，确保同一时间只有一个线程操作文件系统
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            
            // 根据命令类型执行不同逻辑
            switch (task.type) {
                case CommandType::LS: {
                    // 调用list_files()获取文件列表
                    std::vector<DirEntry> entries = disk_ptr->list_files();
                    std::stringstream ss;  // 用stringstream拼接结果
                    ss << "文件列表:\n";
                    for (const auto& entry : entries) {
                        // 只显示有效且非根目录的文件
                        if (entry.valid && entry.inode_num != 0) {
                            ss << "  " << entry.name << " (inode: " << entry.inode_num << ")\n";
                        }
                    }
                    task.result = ss.str();  // 保存执行结果
                    task.completed = true;
                    break;
                }

                case CommandType::CAT: {
                    // 校验参数：cat需要1个文件名参数
                    if (task.args.size() < 1) {
                        task.result = "错误: 缺少文件名参数（用法：cat <文件名>）\n";
                        task.completed = true;
                        break;
                    }
                    std::string filename = task.args[0];
                    // 打开文件获取inode
                    int inode = disk_ptr->open_file(filename);
                    if (inode == -1) {
                        task.result = "错误: 文件不存在\n";
                        task.completed = true;
                        break;
                    }
                    
                    // 获取文件大小（避免分配过多内存）
                    int file_size = disk_ptr->get_file_size(inode);
                    if (file_size <= 0) {
                        task.result = "文件为空\n";
                        task.completed = true;
                        break;
                    }

                    // 读取文件内容
                    char* buffer = new char[file_size + 1];  // +1用于存放字符串结束符
                    int bytes_read = disk_ptr->read_file(inode, buffer, file_size, 0);
                    if (bytes_read < 0) {
                        task.result = "错误: 读取文件失败\n";
                    } else {
                        buffer[bytes_read] = '\0';  // 手动添加字符串结束符
                        task.result = "文件内容:\n" + std::string(buffer);
                    }
                    delete[] buffer;  // 释放内存，避免泄漏
                    task.completed = true;
                    break;
                }

                case CommandType::RM: {
                    // 校验参数：rm需要1个文件名参数
                    if (task.args.size() < 1) {
                        task.result = "错误: 缺少文件名参数（用法：rm <文件名>）\n";
                        task.completed = true;
                        break;
                    }
                    std::string filename = task.args[0];
                    // 调用delete_file删除文件
                    bool success = disk_ptr->delete_file(filename);
                    task.result = success ? "删除成功\n" : "删除失败（文件不存在或已删除）\n";
                    task.completed = true;
                    break;
                }

                case CommandType::COPY: {
                    // 校验参数：copy需要2个参数（源文件和目标文件）
                    if (task.args.size() < 2) {
                        task.result = "错误: 缺少参数（用法：copy <源文件> <目标文件>）\n";
                        task.completed = true;
                        break;
                    }
                    std::string src = task.args[0];   // 源文件
                    std::string dest = task.args[1];  // 目标文件

                    // 1. 打开源文件
                    int src_inode = disk_ptr->open_file(src);
                    if (src_inode == -1) {
                        task.result = "错误: 源文件不存在\n";
                        task.completed = true;
                        break;
                    }

                    // 2. 创建目标文件
                    int dest_inode = disk_ptr->create_file(dest);
                    if (dest_inode == -1) {
                        task.result = "错误: 目标文件创建失败（可能已存在）\n";
                        task.completed = true;
                        break;
                    }

                    // 3. 读取源文件内容
                    int file_size = disk_ptr->get_file_size(src_inode);
                    if (file_size <= 0) {
                        task.result = "源文件为空，复制完成\n";
                        task.completed = true;
                        break;
                    }

                    // 分配缓冲区存储源文件内容
                    char* buffer = new char[file_size];
                    int bytes_read = disk_ptr->read_file(src_inode, buffer, file_size, 0);
                    
                    if (bytes_read < 0) {
                        task.result = "错误: 读取源文件失败\n";
                        disk_ptr->delete_file(dest);  // 清理：删除已创建的目标文件
                        delete[] buffer;
                        task.completed = true;
                        break;
                    }

                    // 4. 写入目标文件
                    int bytes_written = disk_ptr->write_file(dest_inode, buffer, bytes_read, 0);
                    delete[] buffer;  // 及时释放内存

                    if (bytes_written != bytes_read) {
                        task.result = "错误: 写入目标文件失败\n";
                        disk_ptr->delete_file(dest);  // 清理：删除不完整的目标文件
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
                    task.result = "未知命令，请输入help查看帮助\n";
                    task.completed = true;
            }
        } catch (...) {
            // 捕获所有异常，避免线程崩溃
            task.result = "命令执行出错（发生未知异常）\n";
            task.completed = true;
        }

        // 5. 打印执行结果，提示用户输入下一个命令
        std::cout << "\n" << task.result << "> ";
        std::cout.flush();  // 强制刷新输出缓冲区，确保结果及时显示
    }
}

/**
 * 打印帮助信息：展示支持的所有命令及用法
 */
void print_help() {
    std::cout << "多线程磁盘模拟文件系统命令:\n";
    std::cout << "  ls                  - 列出当前目录所有文件\n";
    std::cout << "  cat <文件名>        - 查看指定文件的内容\n";
    std::cout << "  rm <文件名>         - 删除指定文件\n";
    std::cout << "  copy <源文件> <目标文件> - 复制源文件内容到目标文件\n";
    std::cout << "  format              - 格式化磁盘（会清除所有数据）\n";
    std::cout << "  mount               - 挂载磁盘\n";
    std::cout << "  umount              - 卸载磁盘\n";
    std::cout << "  info                - 显示磁盘信息（总块数、空闲块数等）\n";
    std::cout << "  create <文件名>     - 创建新文件\n";
    std::cout << "  help                - 显示本帮助信息\n";
    std::cout << "  exit                - 退出程序\n";
}

/**
 * 自动测试函数：验证文件系统基础功能（单线程，确保核心逻辑正确）
 * @param disk 磁盘文件系统实例
 * @return 所有测试通过返回true，否则返回false
 */
bool run_tests(DiskFS& disk)
{
    int test_count = 0;
    int pass_count = 0;
    std::cout << "\n===== 开始自动测试 =====" << std::endl;

    // 测试1: 格式化磁盘（验证超级块和位图初始化）
    test_count++;
    bool format_ok = disk.format();
    
    // 额外验证：格式化后超级块应处于初始状态（如总块数正确，空闲块数=数据块数-1）
    bool super_block_valid = false;
    if (format_ok)
    {
        SuperBlock sb = disk.get_super_block();
        super_block_valid = (sb.total_blocks > 0 && sb.free_blocks == sb.data_blocks - 1); // 根目录占一个块
    }
    std::cout << "测试" << test_count << "(格式化): " << ((format_ok && super_block_valid) ? "通过" : "失败") << std::endl;
    if (format_ok && super_block_valid) pass_count++;


    // 测试2: 挂载磁盘（验证挂载状态和元数据加载）
    test_count++;
    bool mount_ok = disk.mount();
    bool mount_state_valid = disk.isMounted();  // 验证挂载状态标记
    std::cout << "测试" << test_count << "(挂载): " << ((mount_ok && mount_state_valid) ? "通过" : "失败") << std::endl;
    if (mount_ok && mount_state_valid) pass_count++;
    

    // 测试3: 创建文件（验证inode分配和目录项添加）
    test_count++;
    int inode1 = disk.create_file("test1.txt");
    bool create_ok = (inode1 != -1);

    // 额外验证：目录项中应包含新文件，且inode状态为"已使用"
    bool dir_entry_valid = false;
    bool inode_used_valid = false;
    if (create_ok) 
    {
        std::vector<DirEntry> entries = disk.list_files();
        for (const auto& e : entries) 
        {
            if (std::string(e.name) == "test1.txt" && e.inode_num == (uint32_t)inode1 && e.valid) {
                dir_entry_valid = true;
                break;
            }
        }
        inode_used_valid = disk.is_inode_used(inode1);
    }
    
    std::cout << "测试" << test_count << "(创建文件): " << ((create_ok && dir_entry_valid && inode_used_valid) ? "通过" : "失败") << std::endl;
    if (create_ok && dir_entry_valid && inode_used_valid) pass_count++;


    // 测试4: 禁止创建同名文件（验证目录项唯一性）
    test_count++;
    int inode_dup = disk.create_file("test1.txt");
    bool no_dup_ok = (inode_dup == -1);
    // 额外验证：原文件inode和目录项未被修改
    bool original_file_valid = false;
    if (no_dup_ok)
    {
        std::vector<DirEntry> entries = disk.list_files();
        for (const auto& e : entries) {
            if (std::string(e.name) == "test1.txt" && e.inode_num == (uint32_t)inode1 && e.valid) {
                original_file_valid = true;
                break;
            }
        }
    }
    std::cout << "测试" << test_count << "(禁止同名文件): " << ((no_dup_ok && original_file_valid) ? "通过" : "失败") << std::endl;
    if (no_dup_ok && original_file_valid) pass_count++;

    // 测试5: 写入文件（验证数据块分配和内容正确性）
    test_count++;
    std::string content = "hello, disk fs!";
    int write_size = disk.write_file(inode1, content.c_str(), content.size(), 0);
    bool write_ok = (write_size == (int)content.size());

    std::cout << "测试" << test_count << "(写入文件): " << (write_ok ? "通过" : "失败") << std::endl;
    if (write_ok) pass_count++;


    // 测试6: 读取文件（验证内容和偏移量读取）
    test_count++;
    // 6.1 完整读取
    char* read_buf = new char[content.size() + 1];
    int read_size = disk.read_file(inode1, read_buf, content.size(), 0);
    read_buf[read_size] = '\0';
    
    bool full_read_ok = (read_size == (int)content.size() && std::string(read_buf) == content);
    
   
    // 6.2 偏移量读取（从第6个字符开始读，验证部分读取）
    char* partial_buf = new char[10];
    int partial_read = disk.read_file(inode1, partial_buf, 10, 6);  // 偏移6：" disk fs!"
    partial_buf[partial_read] = '\0';
    bool partial_read_ok = (partial_read == 9 && std::string(partial_buf) == " disk fs!");
    
    delete[] read_buf;
    delete[] partial_buf;
    std::cout << "测试" << test_count << "(读取文件): " << ((full_read_ok && partial_read_ok) ? "通过" : "失败") << std::endl;
    if (full_read_ok && partial_read_ok) pass_count++;

    // 测试7: 列出文件（验证ls命令依赖的完整目录项）
    test_count++;
    // 7.1 先创建第二个文件，确保ls能列出多个文件
    disk.create_file("test2.txt");
    std::vector<DirEntry> entries = disk.list_files();
    // 验证：包含test1.txt（有效）、test2.txt（有效），无无效条目
    int valid_count = 0;
    bool has_invalid = false;
    for (const auto& e : entries) 
    {
        if (e.valid) 
        {
            if (std::string(e.name) == "test1.txt" || std::string(e.name) == "test2.txt") 
            {
                valid_count++;
            }
        } 
        else 
        {
            has_invalid = true;  // 不允许存在有效标记为false但仍在列表中的条目
        }
    }
    bool ls_ok = (valid_count == 2 && !has_invalid);
    std::cout << "测试" << test_count << "(列出文件): " << (ls_ok ? "通过" : "失败") << std::endl;
    if (ls_ok) pass_count++;

    // 测试8: 复制文件（验证copy命令核心流程）
    test_count++;
    // 8.1 复制test1.txt到test3.txt
    bool copy_success = false;
    // 模拟copy底层逻辑：读源文件→创建目标→写目标
    int src_inode = disk.open_file("test1.txt");
    if (src_inode != -1) 
    {
        int dest_inode = disk.create_file("test3.txt");
        if (dest_inode != -1)
        {
            char* copy_buf = new char[content.size() + 1];
            int read = disk.read_file(src_inode, copy_buf, content.size(), 0);
            int write = disk.write_file(dest_inode, copy_buf, read, 0);
            delete[] copy_buf;
            // 验证目标文件内容与源文件一致
            char* dest_buf = new char[content.size() + 1];
            disk.read_file(dest_inode, dest_buf, content.size(), 0);
            dest_buf[content.size()] = '\0';
            copy_success = (read == write && std::string(dest_buf) == content);
            delete[] dest_buf;
        }
    }
    std::cout << "测试" << test_count << "(复制文件): " << (copy_success ? "通过" : "失败") << std::endl;
    if (copy_success) pass_count++;


    // 测试9: 删除文件（验证rm命令及资源释放）
    test_count++;
    bool delete_ok = disk.delete_file("test1.txt");
    // 额外验证：inode标记为未使用，数据块释放（空闲块数增加）
    bool inode_freed = false;
    if (delete_ok) {
        inode_freed = !disk.is_inode_used(inode1);  // inode应标记为未使用
        std::cout << inode_freed << std::endl;
    }
    std::cout << "测试" << test_count << "(删除文件): " << ((delete_ok && inode_freed) ? "通过" : "失败") << std::endl;
    if (delete_ok && inode_freed) pass_count++;

    // 测试10: 验证删除效果+卸载（关联rm后ls和unmount）
    test_count++;
    // 10.1 验证删除后文件不再被ls列出
    std::vector<DirEntry> post_delete_entries = disk.list_files();
    bool deleted_in_ls = false;
    for (const auto& e : post_delete_entries) {
        if (std::string(e.name) == "test1.txt" && e.valid) 
        {
            deleted_in_ls = true;  // 如果仍存在则失败
            break;
        }
    }
    // 10.2 验证卸载成功且数据同步
    bool unmount_ok = disk.unmount();
    bool unmount_state_valid = !disk.isMounted();  // 卸载后状态应为未挂载
    
    // 10.3 重新挂载验证数据持久化（test3.txt应存在）
    bool remount_ok = disk.mount();
    bool test3_exists = (disk.open_file("test3.txt") != -1);  // copy的文件应持久化
    disk.unmount();  // 清理测试环境

    bool final_ok = (!deleted_in_ls && unmount_ok && unmount_state_valid && remount_ok && test3_exists);
    std::cout << "测试" << test_count << "(验证删除+卸载): " << (final_ok ? "通过" : "失败") << std::endl;
    if (final_ok) pass_count++;

    // 输出测试总结
    std::cout << "\n===== 测试总结 =====" << std::endl;
    std::cout << "总测试数: " << test_count << std::endl;
    std::cout << "通过数: " << pass_count << std::endl;
    std::cout << "失败数: " << (test_count - pass_count) << std::endl;

    return (test_count == pass_count);
}

/**
 * 主函数：程序入口，实现生产者逻辑（接收命令、创建任务）和线程管理
 */
int main(int argc, char* argv[]) 
{
    // 支持测试模式：./sim_disk <磁盘文件> --test
    if (argc == 3 && std::string(argv[2]) == "--test") 
    {
        DiskFS disk(argv[1]);
        bool all_passed = run_tests(disk);
        return all_passed ? 0 : 1;  // 测试通过返回0，否则返回1
    }

    // 检查交互模式参数（必须提供磁盘文件名）
    if (argc != 2) 
    {
        std::cerr << "用法: " << argv[0] << " <磁盘文件>\n";
        std::cerr << "测试模式: " << argv[0] << " <磁盘文件> --test\n";
        return 1;  // 参数错误，退出程序
    }

    // 初始化磁盘文件系统
    DiskFS disk(argv[1]);
    disk_ptr = &disk;  // 赋值给全局指针，供消费者线程使用

    // 尝试挂载磁盘，若失败则尝试格式化后再挂载
    if (!disk.mount()) 
    {
        std::cerr << "挂载磁盘失败，尝试格式化...\n";
        if (!disk.format() || !disk.mount()) 
        {
            std::cerr << "格式化并挂载磁盘失败，退出\n";
            return 1;  // 初始化失败，退出程序
        }
    }

    // 启动消费者线程池（默认3个线程，可根据需求调整）
    const int num_consumers = 3;
    std::vector<std::thread> consumers;  // 存储消费者线程的容器
    for (int i = 0; i < num_consumers; ++i) 
    {
        // 用emplace_back直接构造线程，传入消费者函数
        consumers.emplace_back(consumer_thread);
    }

    // 打印帮助信息，提示用户输入命令
    print_help();
    std::cout << "> ";
    std::cout.flush();  // 刷新输出，确保提示符及时显示

    // 生产者逻辑：读取用户输入的命令，解析为任务并放入队列
    std::string line;  // 存储用户输入的一行命令
    while (std::getline(std::cin, line))
    {   // 循环读取一行输入
        // 解析命令：将输入字符串按空格分割为tokens（如"copy a.txt b.txt"→["copy", "a.txt", "b.txt"]）
        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }

        // 空输入（只按了回车），直接提示继续输入
        if (tokens.empty()) {
            std::cout << "> ";
            std::cout.flush();
            continue;
        }

        // 构造任务
        Task task;
        task.completed = false;  // 初始化为未完成

        // 根据命令首单词解析命令类型
        if (tokens[0] == "ls")
        {
            task.type = CommandType::LS;  // 无参数命令
        } 
        else if (tokens[0] == "cat" && tokens.size() >= 2)
        {
            task.type = CommandType::CAT;
            task.args.push_back(tokens[1]);  // 传入文件名参数
        } 
        else if (tokens[0] == "rm" && tokens.size() >= 2)
        {
            task.type = CommandType::RM;
            task.args.push_back(tokens[1]);  // 传入文件名参数
        }
        else if (tokens[0] == "copy" && tokens.size() >= 3)
        {
            task.type = CommandType::COPY;
            task.args.push_back(tokens[1]);  // 源文件
            task.args.push_back(tokens[2]);  // 目标文件
        }
        else if (tokens[0] == "exit")
        {
            task.type = CommandType::EXIT;
            // 放入退出任务后，跳出循环，准备结束程序
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                task_queue.push(task);
            }
            cv.notify_one();  // 唤醒一个消费者执行exit任务
            break;
        }
        else if (tokens[0] == "format")
        {
            // 格式化命令：直接在主线程执行（不需要放入队列，避免多线程冲突）
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            bool success = disk.format();
            std::cout << (success ? "格式化成功\n" : "格式化失败\n") << "> ";
            continue;
        }
        else if (tokens[0] == "mount")
        {
            // 挂载命令：直接在主线程执行
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            bool success = disk.mount();
            std::cout << (success ? "挂载成功\n" : "挂载失败\n") << "> ";
            continue;
        }
        else if (tokens[0] == "umount")
        {
            // 卸载命令：直接在主线程执行
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            bool success = disk.unmount();
            std::cout << (success ? "卸载成功\n" : "卸载失败\n") << "> ";
            continue;
        }
        else if (tokens[0] == "info")
        {
            // 磁盘信息命令：直接在主线程执行
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            disk.print_info();
            std::cout << "> ";
            continue;
        }
        else if (tokens[0] == "create" && tokens.size() >= 2)
        {
            // 创建文件命令：直接在主线程执行
            std::lock_guard<std::mutex> disk_lock(disk_mutex);
            int inode = disk.create_file(tokens[1]);
            std::cout << (inode != -1 ? "创建成功，inode: " + std::to_string(inode) + "\n" : "创建失败\n") << "> ";
            continue;
        }
        else if (tokens[0] == "help")
        {
            // 帮助命令：直接打印帮助信息
            print_help();
            std::cout << "> ";
            continue;
        } 
        else 
        {
            // 未知命令，提示用户
            std::cout << "未知命令，请输入help查看帮助\n> ";
            std::cout.flush();
            continue;
        }

        // 将任务加入队列（加锁保护）
        {
            std::lock_guard<std::mutex> lock(queue_mutex);  // 自动加锁/解锁
            task_queue.push(task);
        }
        cv.notify_one();  // 唤醒一个等待的消费者线程

        // 提示用户输入下一个命令
        std::cout << "> ";
        std::cout.flush();
    }

    // 程序退出阶段：等待所有任务完成，回收线程资源
    running = false;  // 通知消费者线程准备退出
    cv.notify_all();  // 唤醒所有阻塞的消费者线程
    // 等待所有消费者线程结束
    for (auto& t : consumers) {
        if (t.joinable()) {  // 检查线程是否可join（避免重复join）
            t.join();  // 等待线程执行完毕
        }
    }

    // 退出前卸载磁盘（确保数据写入）
    if (disk.isMounted()) {
        disk.unmount();
    }

    return 0;
}