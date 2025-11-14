#ifndef __ctpl_distributed_H__
#define __ctpl_distributed_H__

#include "ctpl_thread_pool_core.h"
#include "ctpl_dag_scheduler.h"
#include "ctpl_stream_processing.h"
#include "ctpl_resource_manager.h"
#include "ctpl_network_manager.h"
#include "ctpl_fault_tolerance.h"
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <exception>
#include <future>
#include <mutex>
// #include <boost/lockfree/queue.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <chrono>
#include <tuple>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

// 前向声明ZeroMQ类
namespace zmq {
    class context_t;
    class socket_t;
    class message_t;
    class pollitem_t;
}

// 任务状态枚举
enum class TaskStatus {
    PENDING,      // 等待执行
    RUNNING,      // 执行中
    COMPLETED,    // 执行完成
    FAILED,       // 执行失败
    SKIPPED,      // 已跳过
    CANCELED      // 已取消
};

// 任务优先级枚举
enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    URGENT = 3
};

// 任务类型枚举
enum class TaskType {
    CPU_INTENSIVE,
    IO_INTENSIVE,
    MIXED
};

// 检查点结构体
struct Checkpoint {
    std::string data;              // 检查点数据
    std::chrono::system_clock::time_point timestamp;  // 时间戳
};

// 任务结构体
struct Task {
    std::string id;                // 任务ID
    std::function<void(int)> func;  // 任务函数
    TaskStatus status;            // 任务状态
    TaskPriority priority;        // 任务优先级
    TaskType type;                // 任务类型
    std::unordered_set<std::string> dependencies;  // 依赖任务ID
    std::unordered_set<std::string> dependents;     // 依赖该任务的任务ID
    Checkpoint checkpoint;        // 检查点
    int retry_count;              // 重试次数
    int max_retries;              // 最大重试次数
    std::function<void(const std::string &)> failure_handler;  // 失败处理函数
};

// DAG任务管理器
class DAGManager {
public:
    DAGManager() = default;
    
    // 创建任务
    std::string create_task(std::function<void(int)> func, TaskPriority priority = TaskPriority::NORMAL, TaskType type = TaskType::MIXED) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        
        std::stringstream ss;
        ss << std::hex;
        for (int i = 0; i < 32; ++i) {
            ss << dis(gen);
        }
        
        std::string task_id = ss.str();
        
        std::lock_guard<std::mutex> lock(mutex);
        
        Task task;
        task.id = task_id;
        task.func = func;
        task.status = TaskStatus::PENDING;
        task.priority = priority;
        task.type = type;
        task.retry_count = 0;
        task.max_retries = 3;
        
        tasks[task_id] = std::move(task);
        
        return task_id;
    }
    
    // 添加依赖关系
    bool add_dependency(const std::string &parent, const std::string &child) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto parent_it = tasks.find(parent);
        auto child_it = tasks.find(child);
        
        if (parent_it == tasks.end() || child_it == tasks.end()) {
            return false;
        }
        
        // 检查是否会形成循环
        child_it->second.dependencies.insert(parent);
        parent_it->second.dependents.insert(child);
        
        if (has_cycle()) {
            // 形成了循环，撤销操作
            child_it->second.dependencies.erase(parent);
            parent_it->second.dependents.erase(child);
            return false;
        }
        
        return true;
    }
    
    // 移除依赖关系
    bool remove_dependency(const std::string &parent, const std::string &child) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto parent_it = tasks.find(parent);
        auto child_it = tasks.find(child);
        
        if (parent_it == tasks.end() || child_it == tasks.end()) {
            return false;
        }
        
        child_it->second.dependencies.erase(parent);
        parent_it->second.dependents.erase(child);
        
        return true;
    }
    
    // 获取可执行任务（没有未完成依赖的任务）
    std::vector<std::string> get_ready_tasks() {
        std::lock_guard<std::mutex> lock(mutex);
        
        std::vector<std::string> ready_tasks;
        
        for (auto &[task_id, task] : tasks) {
            if (task.status == TaskStatus::PENDING) {
                bool all_deps_completed = true;
                for (const auto &dep : task.dependencies) {
                    auto dep_it = tasks.find(dep);
                    if (dep_it == tasks.end() || dep_it->second.status != TaskStatus::COMPLETED) {
                        all_deps_completed = false;
                        break;
                    }
                }
                
                if (all_deps_completed) {
                    ready_tasks.push_back(task_id);
                }
            }
        }
        
        // 按优先级排序
        std::sort(ready_tasks.begin(), ready_tasks.end(), [this](const std::string &a, const std::string &b) {
            return tasks.at(a).priority > tasks.at(b).priority;
        });
        
        return ready_tasks;
    }
    
    // 标记任务完成
    void mark_task_completed(const std::string &task_id) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = tasks.find(task_id);
        if (it != tasks.end()) {
            it->second.status = TaskStatus::COMPLETED;
        }
    }
    
    // 标记任务失败
    void mark_task_failed(const std::string &task_id, bool retry = false) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = tasks.find(task_id);
        if (it != tasks.end()) {
            if (retry && it->second.retry_count < it->second.max_retries) {
                it->second.retry_count++;
                it->second.status = TaskStatus::PENDING;
            } else {
                it->second.status = TaskStatus::FAILED;
                
                // 执行失败处理函数
                if (it->second.failure_handler) {
                    it->second.failure_handler(task_id);
                }
            }
        }
    }
    
    // 检查DAG是否有循环
    bool has_cycle() const {
        std::unordered_map<std::string, int> in_degree;
        
        // 计算每个节点的入度
        for (const auto &[task_id, task] : tasks) {
            in_degree[task_id] = task.dependencies.size();
        }
        
        std::queue<std::string> q;
        
        // 将入度为0的节点加入队列
        for (const auto &[task_id, degree] : in_degree) {
            if (degree == 0) {
                q.push(task_id);
            }
        }
        
        int processed = 0;
        
        while (!q.empty()) {
            std::string current = q.front();
            q.pop();
            processed++;
            
            // 减少所有邻接节点的入度
            auto it = tasks.find(current);
            if (it != tasks.end()) {
                for (const auto &dependent : it->second.dependents) {
                    if (--in_degree[dependent] == 0) {
                        q.push(dependent);
                    }
                }
            }
        }
        
        return processed != tasks.size();
    }
    
    // 获取任务
    Task* get_task(const std::string &task_id) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = tasks.find(task_id);
        if (it != tasks.end()) {
            return &it->second;
        }
        
        return nullptr;
    }
    
    // 清空所有任务
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        tasks.clear();
    }
    
private:
    // 检查循环的辅助函数
    bool dfs_cycle_check(const std::string &task_id, std::unordered_set<std::string> &visited, std::unordered_set<std::string> &rec_stack) const {
        if (visited.find(task_id) == visited.end()) {
            visited.insert(task_id);
            rec_stack.insert(task_id);
            
            auto it = tasks.find(task_id);
            if (it != tasks.end()) {
                for (const auto &dependent : it->second.dependents) {
                    if (visited.find(dependent) == visited.end()) {
                        if (dfs_cycle_check(dependent, visited, rec_stack)) {
                            return true;
                        }
                    } else if (rec_stack.find(dependent) != rec_stack.end()) {
                        return true;
                    }
                }
            }
            
            rec_stack.erase(task_id);
        }
        
        return false;
    }
    
    std::unordered_map<std::string, Task> tasks;
    mutable std::mutex mutex;
};

// 分布式线程池类
class distributed_thread_pool : public ctpl_thread_pool_core {
public:
    distributed_thread_pool(int nThreads = 1, int queueSize = -1)
        : ctpl_thread_pool_core(nThreads, queueSize) {
        // 初始化模块
        network_manager.init("tcp://*:5555");
        
        // 启动DAG调度器线程
        stop_dag_scheduler = false;
        dag_scheduler_thread = std::thread(&distributed_thread_pool::dag_scheduler_thread, this);
        
        // 启动资源监控线程
        stop_resource_monitor = false;
        resource_monitor_thread = std::thread(&distributed_thread_pool::resource_monitor_thread, this);
        
        // 启动网络线程
        stop_network = false;
        network_thread = std::thread(&distributed_thread_pool::network_thread, this);
        
        // 容错设置
        persistence_enabled = false;
        checkpoint_interval_ms = 5000;
    }
    
    ~distributed_thread_pool() {
        stop(true);
        
        // 清理模块
        network_manager.cleanup();
    }
    
    // 重写stop方法以清理所有模块
    void stop(bool isWait = false) {
        // 停止基础线程池
        ctpl_thread_pool_core::stop(isWait);
        
        // 停止其他线程
        stop_dag_scheduler = true;
        if (dag_scheduler.joinable()) {
            dag_scheduler.join();
        }
        
        stop_network = true;
        if (network_thread_obj.joinable()) {
            network_thread_obj.join();
        }
        
        stop_resource_monitor = true;
        if (resource_monitor.joinable()) {
            resource_monitor.join();
        }
        
        // 清理数据流
        std::lock_guard<std::mutex> lock(streams_mutex);
        for (auto &[stream_id, stream] : streams) {
            for (int i = 0; i < stream.shard_count; ++i) {
                stream.shards[i]->stop = true;
                std::unique_lock<std::mutex> shard_lock(stream.shards[i]->queue_mutex);
                stream.shards[i]->queue_cv.notify_all();
                shard_lock.unlock();
            }
            
            for (auto &thread : stream.processing_threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
        }
        
        // 清理模块
        //dag_manager.clear();
    }
    
    // DAG任务调度
    std::string create_dag_task(std::function<void(int)> func, TaskPriority priority = TaskPriority::NORMAL, TaskType type = TaskType::MIXED) {
        return dag_scheduler.create_task(func, priority, type);
    }
    
    bool add_dependency(const std::string &parent, const std::string &child) {
        return dag_scheduler.add_dependency(parent, child);
    }
    
    bool remove_dependency(const std::string &parent, const std::string &child) {
        return dag_scheduler.remove_dependency(parent, child);
    }
    
    void submit_dag() {
        // 启动DAG调度器线程
        if (!dag_scheduler.joinable()) {
            stop_dag_scheduler = false;
            dag_scheduler = std::thread(&distributed_thread_pool::dag_scheduler_thread, this);
        }
        
        // 立即触发一次调度
        std::unique_lock<std::mutex> lock(dag_mutex);
        dag_cv.notify_one();
    }
    
    // 单任务提交（保持兼容性）
    template<typename F, typename... Rest>
    auto push(F &&f, Rest&&... rest) -> std::future<decltype(f(0, rest...))> {
        auto pck = std::make_shared<std::packaged_task<decltype(f(0, rest...))(int)>>(std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Rest>(rest)...));
        
        auto _f = new std::function<void(int id)>([pck](int id) {
            (*pck)(id);
        });
        
        { 
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->q.push(_f);
        }
        
        std::unique_lock<std::mutex> lock(this->mutex);
        this->cv.notify_one();
        
        return pck->get_future();
    }
    
    template<typename F>
    auto push(F &&f) -> std::future<decltype(f(0))> {
        auto pck = std::make_shared<std::packaged_task<decltype(f(0))(int)>>(std::forward<F>(f));
        
        auto _f = new std::function<void(int id)>([pck](int id) {
            (*pck)(id);
        });
        
        this->q.push(_f);
        
        std::unique_lock<std::mutex> lock(this->mutex);
        this->cv.notify_one();
        
        return pck->get_future();
    }
    
    // 数据流处理
    std::string create_stream(const std::string &name, int shard_count = 1) {
        std::string stream_id = generate_unique_id();
        
        std::lock_guard<std::mutex> lock(streams_mutex);
        
        Stream stream;
        stream.name = name;
        stream.shard_count = shard_count;
        stream.shards.resize(shard_count);
        stream.processing_threads.resize(shard_count);
        
        for (int i = 0; i < shard_count; ++i) {
            stream.shards[i] = std::make_unique<StreamShard>();
            stream.shards[i]->stop = false;
            stream.processing_threads[i] = std::thread(&distributed_thread_pool::stream_processing_thread, this, stream_id, i);
        }
        
        streams[stream_id] = std::move(stream);
        
        return stream_id;
    }
    
    bool add_window(const std::string &stream_id, int window_size_ms, int slide_interval_ms, std::function<void(const std::vector<std::string> &)> aggregate_func) {
        std::lock_guard<std::mutex> lock(streams_mutex);
        
        auto stream_it = streams.find(stream_id);
        if (stream_it == streams.end()) {
            return false;
        }
        
        Stream::WindowConfig config;
        config.window_size_ms = window_size_ms;
        config.slide_interval_ms = slide_interval_ms;
        config.aggregate_func = aggregate_func;
        
        stream_it->second.windows.push_back(config);
        
        return true;
    }
    
    bool push_stream_data(const std::string &stream_id, const std::string &data) {
        std::lock_guard<std::mutex> lock(streams_mutex);
        
        auto stream_it = streams.find(stream_id);
        if (stream_it == streams.end()) {
            return false;
        }
        
        Stream &stream = stream_it->second;
        
        // 简单的哈希分片
        size_t hash = std::hash<std::string>{}(data) % stream.shard_count;
        
        StreamShard &shard = *stream.shards[hash];
        std::unique_lock<std::mutex> shard_lock(shard.queue_mutex);
        shard.data_queue.push(data);
        shard.queue_cv.notify_one();
        
        return true;
    }
    
    // 资源管理
    void enable_elastic_scaling(bool enable) {
        resource_manager.enable_elastic_scaling(enable);
    }
    
    void set_cpu_threshold(double threshold) {
        resource_manager.set_cpu_threshold(threshold);
    }
    
    void set_memory_threshold(double threshold) {
        resource_manager.set_memory_threshold(threshold);
    }
    
    // 容错设置
    void enable_persistence(bool enable) {
        persistence_enabled = enable;
    }
    
    void set_checkpoint_interval(int interval_ms) {
        checkpoint_interval_ms = interval_ms;
    }
    
    // 多租户管理
    std::string create_tenant(const std::string &name, int max_threads, int max_queue_size) {
        std::string tenant_id = generate_unique_id();
        
        std::lock_guard<std::mutex> lock(tenants_mutex);
        
        Tenant tenant;
        tenant.name = name;
        tenant.max_threads = max_threads;
        tenant.max_queue_size = max_queue_size;
        tenant.current_threads = 0;
        tenant.current_queue_size = 0;
        tenant.priority = 0;
        
        tenants[tenant_id] = std::move(tenant);
        
        return tenant_id;
    }
    
    bool allocate_tenant_resource(const std::string &tenant_id, int threads, int queue_size) {
        std::lock_guard<std::mutex> lock(tenants_mutex);
        
        auto it = tenants.find(tenant_id);
        if (it == tenants.end()) {
            return false;
        }
        
        if (threads > it->second.max_threads || queue_size > it->second.max_queue_size) {
            return false;
        }
        
        it->second.current_threads = threads;
        it->second.current_queue_size = queue_size;
        
        return true;
    }
    
private:
    // 初始化
    void init() {
        nWaiting = 0;
        isStop = false;
        isDone = false;
        stop_dag_scheduler = false;
        stop_network = false;
        stop_resource_monitor = false;
    }
    
    // 初始化网络通信
    void init_network() {
        // 注释掉ZeroMQ初始化，避免依赖
        /*
        try {
            zmq_context = std::make_unique<zmq::context_t>(1);
            zmq_socket = std::make_unique<zmq::socket_t>(*zmq_context, ZMQ_REP);
            
            // 绑定到随机端口
            std::string address = "tcp://*:" + std::to_string(5555 + (rand() % 1000));
            zmq_socket->bind(address);
            
            // 启动网络线程
            network_thread_obj = std::thread(&distributed_thread_pool::network_thread, this);
            
        } catch (const std::exception &e) {
            // 网络初始化失败，继续运行但仅支持本地模式
            zmq_context.reset();
            zmq_socket.reset();
        }
        */
    }
    
    // 网络通信线程
    void network_thread() {
        // 注释掉网络线程实现
        /*
        if (!zmq_socket) return;
        
        zmq::pollitem_t items[] = {
            { *zmq_socket, 0, ZMQ_POLLIN, 0 }
        };
        
        while (!stop_network) {
            zmq::poll(&items[0], 1, 1000);  // 1秒超时
            
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t request;
                zmq_socket->recv(&request);
                
                // 处理请求
                std::string request_str(static_cast<char*>(request.data()), request.size());
                
                // 简单的echo响应
                zmq::message_t response(request_str.size());
                memcpy(response.data(), request_str.data(), request_str.size());
                zmq_socket->send(response);
            }
        }
        */
    }
    
    // 任务执行线程
    void worker_thread(int i) {
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
            
            // 检查DAG任务
            std::vector<std::string> ready_tasks = dag_manager.get_ready_tasks();
            for (const auto &task_id : ready_tasks) {
                Task* task = dag_manager.get_task(task_id);
                if (task && task->status == TaskStatus::PENDING) {
                    task->status = TaskStatus::RUNNING;
                    
                    try {
                        task->func(i);
                        dag_manager.mark_task_completed(task_id);
                    } catch (const std::exception &e) {
                        dag_manager.mark_task_failed(task_id, true);
                    }
                    
                    // 通知DAG调度器有任务完成
                    dag_cv.notify_one();
                }
            }
            
            std::unique_lock<std::mutex> lock(mutex);
            ++nWaiting;
            cv.wait(lock, [this, &_f, &isPop, &_flag]() {
                isPop = q.pop(_f);
                return isPop || isDone || _flag;
            });
            --nWaiting;
            
            if (!isPop) return;
        }
    }
    
    // DAG任务调度线程
    void dag_scheduler_thread() {
        while (!stop_dag_scheduler) {
            // 获取可执行任务
            std::vector<std::string> ready_tasks = dag_scheduler.get_ready_tasks();
            
            if (!ready_tasks.empty()) {
                // 提交任务到线程池
                for (const auto &task_id : ready_tasks) {
                    Task* task = dag_scheduler.get_task(task_id);
                    if (task && task->status == TaskStatus::PENDING) {
                        task->status = TaskStatus::RUNNING;
                        
                        auto f = [this, task_id](int thread_id) {
                            Task* task = dag_scheduler.get_task(task_id);
                            if (task) {
                                try {
                                    task->func(thread_id);
                                    dag_scheduler.mark_task_completed(task_id);
                                } catch (const std::exception &e) {
                                    dag_scheduler.mark_task_failed(task_id, true);
                                }
                            }
                        };
                        
                        push(f);
                    }
                }
            }
            
            // 等待一段时间或直到有新任务
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    // 资源监控线程
    void resource_monitor_thread() {
        while (!stop_resource_monitor) {
            // 监控资源使用情况
            auto usage = resource_manager.monitor_resource();
            
            // 弹性伸缩
            resource_manager.elastic_scaling(usage);
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    // 数据流处理线程
    void stream_processing_thread(const std::string &stream_id, int shard_id) {
        auto stream_it = streams.find(stream_id);
        if (stream_it == streams.end()) return;
        
        Stream &stream = stream_it->second;
        StreamShard &shard = *stream.shards[shard_id];
        
        while (!shard.stop) {
            std::unique_lock<std::mutex> lock(shard.queue_mutex);
            shard.queue_cv.wait_for(lock, std::chrono::milliseconds(100), [&shard]() { return !shard.data_queue.empty() || shard.stop; });
            
            if (shard.stop) break;
            
            std::vector<std::string> batch;
            while (!shard.data_queue.empty()) {
                batch.push_back(shard.data_queue.front());
                shard.data_queue.pop();
            }
            
            lock.unlock();
            
            // 处理窗口计算
            for (const auto &window_config : stream.windows) {
                // 简单的窗口处理实现
                // 实际需要维护窗口状态和滑动逻辑
                window_config.aggregate_func(batch);
            }
        }
    }
    
    // 生成唯一ID
    std::string generate_unique_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        
        std::stringstream ss;
        ss << std::hex;
        for (int i = 0; i < 32; ++i) {
            ss << dis(gen);
        }
        
        return ss.str();
    }
    
    // DAG任务管理
    CTPL_DAG_Scheduler dag_scheduler;
    std::thread dag_scheduler_thread;
    std::atomic<bool> stop_dag_scheduler;
    
    // 网络通信
    CTPL_Network_Manager network_manager;
    std::thread network_thread;
    std::atomic<bool> stop_network;
    
    // 数据流处理
    CTPL_Stream_Processor stream_processor;
    std::mutex streams_mutex;
    
    // 资源管理
    CTPL_Resource_Manager resource_manager;
    std::thread resource_monitor_thread;
    std::atomic<bool> stop_resource_monitor;
    
    // 容错机制
    CTPL_Fault_Tolerance fault_tolerance;
    
    // 容错机制
    bool persistence_enabled;
    int checkpoint_interval_ms;
    
    // 多租户管理
    struct Tenant {
        std::string name;
        int max_threads;
        int max_queue_size;
        int current_threads;
        int current_queue_size;
        int priority;
    };
    
    std::unordered_map<std::string, Tenant> tenants;
    std::mutex tenants_mutex;
    
    std::mutex mutex;
    std::condition_variable cv;
};

#endif // __ctpl_distributed_H__