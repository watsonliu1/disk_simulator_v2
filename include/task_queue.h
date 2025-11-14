#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <queue>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "disk_fs.h"
#include "command_parser.h"

// 任务结构体：封装一个命令的所有信息，作为生产者和消费者之间的数据载体
struct Task 
{
    CommandType type;               // 命令类型（如LS、COPY）
    std::vector<std::string> args;  // 命令参数（如COPY的源文件和目标文件）
    std::string result;             // 命令执行结果（用于反馈给用户）
    bool completed;                 // 任务是否完成（标记执行状态）
};

// 全局任务队列及同步机制（生产者和消费者共享）
extern std::queue<Task> task_queue;               // 存储待执行的任务
extern std::mutex queue_mutex;                    // 保护任务队列的互斥锁（避免并发读写冲突）
extern std::condition_variable cv;                // 条件变量（用于线程间通知：有任务时唤醒消费者）
extern std::atomic<bool> running;           // 原子变量：控制线程运行状态（线程安全的开关）
extern DiskFS* disk_ptr;                // 全局磁盘指针（指向文件系统实例，供消费者调用）
extern std::mutex disk_mutex;                     // 磁盘操作互斥锁（保护DiskFS的所有方法，避免多线程冲突）


/**
 * @brief 向任务队列添加任务
 */
void add_task(const Task& task);

/**
 * @brief 从任务队列获取任务
 */
bool get_task(Task& task);

#endif // TASK_QUEUE_H