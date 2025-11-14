#ifndef __ctpl_dag_scheduler_H__
#define __ctpl_dag_scheduler_H__

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <unordered_set>

// DAG任务状态枚举
enum class TaskStatus {
    PENDING,
    READY,
    RUNNING,
    COMPLETED,
    FAILED
};

// DAG任务优先级枚举
enum class TaskPriority {
    LOW,
    NORMAL,
    HIGH,
    CRITICAL
};

// DAG任务类型枚举
enum class TaskType {
    COMPUTATION,
    IO,
    COMMUNICATION,
    MEMORY
};

// 检查点结构体
struct Checkpoint {
    std::string task_id;
    std::string serialized_data;
    std::string metadata;
};

// DAG任务结构体
struct Task {
    std::string task_id;
    std::function<void(const std::string &id, const std::unordered_map<std::string, std::string> &inputs)> task_func;
    TaskStatus status;
    TaskPriority priority;
    TaskType type;
    std::vector<std::string> dependencies;
    std::vector<std::string> dependents;
    std::unordered_map<std::string, std::string> inputs;
    std::unordered_map<std::string, std::string> outputs;
    int retries = 0;
    int max_retries = 3;
};

// DAG调度器类
class CTPL_DAG_Scheduler {
public:
    CTPL_DAG_Scheduler();
    ~CTPL_DAG_Scheduler();
    
    // DAG任务管理
    std::string create_task(
        std::function<void(const std::string &id, const std::unordered_map<std::string, std::string> &inputs)> func,
        TaskPriority priority = TaskPriority::NORMAL,
        TaskType type = TaskType::COMPUTATION,
        int max_retries = 3
    );
    
    bool add_dependency(const std::string &from_task_id, const std::string &to_task_id);
    
    std::vector<std::string> get_ready_tasks();
    
    bool mark_task_completed(const std::string &task_id, const std::unordered_map<std::string, std::string> &outputs);
    
    bool mark_task_failed(const std::string &task_id);
    
    bool has_cycle();
    
    void clear_all_tasks();
    
    // 动态优先级调整
    void set_task_priority(const std::string &task_id, TaskPriority new_priority);
    
    // 任务重试
    bool retry_task(const std::string &task_id);
    
private:
    // DAG循环检测辅助函数
    bool dfs_cycle_check(const std::string &task_id, std::unordered_map<std::string, bool> &visited, std::unordered_map<std::string, bool> &rec_stack);
    
    std::unordered_map<std::string, Task> tasks;
    std::mutex tasks_mutex;
    std::atomic<int> task_counter;
};

#endif // __ctpl_dag_scheduler_H__