#include "../include/task_queue.h"

std::queue<Task> task_queue;
std::mutex queue_mutex;
std::condition_variable cv;
std::atomic<bool> running(true);
DiskFS* disk_ptr = nullptr;
std::mutex disk_mutex;

void add_task(const Task& task) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    task_queue.push(task);
    cv.notify_one();
}

bool get_task(Task& task) {
    std::unique_lock<std::mutex> lock(queue_mutex);
    cv.wait(lock, []{ return !task_queue.empty() || !running; });

    if (!running && task_queue.empty()) {
        return false;
    }

    if (task_queue.empty()) {
        return false;
    }

    task = task_queue.front();
    task_queue.pop();
    return true;
}