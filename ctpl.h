
/*********************************************************
 *
 *  Copyright (C) 2014 by Vitaliy Vitsentiy
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *********************************************************/


#ifndef __ctpl_thread_pool_H__
#define __ctpl_thread_pool_H__

#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <exception>
#include <future>
#include <mutex>
#include <boost/lockfree/queue.hpp>
#include <chrono>
#include <unordered_map>
#include <set>
#include <queue>
#include <sstream>
#include <stdexcept>


#ifndef _ctplThreadPoolLength_
#define _ctplThreadPoolLength_  100
#endif


// thread pool to run user's functors with signature
//      ret func(int id, other_params)
// where id is the index of the thread that runs the functor
// ret is some return type


namespace ctpl {

    class thread_pool {
    public:
        // 配置结构体
        struct config {
            // 空指针异常处理配置
            bool enable_null_ptr_protection = true;
            int max_retry_count = 3;
            std::chrono::milliseconds retry_interval = std::chrono::milliseconds(100);
            
            // 网络超时管控配置
            bool enable_network_timeout = false;
            std::chrono::milliseconds default_network_timeout = std::chrono::milliseconds(5000);
            
            // 并发资源冲突处理配置
            bool enable_resource_conflict_handling = false;
            std::chrono::milliseconds deadlock_detection_period = std::chrono::milliseconds(1000);
            
            // 异常自愈与监控配置
            bool enable_health_monitoring = true;
            double max_exception_rate = 0.1; // 10%
            int min_threads = 1;
            int max_threads = 100;
        };

        thread_pool() : q(_ctplThreadPoolLength_), cfg() { this->init(); }
        thread_pool(int nThreads, int queueSize = _ctplThreadPoolLength_) : q(queueSize), cfg() { this->init(); this->resize(nThreads); }
        thread_pool(const config & config) : q(_ctplThreadPoolLength_), cfg(config) { this->init(); }
        thread_pool(int nThreads, const config & config, int queueSize = _ctplThreadPoolLength_) : q(queueSize), cfg(config) { this->init(); this->resize(nThreads); }

        // 获取和设置配置
        config & get_config() { return cfg; }
        const config & get_config() const { return cfg; }
        void set_config(const config & config) { cfg = config; }

        // the destructor waits for all the functions in the queue to be finished
        ~thread_pool() {
            this->stop(true);
        }

        // get the number of running threads in the pool
        int size() { return static_cast<int>(this->threads.size()); }

        // number of idle threads
        int n_idle() { return this->nWaiting; }
        std::thread & get_thread(int i) { return *this->threads[i]; }

        // change the number of threads in the pool
        // should be called from one thread, otherwise be careful to not interleave, also with this->stop()
        // nThreads must be >= 0
        void resize(int nThreads) {
            if (!this->isStop && !this->isDone) {
                int oldNThreads = static_cast<int>(this->threads.size());
                if (oldNThreads <= nThreads) {  // if the number of threads is increased
                    this->threads.resize(nThreads);
                    this->flags.resize(nThreads);

                    for (int i = oldNThreads; i < nThreads; ++i) {
                        this->flags[i] = std::make_shared<std::atomic<bool>>(false);
                        this->set_thread(i);
                    }
                }
                else {  // the number of threads is decreased
                    for (int i = oldNThreads - 1; i >= nThreads; --i) {
                        *this->flags[i] = true;  // this thread will finish
                        this->threads[i]->detach();
                    }
                    {
                        // stop the detached threads that were waiting
                        std::unique_lock<std::mutex> lock(this->mutex);
                        this->cv.notify_all();
                    }
                    this->threads.resize(nThreads);  // safe to delete because the threads are detached
                    this->flags.resize(nThreads);  // safe to delete because the threads have copies of shared_ptr of the flags, not originals
                }
            }
        }

        // empty the queue
        void clear_queue() {
            task_info * task_info_ptr;
            while (this->q.pop(task_info_ptr)) {
                delete task_info_ptr->task;
                delete task_info_ptr;
            }  // empty the queue
        }

        // pops a functional wraper to the original function
        std::function<void(int)> pop() {
            std::function<void(int id)> * _f = nullptr;
            this->q.pop(_f);
            std::unique_ptr<std::function<void(int id)>> func(_f);  // at return, delete the function even if an exception occurred
            
            std::function<void(int)> f;
            if (_f)
                f = *_f;
            return f;
        }


        // wait for all computing threads to finish and stop all threads
        // may be called asyncronously to not pause the calling thread while waiting
        // if isWait == true, all the functions in the queue are run, otherwise the queue is cleared without running the functions
        void stop(bool isWait = false) {
            if (!isWait) {
                if (this->isStop)
                    return;
                this->isStop = true;
                for (int i = 0, n = this->size(); i < n; ++i) {
                    *this->flags[i] = true;  // command the threads to stop
                }
                this->clear_queue();  // empty the queue
            }
            else {
                if (this->isDone || this->isStop)
                    return;
                this->isDone = true;  // give the waiting threads a command to finish
            }
            {
                std::unique_lock<std::mutex> lock(this->mutex);
                this->cv.notify_all();  // stop all waiting threads
            }
            for (int i = 0; i < static_cast<int>(this->threads.size()); ++i) {  // wait for the computing threads to finish
                if (this->threads[i]->joinable())
                    this->threads[i]->join();
            }
            // if there were no threads in the pool but some functors in the queue, the functors are not deleted by the threads
            // therefore delete them here
            this->clear_queue();
            this->threads.clear();
            this->flags.clear();
        }

        template<typename F, typename... Rest>
        auto push(F && f, Rest&&... rest) ->std::future<decltype(f(0, rest...))> {
            return push(0, std::forward<F>(f), std::forward<Rest>(rest)...);
        }

        // push with resource ID for resource conflict handling
        template<typename F, typename... Rest>
        auto push(std::size_t resource_id, F && f, Rest&&... rest) ->std::future<decltype(f(0, rest...))> {
            auto pck = std::make_shared<std::packaged_task<decltype(f(0, rest...))(int)>>(std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Rest>(rest)...));

            auto task_func = new std::function<void(int id)>([pck](int id) {
                (*pck)(id);
            });
            
            auto task_info_ptr = new task_info;
            task_info_ptr->task = task_func;
            task_info_ptr->resource_id = resource_id;
            
            if (cfg.enable_resource_conflict_handling && resource_id != 0) {
                // 资源冲突处理逻辑：将任务放入对应的资源队列
                std::lock_guard<std::mutex> lock(resource_mutex);
                
                // 检查资源是否被锁定
                if (!resource_locks[resource_id].load()) {
                    // 资源未被锁定，直接执行任务
                    resource_locks[resource_id] = true;
                    this->q.push(task_info_ptr);
                    
                    std::unique_lock<std::mutex> lock(this->mutex);
                    this->cv.notify_one();
                } else {
                    // 资源已被锁定，将任务放入资源队列
                    resource_queues[resource_id].push(task_info_ptr);
                }
            } else {
                // 普通任务，直接放入队列
                this->q.push(task_info_ptr);
                
                std::unique_lock<std::mutex> lock(this->mutex);
                this->cv.notify_one();
            }

            return pck->get_future();
        }


    private:

        // deleted
        thread_pool(const thread_pool &);// = delete;
        thread_pool(thread_pool &&);// = delete;
        thread_pool & operator=(const thread_pool &);// = delete;
        thread_pool & operator=(thread_pool &&);// = delete;

        // 异常处理函数实现
        void handle_exception(int thread_id, const std::exception & e, const std::function<void(int id)> & task) {
            try {
                // 记录异常信息
                std::stringstream ss;
                ss << "Thread " << thread_id << " exception: " << e.what();
                
                // 统计异常
                stats.failed_tasks++;
                
                // 检查是否为空指针异常
                if (typeid(e) == typeid(std::runtime_error) && 
                    std::string(e.what()).find("nullptr") != std::string::npos) {
                    stats.null_ptr_exceptions++;
                } else if (typeid(e) == typeid(std::runtime_error) && 
                           std::string(e.what()).find("timeout") != std::string::npos) {
                    stats.timeout_exceptions++;
                } else if (typeid(e) == typeid(std::runtime_error) && 
                           std::string(e.what()).find("deadlock") != std::string::npos) {
                    stats.deadlock_exceptions++;
                }
                
                // 其他类型异常处理...
                
            } catch (...) {
                // 避免异常处理函数本身抛出异常
            }
        }
        
        // 任务执行包装器实现
        void execute_task(int thread_id, std::function<void(int id)> * task, std::size_t resource_id = 0) {
            std::unique_ptr<std::function<void(int id)>> func(task);
            int retry_count = 0;
            bool success = false;
            
            while (!success && (retry_count <= cfg.max_retry_count || cfg.max_retry_count == -1)) {
                try {
                    stats.total_tasks++;
                    
                    if (cfg.enable_network_timeout) {
                        // 超时任务执行逻辑
                        auto task_copy = *task;
                        auto bound_task = std::bind(task_copy, thread_id);
                        std::future<void> future = std::async(std::launch::async, bound_task);
                        
                        // 存储正在运行的任务
                        {
                            std::lock_guard<std::mutex> lock(timeout_mutex);
                            running_tasks[std::this_thread::get_id()] = std::move(future);
                        }
                        
                        // 等待任务完成或超时
                        auto status = running_tasks[std::this_thread::get_id()].wait_for(cfg.default_network_timeout);
                        
                        if (status == std::future_status::timeout) {
                            // 任务超时
                            std::runtime_error e("Network timeout exception");
                            handle_exception(thread_id, e, *task);
                            
                            // 清除超时任务
                            {
                                std::lock_guard<std::mutex> lock(timeout_mutex);
                                running_tasks.erase(std::this_thread::get_id());
                            }
                            
                            if (cfg.enable_null_ptr_protection && retry_count < cfg.max_retry_count) {
                                retry_count++;
                                std::this_thread::sleep_for(cfg.retry_interval);
                            } else {
                                break;
                            }
                        } else {
                            // 任务成功完成
                            stats.successful_tasks++;
                            success = true;
                            
                            // 清除已完成任务
                            {
                                std::lock_guard<std::mutex> lock(timeout_mutex);
                                running_tasks.erase(std::this_thread::get_id());
                            }
                            
                            break;
                        }
                    } else {
                        // 正常执行任务
                        (*task)(thread_id);
                        stats.successful_tasks++;
                        success = true;
                        break;
                    }
                } catch (const std::exception & e) {
                    handle_exception(thread_id, e, *task);
                    
                    // 检查是否为空指针异常且需要重试
                    bool is_null_ptr = (typeid(e) == typeid(std::runtime_error) && 
                                       std::string(e.what()).find("nullptr") != std::string::npos);
                    
                    if (cfg.enable_null_ptr_protection && is_null_ptr && retry_count < cfg.max_retry_count) {
                        retry_count++;
                        // 延迟重试
                        std::this_thread::sleep_for(cfg.retry_interval);
                    } else {
                        // 不再重试
                        break;
                    }
                } catch (...) {
                    std::runtime_error e("Unknown exception");
                    handle_exception(thread_id, e, *task);
                    break;
                }
            }
            
            // 资源冲突处理：任务完成后解锁资源并执行下一个任务
            if (cfg.enable_resource_conflict_handling && resource_id != 0) {
                std::lock_guard<std::mutex> lock(resource_mutex);
                
                // 解锁资源
                resource_locks[resource_id] = false;
                
                // 检查资源队列中是否有等待的任务
                auto it = resource_queues.find(resource_id);
                if (it != resource_queues.end() && !it->second.empty()) {
                    // 取出队列中的下一个任务并执行
                    task_info * next_task_info = it->second.front();
                    it->second.pop();
                    
                    // 锁定资源
                    resource_locks[resource_id] = true;
                    
                    // 将任务放入执行队列
                    this->q.push(next_task_info);
                    
                    std::unique_lock<std::mutex> lock(this->mutex);
                    this->cv.notify_one();
                } else if (it != resource_queues.end() && it->second.empty()) {
                    // 队列已空，移除资源队列
                    resource_queues.erase(it);
                    resource_locks.erase(resource_id);
                }
            }
        }
        
        void set_thread(int i) {
            std::shared_ptr<std::atomic<bool>> flag(this->flags[i]);  // a copy of the shared ptr to the flag
            auto f = [this, i, flag/* a copy of the shared ptr to the flag */]() {
                std::atomic<bool> & _flag = *flag;
                task_info * task_info_ptr;
                bool isPop = this->q.pop(task_info_ptr);
                while (true) {
                    while (isPop) {  // if there is anything in the queue
                        execute_task(i, task_info_ptr->task, task_info_ptr->resource_id);
                        delete task_info_ptr->task;
                        delete task_info_ptr;

                        if (_flag)
                            return;  // the thread is wanted to stop, return even if the queue is not empty yet
                        else
                            isPop = this->q.pop(task_info_ptr);
                    }

                    // the queue is empty here, wait for the next command
                    std::unique_lock<std::mutex> lock(this->mutex);
                    ++this->nWaiting;
                    this->cv.wait(lock, [this, &task_info_ptr, &isPop, &_flag](){ isPop = this->q.pop(task_info_ptr); return isPop || this->isDone || _flag; });
                    --this->nWaiting;

                    if (!isPop)
                        return;  // if the queue is empty and this->isDone == true or *flag then return
                }
            };
            this->threads[i].reset(new std::thread(f));  // compiler may not support std::make_unique()
        }

        // 健康监控统计信息
        struct health_stats {
            std::atomic<long long> total_tasks = 0;
            std::atomic<long long> successful_tasks = 0;
            std::atomic<long long> failed_tasks = 0;
            std::atomic<long long> null_ptr_exceptions = 0;
            std::atomic<long long> timeout_exceptions = 0;
            std::atomic<long long> deadlock_exceptions = 0;
        };

    public:
        // 获取健康统计信息
        health_stats get_health_stats() const {
            return stats;
        }

        // 重置健康统计信息
        void reset_health_stats() {
            health_stats new_stats;
            stats = new_stats;
        }

    private:
        void init() { 
            this->nWaiting = 0; 
            this->isStop = false; 
            this->isDone = false;
        }

        // 异常处理函数
        void handle_exception(int thread_id, const std::exception & e, const std::function<void(int id)> & task);
        
        // 任务执行包装器
        void execute_task(int thread_id, std::function<void(int id)> * task_ptr);

        config cfg;
        health_stats stats;
        
        std::vector<std::unique_ptr<std::thread>> threads;
        std::vector<std::shared_ptr<std::atomic<bool>>> flags;
        mutable boost::lockfree::queue<task_info *> q;
        std::atomic<bool> isDone;
        std::atomic<bool> isStop;
        std::atomic<int> nWaiting;  // how many threads are waiting

        std::mutex mutex;
        std::condition_variable cv;
        
        // 任务信息结构体
        struct task_info {
            std::function<void(int id)> * task;
            std::size_t resource_id;
        };
        
        // 资源冲突处理相关
        std::mutex resource_mutex;
        std::unordered_map<std::size_t, std::queue<task_info *>> resource_queues;
        std::unordered_map<std::size_t, std::atomic<bool>> resource_locks;
        
        // 超时任务相关
        std::mutex timeout_mutex;
        std::unordered_map<std::thread::id, std::future<void>> running_tasks;
    };

}

#endif // __ctpl_thread_pool_H__

