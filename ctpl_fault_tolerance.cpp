#include "ctpl_fault_tolerance.h"
#include <chrono>
#include <thread>
#include <sstream>
#include <algorithm>

CTPL_Fault_Tolerance::CTPL_Fault_Tolerance() 
    : is_initialized(false), is_running(false), ft_strategy(FaultToleranceStrategy::RESTART_TASK),
      cp_strategy(CheckpointStrategy::NO_CHECKPOINT), checkpoint_interval(5000) {
}

CTPL_Fault_Tolerance::~CTPL_Fault_Tolerance() {
    if (is_running) {
        is_running = false;
        if (cleanup_thread && cleanup_thread->joinable()) {
            cleanup_thread->join();
        }
    }
}

bool CTPL_Fault_Tolerance::init(const std::string &node_id, FaultToleranceStrategy ft_strategy, CheckpointStrategy cp_strategy) {
    if (is_initialized) {
        return false;
    }
    
    this->node_id = node_id;
    this->ft_strategy = ft_strategy;
    this->cp_strategy = cp_strategy;
    
    is_initialized = true;
    is_running = true;
    
    // 启动检查点清理线程
    cleanup_thread = std::make_unique<std::thread>(&CTPL_Fault_Tolerance::checkpoint_cleanup_thread, this);
    
    return true;
}

bool CTPL_Fault_Tolerance::enable_persistence(const std::string &storage_path) {
    if (!is_initialized) {
        return false;
    }
    
    this->storage_path = storage_path;
    // 这里可以添加实际的持久化初始化逻辑
    return true;
}

bool CTPL_Fault_Tolerance::set_checkpoint_interval(int interval_ms) {
    if (!is_initialized) {
        return false;
    }
    
    checkpoint_interval = interval_ms;
    return true;
}

bool CTPL_Fault_Tolerance::create_checkpoint(const std::string &task_id, const std::unordered_map<std::string, std::string> &state, const std::string &metadata) {
    if (!is_initialized) {
        return false;
    }
    
    uint64_t timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
    std::string serialized = serialize_state(state);
    
    RecoveryPoint checkpoint;
    checkpoint.checkpoint_id = "cp_" + task_id + "_" + std::to_string(timestamp);
    checkpoint.task_id = task_id;
    checkpoint.serialized_state = serialized;
    checkpoint.timestamp = timestamp;
    checkpoint.metadata = metadata;
    
    { 
        std::unique_lock<std::mutex> lock(checkpoints_mutex);
        checkpoints[task_id].push_back(checkpoint);
    }
    
    // 这里可以添加实际的持久化保存逻辑
    return true;
}

bool CTPL_Fault_Tolerance::create_checkpoint_for_dag(const std::string &dag_id, const std::vector<std::string> &task_ids, const std::string &metadata) {
    if (!is_initialized) {
        return false;
    }
    
    bool all_success = true;
    for (const auto &task_id : task_ids) {
        // 这里可以添加DAG级别的检查点创建逻辑
        // 简单实现：为每个任务创建检查点
        Task task = get_task_state(task_id);
        if (task.task_id != "") {
            std::unordered_map<std::string, std::string> empty_state;
            if (!create_checkpoint(task_id, empty_state, "DAG: " + dag_id + " " + metadata)) {
                all_success = false;
            }
        }
    }
    
    return all_success;
}

std::vector<RecoveryPoint> CTPL_Fault_Tolerance::get_checkpoints(const std::string &task_id) {
    std::vector<RecoveryPoint> result;
    
    if (!is_initialized) {
        return result;
    }
    
    { 
        std::unique_lock<std::mutex> lock(checkpoints_mutex);
        
        if (task_id.empty()) {
            // 返回所有检查点
            for (const auto &pair : checkpoints) {
                for (const auto &cp : pair.second) {
                    result.push_back(cp);
                }
            }
        } else {
            // 返回特定任务的检查点
            auto it = checkpoints.find(task_id);
            if (it != checkpoints.end()) {
                result = it->second;
            }
        }
    }
    
    return result;
}

RecoveryPoint CTPL_Fault_Tolerance::get_latest_checkpoint(const std::string &task_id) {
    RecoveryPoint result;
    
    if (!is_initialized) {
        return result;
    }
    
    { 
        std::unique_lock<std::mutex> lock(checkpoints_mutex);
        
        auto it = checkpoints.find(task_id);
        if (it != checkpoints.end() && !it->second.empty()) {
            // 找到最新的检查点
            const auto &cps = it->second;
            result = *std::max_element(cps.begin(), cps.end(), [](const RecoveryPoint &a, const RecoveryPoint &b) {
                return a.timestamp < b.timestamp;
            });
        }
    }
    
    return result;
}

bool CTPL_Fault_Tolerance::delete_checkpoint(const std::string &checkpoint_id) {
    if (!is_initialized) {
        return false;
    }
    
    { 
        std::unique_lock<std::mutex> lock(checkpoints_mutex);
        
        for (auto &pair : checkpoints) {
            auto &cps = pair.second;
            auto it = std::remove_if(cps.begin(), cps.end(), [&checkpoint_id](const RecoveryPoint &cp) {
                return cp.checkpoint_id == checkpoint_id;
            });
            
            if (it != cps.end()) {
                cps.erase(it, cps.end());
                return true;
            }
        }
    }
    
    return false;
}

bool CTPL_Fault_Tolerance::cleanup_old_checkpoints(int max_keep) {
    if (!is_initialized) {
        return false;
    }
    
    { 
        std::unique_lock<std::mutex> lock(checkpoints_mutex);
        
        for (auto &pair : checkpoints) {
            auto &cps = pair.second;
            if (cps.size() > static_cast<size_t>(max_keep)) {
                // 按时间排序并保留最新的max_keep个
                std::sort(cps.begin(), cps.end(), [](const RecoveryPoint &a, const RecoveryPoint &b) {
                    return a.timestamp < b.timestamp;
                });
                
                cps.erase(cps.begin(), cps.end() - max_keep);
            }
        }
    }
    
    return true;
}

bool CTPL_Fault_Tolerance::recover_from_checkpoint(const RecoveryPoint &checkpoint) {
    if (!is_initialized) {
        return false;
    }
    
    // 这里可以添加实际的恢复逻辑
    if (recovery_callback) {
        recovery_callback(checkpoint.task_id, checkpoint);
    }
    
    return true;
}

bool CTPL_Fault_Tolerance::restart_failed_task(const std::string &task_id, TaskPriority priority) {
    if (!is_initialized) {
        return false;
    }
    
    // 简单实现：将任务状态重置为PENDING
    { 
        std::unique_lock<std::mutex> lock(task_states_mutex);
        
        auto it = task_states.find(task_id);
        if (it != task_states.end()) {
            it->second.status = TaskStatus::PENDING;
            it->second.priority = priority;
            it->second.retries++;
        }
    }
    
    return true;
}

bool CTPL_Fault_Tolerance::restart_dag(const std::string &dag_id) {
    if (!is_initialized) {
        return false;
    }
    
    // 简单实现：将所有任务状态重置为PENDING
    { 
        std::unique_lock<std::mutex> lock(task_states_mutex);
        
        for (auto &pair : task_states) {
            pair.second.status = TaskStatus::PENDING;
        }
    }
    
    return true;
}

bool CTPL_Fault_Tolerance::migrate_task(const std::string &task_id, const std::string &target_node_id, const std::unordered_map<std::string, std::string> &state) {
    if (!is_initialized) {
        return false;
    }
    
    uint64_t migration_time = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
    
    TaskMigrationInfo info;
    info.task_id = task_id;
    info.source_node_id = node_id;
    info.target_node_id = target_node_id;
    info.serialized_state = serialize_state(state);
    info.migration_time = migration_time;
    info.success = true; // 模拟成功
    
    { 
        std::unique_lock<std::mutex> lock(migration_mutex);
        migration_history.push_back(info);
    }
    
    if (migration_callback) {
        migration_callback(info);
    }
    
    return true;
}

std::vector<TaskMigrationInfo> CTPL_Fault_Tolerance::get_migration_history() {
    { 
        std::unique_lock<std::mutex> lock(migration_mutex);
        return migration_history;
    }
}

bool CTPL_Fault_Tolerance::set_task_state(const std::string &task_id, const Task &task) {
    if (!is_initialized) {
        return false;
    }
    
    { 
        std::unique_lock<std::mutex> lock(task_states_mutex);
        task_states[task_id] = task;
    }
    
    return true;
}

Task CTPL_Fault_Tolerance::get_task_state(const std::string &task_id) {
    Task result;
    
    if (!is_initialized) {
        return result;
    }
    
    { 
        std::unique_lock<std::mutex> lock(task_states_mutex);
        
        auto it = task_states.find(task_id);
        if (it != task_states.end()) {
            result = it->second;
        }
    }
    
    return result;
}

bool CTPL_Fault_Tolerance::register_recovery_callback(std::function<void(const std::string &task_id, const RecoveryPoint &checkpoint)> callback) {
    recovery_callback = callback;
    return true;
}

bool CTPL_Fault_Tolerance::register_migration_callback(std::function<void(const TaskMigrationInfo &info)> callback) {
    migration_callback = callback;
    return true;
}

void CTPL_Fault_Tolerance::checkpoint_cleanup_thread() {
    while (is_running) {
        // 定期清理旧检查点
        cleanup_old_checkpoints(10);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(checkpoint_interval * 60));
    }
}

std::string CTPL_Fault_Tolerance::serialize_state(const std::unordered_map<std::string, std::string> &state) {
    // 简单的序列化实现：使用JSON格式
    std::stringstream ss;
    ss << "{";
    bool first = true;
    
    for (const auto &pair : state) {
        if (!first) {
            ss << ",";
        }
        ss << "\"" << pair.first << "\":\"" << pair.second << "\"";
        first = false;
    }
    
    ss << "}";
    return ss.str();
}

std::unordered_map<std::string, std::string> CTPL_Fault_Tolerance::deserialize_state(const std::string &serialized) {
    std::unordered_map<std::string, std::string> state;
    
    // 简单的反序列化实现：解析JSON格式
    // 注意：这里只是一个示例，生产环境应该使用更可靠的JSON解析库
    
    return state;
}
