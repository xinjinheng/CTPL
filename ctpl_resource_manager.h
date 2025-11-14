#ifndef __ctpl_resource_manager_H__
#define __ctpl_resource_manager_H__

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>

// 资源使用情况结构体
struct ResourceUsage {
    double cpu_usage;
    double memory_usage;
    double gpu_usage;
    int running_tasks;
    int pending_tasks;
    uint64_t timestamp;
};

// 资源池结构体
struct ResourcePool {
    std::string pool_id;
    int num_cpus;
    int num_gpus;
    size_t memory_limit;
    int max_threads;
    int current_threads;
    int active_tasks;
};

// 多租户资源分配结构体
struct TenantResource {
    std::string tenant_id;
    int cpu_quota;
    int gpu_quota;
    size_t memory_quota;
    int max_concurrent_tasks;
    int priority;
};

// 资源管理模块类
class CTPL_Resource_Manager {
public:
    CTPL_Resource_Manager();
    ~CTPL_Resource_Manager();
    
    // 资源监控
    bool enable_resource_monitoring(int interval_ms = 1000);
    
    bool set_cpu_threshold(double threshold);
    
    bool set_memory_threshold(double threshold);
    
    bool set_gpu_threshold(double threshold);
    
    ResourceUsage get_current_resource_usage();
    
    // 弹性伸缩
    bool enable_elastic_scaling(int min_threads = 1, int max_threads = 100);
    
    bool set_scale_up_policy(std::function<bool(const ResourceUsage &)> policy);
    
    bool set_scale_down_policy(std::function<bool(const ResourceUsage &)> policy);
    
    int recommend_thread_count();
    
    // 资源池管理
    bool create_resource_pool(const std::string &pool_id, int num_cpus, int num_gpus, size_t memory_limit, int max_threads);
    
    bool allocate_resource(const std::string &pool_id, int num_cpus, int num_gpus, size_t memory);
    
    bool release_resource(const std::string &pool_id, int num_cpus, int num_gpus, size_t memory);
    
    // 多租户管理
    bool create_tenant(const std::string &tenant_id, int cpu_quota, int gpu_quota, size_t memory_quota, int max_concurrent_tasks, int priority);
    
    bool allocate_tenant_resource(const std::string &tenant_id, int num_cpus, int num_gpus, size_t memory);
    
    bool release_tenant_resource(const std::string &tenant_id, int num_cpus, int num_gpus, size_t memory);
    
private:
    // 资源监控线程函数
    void resource_monitor_thread();
    
    ResourceUsage current_resource;
    std::mutex resource_mutex;
    
    // 弹性伸缩参数
    std::atomic<bool> is_elastic_scaling_enabled;
    std::atomic<int> min_threads;
    std::atomic<int> max_threads;
    
    // 阈值设置
    std::atomic<double> cpu_threshold;
    std::atomic<double> memory_threshold;
    std::atomic<double> gpu_threshold;
    
    // 资源池
    std::unordered_map<std::string, ResourcePool> resource_pools;
    
    // 多租户资源分配
    std::unordered_map<std::string, TenantResource> tenant_resources;
    
    std::atomic<bool> is_resource_monitoring_enabled;
    std::atomic<int> monitoring_interval;
    std::unique_ptr<std::thread> monitor_thread;
    
    // 弹性伸缩策略
    std::function<bool(const ResourceUsage &)> scale_up_policy;
    std::function<bool(const ResourceUsage &)> scale_down_policy;
};

#endif // __ctpl_resource_manager_H__