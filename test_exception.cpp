#include <ctpl.h>
#include <iostream>
#include <string>
#include <chrono>
#include <stdexcept>

// 空指针异常测试
void null_ptr_test(int id) {
    std::cout << "Thread " << id << " running null_ptr_test\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 模拟空指针异常
    int* p = nullptr;
    *p = 42;
}

// 网络超时异常测试
void network_timeout_test(int id) {
    std::cout << "Thread " << id << " running network_timeout_test\n";
    
    // 模拟长时间网络操作
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // 模拟网络超时异常
    throw std::runtime_error("Network timeout");
}

// 普通任务测试
void normal_task(int id) {
    std::cout << "Thread " << id << " running normal_task\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int main(int argc, char **argv) {
    // 创建线程池并配置
    ctpl::thread_pool::config cfg;
    cfg.enable_null_ptr_protection = true;
    cfg.max_retry_count = 2;
    cfg.retry_interval = std::chrono::milliseconds(500);
    cfg.enable_health_monitoring = true;
    
    ctpl::thread_pool p(2, cfg);
    
    // 提交空指针异常任务
    std::cout << "Submitting null_ptr_test\n";
    auto f1 = p.push(null_ptr_test);
    
    // 提交网络超时任务
    std::cout << "Submitting network_timeout_test\n";
    auto f2 = p.push(network_timeout_test);
    
    // 提交普通任务
    for (int i = 0; i < 5; ++i) {
        p.push(normal_task);
    }
    
    // 等待任务完成
    std::cout << "Waiting for tasks to complete\n";
    
    try {
        f1.get();
    } catch (const std::exception &e) {
        std::cout << "Caught exception from null_ptr_test: " << e.what() << std::endl;
    }
    
    try {
        f2.get();
    } catch (const std::exception &e) {
        std::cout << "Caught exception from network_timeout_test: " << e.what() << std::endl;
    }
    
    // 输出健康统计信息
    auto stats = p.get_health_stats();
    std::cout << "\nHealth Statistics:\n";
    std::cout << "Total tasks: " << stats.total_tasks << std::endl;
    std::cout << "Successful tasks: " << stats.successful_tasks << std::endl;
    std::cout << "Failed tasks: " << stats.failed_tasks << std::endl;
    std::cout << "Null pointer exceptions: " << stats.null_ptr_exceptions << std::endl;
    std::cout << "Timeout exceptions: " << stats.timeout_exceptions << std::endl;
    std::cout << "Deadlock exceptions: " << stats.deadlock_exceptions << std::endl;
    
    return 0;
}
