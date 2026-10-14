#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

using namespace std;

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this](stop_token stoken) {
                while (!stoken.stop_requested()) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(this->queueMutex);
                        this->cv.wait(lock, stoken, [this] {
                            return !this->tasks.empty();
                        });
                        if (stoken.stop_requested() && this->tasks.empty())
                            return;
                        if (this->tasks.empty())
                            continue;
                        task = move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> future<invoke_result_t<F, Args...>> {
        using RetType = invoke_result_t<F, Args...>;

        auto task = make_shared<packaged_task<RetType()>>(
            bind(forward<F>(f), forward<Args>(args)...)
        );
        
        future<RetType> res = task->get_future();
        {
            unique_lock<mutex> lock(queueMutex);
            tasks.emplace([task]() { (*task)(); });
        }
        cv.notify_one();
        return res;
    }

    ~ThreadPool() = default;

private:
    vector<jthread> workers;
    queue<function<void()>> tasks;
    mutex queueMutex;
    condition_variable_any cv;
};