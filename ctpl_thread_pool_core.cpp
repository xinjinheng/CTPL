#include "ctpl_thread_pool_core.h"

ctpl_thread_pool_core::ctpl_thread_pool_core(int nThreads, int queueSize) 
    : isDone(false), isStop(false), nWaiting(0) {
    resize(nThreads);
}

ctpl_thread_pool_core::~ctpl_thread_pool_core() {
    stop(true);
}

int ctpl_thread_pool_core::size() const {
    return static_cast<int>(threads.size());
}

int ctpl_thread_pool_core::n_idle() const {
    return nWaiting;
}

void ctpl_thread_pool_core::resize(int nThreads) {
    if (!isStop && !isDone) {
        int oldNThreads = static_cast<int>(threads.size());
        if (oldNThreads <= nThreads) {  // if the number of threads is increased
            threads.resize(nThreads);
            flags.resize(nThreads);
            
            for (int i = oldNThreads; i < nThreads; ++i) {
                flags[i] = std::make_shared<std::atomic<bool>>(false);
                threads[i] = std::make_unique<std::thread>(&ctpl_thread_pool_core::worker_thread, this, i);
            }
        } else {  // the number of threads is decreased
            for (int i = oldNThreads - 1; i >= nThreads; --i) {
                *flags[i] = true;  // this thread will finish
                if (threads[i]->joinable()) {
                    threads[i]->join();
                }
            }
            
            { 
                std::unique_lock<std::mutex> lock(mutex);
                cv.notify_all();
            }
            
            threads.resize(nThreads);  // safe to delete because the threads are joined
            flags.resize(nThreads);
        }
    }
}

void ctpl_thread_pool_core::clear_queue() {
    std::function<void(int id)> * _f;
    
    { 
        std::unique_lock<std::mutex> lock(queue_mutex);
        while (!q.empty()) {
            _f = q.front();
            q.pop();
            delete _f;
        }
    }
}

void ctpl_thread_pool_core::stop(bool isWait) {
    if (!isWait) {
        if (isStop) return;
        isStop = true;
        
        for (int i = 0, n = size(); i < n; ++i) {
            *flags[i] = true;
        }
        
        clear_queue();
    } else {
        if (isDone || isStop) return;
        isDone = true;
    }
    
    { 
        std::unique_lock<std::mutex> lock(mutex);
        cv.notify_all();
    }
    
    for (int i = 0; i < static_cast<int>(threads.size()); ++i) {
        if (threads[i]->joinable()) {
            threads[i]->join();
        }
    }
    
    clear_queue();
    threads.clear();
    flags.clear();
}

void ctpl_thread_pool_core::worker_thread(int i) {
    std::shared_ptr<std::atomic<bool>> flag(flags[i]);
    std::atomic<bool> &_flag = *flag;
    std::function<void(int id)> * _f = nullptr;
    bool isPop = false;
    
    { 
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (!q.empty()) {
            _f = q.front();
            q.pop();
            isPop = true;
        }
    }
    
    while (true) {
        while (isPop && _f) {
            std::unique_ptr<std::function<void(int id)>> func(_f);
            (*_f)(i);
            
            _f = nullptr;
            isPop = false;
            
            if (_flag) return;
            
            { 
                std::unique_lock<std::mutex> lock(queue_mutex);
                if (!q.empty()) {
                    _f = q.front();
                    q.pop();
                    isPop = true;
                }
            }
        }
        
        std::unique_lock<std::mutex> lock(mutex);
        ++nWaiting;
        cv.wait(lock, [this, &_f, &isPop, &_flag]() {
            { 
                std::unique_lock<std::mutex> lock(queue_mutex);
                if (!q.empty()) {
                    _f = q.front();
                    q.pop();
                    isPop = true;
                }
            }
            return isPop || isDone || _flag;
        });
        --nWaiting;
        
        if (!isPop) return;
    }
}
