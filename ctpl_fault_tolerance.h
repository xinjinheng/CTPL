#ifndef __ctpl_fault_tolerance_H__
#define __ctpl_fault_tolerance_H__

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>
#include <chrono>
#include "ctpl_dag_scheduler.h" // 引入Task和Checkpoint结构体

// 容错策略枚举
enum class FaultToleranceStrategy {
    NO_RECOVERY,
    RESTART_TASK,
    RESTART_DAG,
    MIGRATE_TASK
};

// 检查点策略枚举
enum class CheckpointStrategy {
    NO_CHECKPOINT,
    PERIODIC,
    ON_TASK_COMPLETE,
    ON_DEMAND
};

// 恢复点结构体
struct RecoveryPoint {
    std::string checkpoint_id;
    std::string task_id;
    std::string serialized_state;
    uint64_t timestamp;
    std::string metadata;
};

// 任务迁移信息结构体
struct TaskMigrationInfo {
    std::string task_id;
    std::string source_node_id;
    std::string target_node_id;
    std::string serialized_state;
    uint64_t migration_time;
    bool success;
};

// 容错与检查点模块类
class CTPL_Fault_Tolerance {
public:
    CTPL_Fault_Tolerance();
    ~CTPL_Fault_Tolerance();
    
    // 初始化与配置
    bool init(const std::string &node_id, FaultToleranceStrategy ft_strategy, CheckpointStrategy cp_strategy);
    
    bool enable_persistence(const std::string &storage_path);
    
    bool set_checkpoint_interval(int interval_ms);
    
    // 检查点操作
    bool create_checkpoint(const std::string &task_id, const std::unordered_map<std::string, std::string> &state, const std::string &metadata = "");
    
    bool create_checkpoint_for_dag(const std::string &dag_id, const std::vector<std::string> &task_ids, const std::string &metadata = "");
    
    std::vector<RecoveryPoint> get_checkpoints(const std::string &task_id = "");
    
    RecoveryPoint get_latest_checkpoint(const std::string &task_id);
    
    bool delete_checkpoint(const std::string &checkpoint_id);
    
    bool cleanup_old_checkpoints(int max_keep = 10);
    
    // 容错恢复
    bool recover_from_checkpoint(const RecoveryPoint &checkpoint);
    
    bool restart_failed_task(const std::string &task_id, TaskPriority priority = TaskPriority::NORMAL);
    
    bool restart_dag(const std::string &dag_id);
    
    // 任务迁移
    bool migrate_task(const std::string &task_id, const std::string &target_node_id, const std::unordered_map<std::string, std::string> &state = {});
    
    std::vector<TaskMigrationInfo> get_migration_history();
    
    // 状态管理
    bool set_task_state(const std::string &task_id, const Task &task);
    
    Task get_task_state(const std::string &task_id);
    
    bool register_recovery_callback(std::function<void(const std::string &task_id, const RecoveryPoint &checkpoint)> callback);
    
    bool register_migration_callback(std::function<void(const TaskMigrationInfo &info)> callback);
    
private:
    // 检查点清理线程
    void checkpoint_cleanup_thread();
    
    // 序列化函数
    std::string serialize_state(const std::unordered_map<std::string, std::string> &state);
    
    // 反序列化函数
    std::unordered_map<std::string, std::string> deserialize_state(const std::string &serialized);
    
    std::string node_id;
    FaultToleranceStrategy ft_strategy;
    CheckpointStrategy cp_strategy;
    
    std::string storage_path;
    int checkpoint_interval;
    
    std::unordered_map<std::string, std::vector<RecoveryPoint>> checkpoints;
    std::unordered_map<std::string, Task> task_states;
    
    std::vector<TaskMigrationInfo> migration_history;
    
    std::mutex checkpoints_mutex;
    std::mutex task_states_mutex;
    std::mutex migration_mutex;
    
    std::atomic<bool> is_initialized;
    std::atomic<bool> is_running;
    
    std::unique_ptr<std::thread> cleanup_thread;
    
    std::function<void(const std::string &task_id, const RecoveryPoint &checkpoint)> recovery_callback;
    std::function<void(const TaskMigrationInfo &info)> migration_callback;
};

#endif // __ctpl_fault_tolerance_H__