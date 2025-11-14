#include "ctpl_dag_scheduler.h"
#include <sstream>
#include <algorithm>

CTPL_DAG_Scheduler::CTPL_DAG_Scheduler() : task_counter(0) {
}

CTPL_DAG_Scheduler::~CTPL_DAG_Scheduler() {
    clear_all_tasks();
}

std::string CTPL_DAG_Scheduler::create_task(
    std::function<void(const std::string &id, const std::unordered_map<std::string, std::string> &inputs)> func,
    TaskPriority priority,
    TaskType type,
    int max_retries
) {
    std::stringstream ss;
    ss << "task_" << task_counter++;
    std::string task_id = ss.str();
    
    Task task;
    task.task_id = task_id;
    task.task_func = std::move(func);
    task.status = TaskStatus::PENDING;
    task.priority = priority;
    task.type = type;
    task.max_retries = max_retries;
    
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        tasks[task_id] = std::move(task);
    }
    
    return task_id;
}

bool CTPL_DAG_Scheduler::add_dependency(const std::string &from_task_id, const std::string &to_task_id) {
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        
        auto from_it = tasks.find(from_task_id);
        auto to_it = tasks.find(to_task_id);
        
        if (from_it == tasks.end() || to_it == tasks.end()) {
            return false;
        }
        
        from_it->second.dependents.push_back(to_task_id);
        to_it->second.dependencies.push_back(from_task_id);
        
        to_it->second.status = TaskStatus::PENDING;
    }
    
    return true;
}

std::vector<std::string> CTPL_DAG_Scheduler::get_ready_tasks() {
    std::vector<std::string> ready_tasks;
    
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        
        for (auto &pair : tasks) {
            const Task &task = pair.second;
            
            if (task.status == TaskStatus::PENDING) {
                bool all_deps_ready = true;
                for (const auto &dep_id : task.dependencies) {
                    auto dep_it = tasks.find(dep_id);
                    if (dep_it == tasks.end() || dep_it->second.status != TaskStatus::COMPLETED) {
                        all_deps_ready = false;
                        break;
                    }
                }
                
                if (all_deps_ready) {
                    ready_tasks.push_back(task.task_id);
                }
            }
        }
        
        // 根据优先级排序
        std::sort(ready_tasks.begin(), ready_tasks.end(), [this](const std::string &a, const std::string &b) {
            const Task &task_a = tasks.at(a);
            const Task &task_b = tasks.at(b);
            return task_a.priority > task_b.priority;
        });
    }
    
    return ready_tasks;
}

bool CTPL_DAG_Scheduler::mark_task_completed(const std::string &task_id, const std::unordered_map<std::string, std::string> &outputs) {
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        
        auto it = tasks.find(task_id);
        if (it == tasks.end()) {
            return false;
        }
        
        it->second.status = TaskStatus::COMPLETED;
        it->second.outputs = outputs;
        
        // 更新依赖此任务的所有任务的输入
        for (const auto &dependent_id : it->second.dependents) {
            auto dep_it = tasks.find(dependent_id);
            if (dep_it != tasks.end()) {
                for (const auto &output : outputs) {
                    dep_it->second.inputs[output.first] = output.second;
                }
            }
        }
    }
    
    return true;
}

bool CTPL_DAG_Scheduler::mark_task_failed(const std::string &task_id) {
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        
        auto it = tasks.find(task_id);
        if (it == tasks.end()) {
            return false;
        }
        
        it->second.status = TaskStatus::FAILED;
        it->second.retries++;
    }
    
    return true;
}

bool CTPL_DAG_Scheduler::has_cycle() {
    std::unordered_map<std::string, int> in_degree;
    std::queue<std::string> zero_in_degree;
    int processed = 0;
    
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        
        // 计算所有节点的入度
        for (const auto &pair : tasks) {
            in_degree[pair.first] = 0;
        }
        
        for (const auto &pair : tasks) {
            for (const auto &dep : pair.second.dependencies) {
                in_degree[pair.first]++;
            }
        }
        
        // 将入度为0的节点加入队列
        for (const auto &pair : in_degree) {
            if (pair.second == 0) {
                zero_in_degree.push(pair.first);
            }
        }
    }
    
    // Kahn算法检测循环
    while (!zero_in_degree.empty()) {
        std::string task_id = zero_in_degree.front();
        zero_in_degree.pop();
        processed++;
        
        { 
            std::unique_lock<std::mutex> lock(tasks_mutex);
            auto it = tasks.find(task_id);
            if (it == tasks.end()) continue;
            
            // 减少所有依赖此任务的节点的入度
            for (const auto &dependent_id : it->second.dependents) {
                if (in_degree[dependent_id] > 0) {
                    in_degree[dependent_id]--;
                    if (in_degree[dependent_id] == 0) {
                        zero_in_degree.push(dependent_id);
                    }
                }
            }
        }
    }
    
    return processed != static_cast<int>(in_degree.size());
}

void CTPL_DAG_Scheduler::clear_all_tasks() {
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        tasks.clear();
    }
}

void CTPL_DAG_Scheduler::set_task_priority(const std::string &task_id, TaskPriority new_priority) {
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        
        auto it = tasks.find(task_id);
        if (it != tasks.end()) {
            it->second.priority = new_priority;
        }
    }
}

bool CTPL_DAG_Scheduler::retry_task(const std::string &task_id) {
    { 
        std::unique_lock<std::mutex> lock(tasks_mutex);
        
        auto it = tasks.find(task_id);
        if (it == tasks.end() || it->second.retries >= it->second.max_retries) {
            return false;
        }
        
        it->second.status = TaskStatus::PENDING;
        it->second.retries++;
        
        // 重置依赖此任务的所有任务为PENDING
        for (const auto &dependent_id : it->second.dependents) {
            auto dep_it = tasks.find(dependent_id);
            if (dep_it != tasks.end()) {
                dep_it->second.status = TaskStatus::PENDING;
            }
        }
    }
    
    return true;
}
