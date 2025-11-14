#ifndef __ctpl_thread_pool_core_H__
#define __ctpl_thread_pool_core_H__

#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <future>
#include <mutex>
#include <queue>

// 线程池核心模块 - 负责基本的线程管理和任务执行
class ctpl_thread_pool_core {
public:
    ctpl_thread_pool_core(int nThreads = 4, int queueSize = 1000);
    ~ctpl_thread_pool_core();
    
    // 基本线程池操作
    int size() const;
    int n_idle() const;
    void resize(int nThreads);
    void clear_queue();
    void stop(bool isWait = false);
    
    // 任务提交
    template<typename F, typename... Rest>
    auto push(F &&f, Rest&&... rest) -> std::future<decltype(f(0, rest...))> {
        auto pck = std::make_shared<std::packaged_task<decltype(f(0, rest...))(int)>>(std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Rest>(rest)...));
        
        auto _f = new std::function<void(int id)>([pck](int id) {
            (*pck)(id);
        });
        
        { 
            std::unique_lock<std::mutex> lock(queue_mutex);
            q.push(_f);
        }
        
        std::unique_lock<std::mutex> lock(mutex);
        cv.notify_one();
        
        return pck->get_future();
    }
    
    template<typename F>
    auto push(F &&f) -> std::future<decltype(f(0))> {
        auto pck = std::make_shared<std::packaged_task<decltype(f(0))(int)>>(std::forward<F>(f));
        
        auto _f = new std::function<void(int id)>([pck](int id) {
            (*pck)(id);
        });
        
        { 
            std::unique_lock<std::mutex> lock(queue_mutex);
            q.push(_f);
        }
        
        std::unique_lock<std::mutex> lock(mutex);
        cv.notify_one();
        
        return pck->get_future();
    }
    
private:
    // 任务执行线程
    void worker_thread(int i);
    
    std::vector<std::unique_ptr<std::thread>> threads;
    std::vector<std::shared_ptr<std::atomic<bool>>> flags;
    std::queue<std::function<void(int id)> *> q;
    mutable std::mutex queue_mutex;
    std::atomic<bool> isDone;
    std::atomic<bool> isStop;
    std::atomic<int> nWaiting;
    
    std::mutex mutex;
    std::condition_variable cv;
};

#endif // __ctpl_thread_pool_core_H__