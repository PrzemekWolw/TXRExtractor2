#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

void setRedrawCallback(std::function<void()> callback);
void requestRedraw();

struct TaskState {
    std::mutex mu;
    std::string status;
    std::vector<std::string> logLines;
    bool running = false;
    std::atomic<bool> stop{false};

    void log(const std::string& msg);
    void setStatus(const std::string& status);
    void begin(const std::string& status);
    void finish(const std::string& status);
};

void startTask(TaskState& task, const std::string& status, std::function<void()> work);
void runPool(size_t count, unsigned workers, TaskState& task, const std::function<std::string(size_t)>& work,
             const std::function<void(size_t, const std::string&)>& record);
