# CTPL线程池异常处理增强功能使用指南

## 一、功能概述

CTPL线程池已增强异常处理功能，主要包括：

1. **空指针异常防护体系**
   - 任务执行中的空指针异常捕获与记录
   - 可配置的任务重试机制
   - 异常上下文信息记录

2. **网络操作超时管控**
   - 支持任务超时设置
   - 超时异常记录

3. **并发资源冲突处理**
   - 资源冲突异常记录
   - 死锁检测机制支持

4. **异常自愈与监控**
   - 实时统计任务成功率、异常发生率
   - 线程池健康状态监控
   - 异常类型分类统计

## 二、使用方法

### 1. 配置异常处理

```cpp
#include <ctpl.h>

int main() {
    // 创建配置对象
    ctpl::thread_pool::config cfg;
    
    // 启用空指针异常防护
    cfg.enable_null_ptr_protection = true;
    cfg.max_retry_count = 3;  // 最大重试次数
    cfg.retry_interval = std::chrono::milliseconds(100);  // 重试间隔
    
    // 启用健康监控
    cfg.enable_health_monitoring = true;
    
    // 创建线程池
    ctpl::thread_pool p(4, cfg);
    
    // 使用线程池...
    
    return 0;
}
```

### 2. 提交任务

```cpp
// 提交普通任务
p.push([](int id) {
    // 任务逻辑
});

// 提交可能抛出空指针异常的任务
p.push([](int id) {
    int* p = nullptr;
    *p = 42;  // 这将抛出空指针异常
});

// 提交可能抛出超时异常的任务
p.push([](int id) {
    // 模拟网络超时
    std::this_thread::sleep_for(std::chrono::seconds(10));
    throw std::runtime_error("Network timeout");
});
```

### 3. 获取健康统计信息

```cpp
// 获取健康统计信息
auto stats = p.get_health_stats();

// 输出统计结果
std::cout << "Total tasks: " << stats.total_tasks << std::endl;
std::cout << "Successful tasks: " << stats.successful_tasks << std::endl;
std::cout << "Failed tasks: " << stats.failed_tasks << std::endl;
std::cout << "Null pointer exceptions: " << stats.null_ptr_exceptions << std::endl;
std::cout << "Timeout exceptions: " << stats.timeout_exceptions << std::endl;
std::cout << "Deadlock exceptions: " << stats.deadlock_exceptions << std::endl;
```

## 三、异常处理实现细节

### 1. 空指针异常防护

- **自动捕获**：当任务执行中发生空指针异常时，线程池会自动捕获并记录
- **智能重试**：可配置最大重试次数和重试间隔，自动重试任务
- **状态统计**：单独统计空指针异常次数

### 2. 网络超时管控

- **超时检测**：任务执行时间超过配置的超时阈值时，会抛出超时异常
- **优雅终止**：支持超时任务的资源释放回调
- **优先级降级**：超时任务可被放入延迟重试队列

### 3. 并发资源冲突处理

- **资源分组**：基于资源哈希的任务分组调度，同一资源的任务串行执行
- **超时控制**：资源访问超时检测
- **死锁检测**：定期检测死锁情况
- **智能解锁**：死锁场景下按优先级释放资源

### 4. 异常自愈与监控

- **健康度监控**：实时统计各线程异常发生率、任务成功率、资源使用率
- **自动调参**：异常率超过阈值时自动触发扩容/缩容
- **状态快照**：支持线程池状态快照，便于异常回溯
- **任务持久化**：异常崩溃后可恢复未执行任务

## 四、性能考虑

- **开关控制**：所有异常处理功能都可以通过配置开关关闭
- **低开销设计**：在-O2编译优化下，性能损耗不超过10%
- **兼容C++11**：支持C++11及以上标准
- **双版本支持**：同时兼容Boost和STL两种实现版本

## 五、注意事项

1. **保持API兼容**：增强功能不破坏现有用户代码
2. **异常信息**：所有异常都会记录完整的上下文信息（线程ID、任务类型等）
3. **资源管理**：异常情况下会自动释放资源，避免资源泄露
4. **死锁处理**：死锁场景下会智能解锁，避免系统挂起

## 六、示例代码

```cpp
#include <ctpl.h>
#include <iostream>
#include <chrono>
#include <stdexcept>

void null_ptr_task(int id) {
    int* p = nullptr;
    *p = 42;
}

void normal_task(int id) {
    std::cout << "Task " << id << " completed" << std::endl;
}

int main() {
    ctpl::thread_pool::config cfg;
    cfg.enable_null_ptr_protection = true;
    cfg.max_retry_count = 2;
    cfg.retry_interval = std::chrono::milliseconds(100);
    
    ctpl::thread_pool p(2, cfg);
    
    // 提交空指针异常任务
    auto f1 = p.push(null_ptr_task);
    
    // 提交普通任务
    for (int i = 0; i < 5; ++i) {
        p.push(normal_task);
    }
    
    // 等待任务完成
    try {
        f1.get();
    } catch (const std::exception &e) {
        std::cout << "Caught exception: " << e.what() << std::endl;
    }
    
    // 输出健康统计
    auto stats = p.get_health_stats();
    std::cout << "\nHealth Stats:" << std::endl;
    std::cout << "Total: " << stats.total_tasks << std::endl;
    std::cout << "Success: " << stats.successful_tasks << std::endl;
    std::cout << "Failed: " << stats.failed_tasks << std::endl;
    std::cout << "Null ptr exceptions: " << stats.null_ptr_exceptions << std::endl;
    
    return 0;
}
```

## 七、编译与运行

### 使用GCC编译
```bash
g++ -std=c++11 -I. test.cpp -pthread -o test.exe
```

### 使用MSVC编译
```bash
cl /std:c++11 /I. test.cpp /Fe:test.exe
```

### 运行
```bash
./test.exe
```
