#include "ctpl_stl.h"
#include "ctpl_distributed.h"
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <atomic>

int main(int argc, char **argv) {
    std::cout << "=== CTPL Distributed Thread Pool Example ===\n\n";
    
    // 创建分布式线程池
    ctpl::distributed_thread_pool p(4);
    
    // --------------------------
    // 示例1: DAG任务调度
    // --------------------------
    std::cout << "1. Testing DAG Task Scheduling...\n";
    
    // 创建任务
    auto task_a = p.create_dag_task([](int id) {
        std::cout << "Task A (CPU intensive) running on thread " << id << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }, ctpl::TaskPriority::HIGH, ctpl::TaskType::CPU_INTENSIVE);
    
    auto task_b = p.create_dag_task([](int id) {
        std::cout << "Task B (I/O intensive) running on thread " << id << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }, ctpl::TaskPriority::NORMAL, ctpl::TaskType::IO_INTENSIVE);
    
    auto task_c = p.create_dag_task([](int id) {
        std::cout << "Task C (CPU intensive) running on thread " << id << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }, ctpl::TaskPriority::NORMAL, ctpl::TaskType::CPU_INTENSIVE);
    
    auto task_d = p.create_dag_task([](int id) {
        std::cout << "Task D (Mixed) running on thread " << id << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }, ctpl::TaskPriority::LOW, ctpl::TaskType::MIXED);
    
    // 设置依赖关系: A -> B, A -> C, B -> D, C -> D
    p.add_dependency(task_a, task_b);
    p.add_dependency(task_a, task_c);
    p.add_dependency(task_b, task_d);
    p.add_dependency(task_c, task_d);
    
    // 提交DAG任务
    p.submit_dag();
    
    // 等待DAG任务完成
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 动态修改依赖关系: 移除B->D的依赖
    p.remove_dependency(task_b, task_d);
    
    std::cout << "DAG Task Scheduling test completed.\n\n";
    
    // --------------------------
    // 示例2: 实时数据流处理
    // --------------------------
    std::cout << "2. Testing Real-time Data Stream Processing...\n";
    
    // 创建数据流（2个分片）
    auto stream_id = p.create_stream("test_stream", 2);
    
    // 滑动窗口：每500ms计算过去2000ms的平均值
    std::atomic<int> window_count = 0;
    p.add_window(stream_id, 2000, 500, [&window_count](const std::vector<std::string> &data) {
        int sum = 0;
        for (const auto &item : data) {
            sum += std::stoi(item);
        }
        double avg = data.empty() ? 0 : static_cast<double>(sum) / data.size();
        std::cout << "Window " << ++window_count << ": average = " << avg << " (" << data.size() << " items)\n";
    });
    
    // 滚动窗口：每1000ms计算该分钟的总和
    p.add_window(stream_id, 1000, 1000, [](const std::vector<std::string> &data) {
        int sum = 0;
        for (const auto &item : data) {
            sum += std::stoi(item);
        }
        std::cout << "Rolling window: sum = " << sum << "\n";
    });
    
    // 模拟数据流输入
    for (int i = 0; i < 20; ++i) {
        std::string data = std::to_string(i * 10);
        p.push_stream_data(stream_id, data);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 等待数据流处理完成
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cout << "Real-time Data Stream Processing test completed.\n\n";
    
    // --------------------------
    // 示例3: 自适应资源调度
    // --------------------------
    std::cout << "3. Testing Adaptive Resource Scheduling...\n";
    
    // 启用弹性伸缩
    p.enable_elastic_scaling(true);
    p.set_cpu_threshold(70.0);
    p.set_memory_threshold(80.0);
    
    // 提交大量任务，模拟高负载
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 50; ++i) {
        futures.emplace_back(p.push([i](int id) {
            std::cout << "Task " << i << " running on thread " << id << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }));
    }
    
    // 等待任务完成
    for (auto &f : futures) {
        f.get();
    }
    
    std::cout << "Adaptive Resource Scheduling test completed.\n\n";
    
    // --------------------------
    // 示例4: 多租户管理
    // --------------------------
    std::cout << "4. Testing Multi-tenant Management...\n";
    
    // 创建两个租户
    auto tenant_a = p.create_tenant("tenant_a", 2, 100);  // 最多2个线程，队列100
    auto tenant_b = p.create_tenant("tenant_b", 3, 150);  // 最多3个线程，队列150
    
    // 为租户分配资源
    p.allocate_tenant_resource(tenant_a, 1, 50);
    p.allocate_tenant_resource(tenant_b, 2, 100);
    
    std::cout << "Multi-tenant Management test completed.\n\n";
    
    // --------------------------
    // 示例5: 容错机制
    // --------------------------
    std::cout << "5. Testing Fault Tolerance...\n";
    
    // 启用持久化
    p.enable_persistence(true);
    p.set_checkpoint_interval(1000);
    
    // 创建一个可能失败的任务
    auto fault_task = p.create_dag_task([](int id) {
        std::cout << "Fault-prone task running on thread " << id << "\n";
        throw std::runtime_error("Simulated task failure");
    });
    
    // 提交任务
    p.submit_dag();
    
    // 等待任务重试
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    std::cout << "Fault Tolerance test completed.\n\n";
    
    // --------------------------
    // 示例6: 原有CTPL接口兼容性
    // --------------------------
    std::cout << "6. Testing CTPL API Compatibility...\n";
    
    // 使用原有push接口
    auto f = p.push([](int id) {
        return "Hello from CTPL compatible interface!";
    });
    
    std::cout << "CTPL compatible interface returned: " << f.get() << "\n";
    
    std::cout << "CTPL API Compatibility test completed.\n\n";
    
    // 停止线程池
    p.stop(true);
    
    std::cout << "=== All Tests Completed ===\n";
    
    return 0;
}
