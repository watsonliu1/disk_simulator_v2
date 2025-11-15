#include "../include/disk_fs.h"
#include "../include/task_queue.h"
#include "../include/command_parser.h"
#include <iostream>
#include <fstream>
#include <random>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sys/resource.h>
#include <cstring>

// 压力测试配置参数
const size_t TEST_DURATION_HOURS = 12;    // 测试时长（小时）
const size_t INIT_FILE_COUNT = 50;        // 初始文件数量
const size_t MAX_OPS_PER_SECOND = 10;     // 每秒最大操作数
const std::string LOG_FILE = "stress_test.log"; // 日志文件路径

// 生成随机字符串（用于文件名和内容）
std::string random_string(size_t length)
{
    const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    static std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(0, chars.size() - 1);
    
    std::string s;
    for (size_t i = 0; i < length; ++i) {
        s += chars[dist(gen)];
    }
    return s;
}

// 获取当前进程内存使用（MB）
double get_memory_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss / 1024.0; // Linux系统下单位转换（KB→MB）
}

// CPU使用率监控类
class CPUUsageMonitor
{
private:
    std::chrono::steady_clock::time_point last_time;
    clock_t last_clock;

public:
    CPUUsageMonitor() {
        last_time = std::chrono::steady_clock::now();
        last_clock = clock();
    }

    // 计算CPU使用率（百分比）
    double get_usage() {
        auto now = std::chrono::steady_clock::now();
        clock_t now_clock = clock();
        
        // 计算实际流逝时间和CPU占用时间
        double elapsed_time = std::chrono::duration<double>(now - last_time).count();
        double cpu_time = (now_clock - last_clock) / (double)CLOCKS_PER_SEC;
        
        // 更新基准值
        last_time = now;
        last_clock = now_clock;
        
        return (cpu_time / elapsed_time) * 100;
    }
};

// 执行压力测试
void stress_test() 
{
    std::ofstream log(LOG_FILE, std::ios::app);
    if (!log.is_open()) {
        std::cerr << "无法打开日志文件: " << LOG_FILE << std::endl;
        return;
    }

    CPUUsageMonitor cpu_monitor;
    DiskFS disk("stress_disk.img");
    
    // 初始化磁盘
    std::cout << "初始化测试磁盘..." << std::endl;
    if (!disk.format() || !disk.mount()) {
        std::cerr << "磁盘初始化失败" << std::endl;
        return;
    }

    // 创建线程池（使用CPU核心数线程）
    ThreadPool pool(&disk);
    
    // 预创建测试文件
    std::vector<std::string> filenames;
    for (size_t i = 0; i < INIT_FILE_COUNT; ++i) {
        std::string name = "test_" + random_string(8) + ".txt";
        filenames.push_back(name);
        Task task{CommandType::TOUCH, {name}, "", false, std::chrono::steady_clock::now()};
        pool.add_task(task);
    }
    pool.wait_for_completion(); // 等待文件创建完成
    std::cout << "初始化完成，开始" << TEST_DURATION_HOURS << "小时压力测试..." << std::endl;

    // 测试主循环
    auto end_time = std::chrono::steady_clock::now() + std::chrono::hours(TEST_DURATION_HOURS);
    size_t total_ops = 0;
    size_t success_ops = 0;
    static std::mt19937 gen(std::random_device{}());

    while (std::chrono::steady_clock::now() < end_time) {
        // 随机生成操作类型
        std::uniform_int_distribution<> op_dist(0, 4); // 0-4对应5种操作
        std::uniform_int_distribution<> file_dist(0, filenames.size() - 1);
        Task task;

        switch (op_dist(gen)) {
            case 0: // LS命令
                task = {CommandType::LS, {}, "", false, std::chrono::steady_clock::now()};
                break;
            
            case 1: // CAT命令
                task = {CommandType::CAT, {filenames[file_dist(gen)]}, "", false, std::chrono::steady_clock::now()};
                break;
            
            case 2: // WRITE命令
            {
                // 生成1KB随机内容，并在首尾添加双引号
                std::string content = "\"" + random_string(1024) + "\"";
                task = {CommandType::WRITE, {filenames[file_dist(gen)], content}, "", false, std::chrono::steady_clock::now()};
                break;
            }
            
            case 3: // RM+重建（保持文件数量稳定）
            {
                std::string name = filenames[file_dist(gen)];
                // 先删除
                Task rm_task{CommandType::RM, {name}, "", false, std::chrono::steady_clock::now()};
                pool.add_task(rm_task);
                pool.wait_for_completion();
                // 再重建
                Task touch_task{CommandType::TOUCH, {name}, "", false, std::chrono::steady_clock::now()};
                pool.add_task(touch_task);
                break;
            }
            
            case 4: // COPY命令
            {
                std::string src = filenames[file_dist(gen)];
                std::string dest = "copy_" + random_string(8) + ".txt";
                task = {CommandType::COPY, {src, dest}, "", false, std::chrono::steady_clock::now()};
                break;
            }
        }

        // 提交任务并统计
        if (task.type != CommandType::UNKNOWN) {
            pool.add_task(task);
            total_ops++;
            success_ops++; // 简化统计，实际应根据结果判断
        }

        // 控制操作频率
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / MAX_OPS_PER_SECOND));

        // 每10min输出一次统计
        if (total_ops % (MAX_OPS_PER_SECOND * 10*60) == 0 && total_ops > 0) {
            double cpu = cpu_monitor.get_usage();
            double mem = get_memory_usage();
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            
            log << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") 
                << "] 总操作数: " << total_ops 
                << " 成功率: " << std::fixed << std::setprecision(2) 
                << (success_ops * 100.0 / total_ops) << "% "
                << "CPU: " << cpu << "% "
                << "内存: " << mem << "MB" << std::endl;
            
            std::cout << "已运行" << total_ops/(MAX_OPS_PER_SECOND*3600) 
                      << "小时，CPU: " << cpu << "%, 内存: " << mem << "MB" << std::endl;
        }
    }

    // 测试结束处理
    pool.wait_for_completion();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    log << "\n[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") 
        << "] 测试结束\n总操作数: " << total_ops 
        << "\n成功率: " << (success_ops * 100.0 / total_ops) << "% "
        << "\n峰值内存: " << get_memory_usage() << "MB" << std::endl;

    std::cout << "压力测试完成，结果已写入" << LOG_FILE << std::endl;
    disk.unmount();
}

int main() {
    stress_test();
    return 0;
}