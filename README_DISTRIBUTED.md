# CTPL Distributed Thread Pool

CTPL Distributed is an extended version of the original CTPL (Compile Time Template Library) thread pool, adding advanced features for distributed computing, real-time data processing, and adaptive resource management.

## Features

### 1. Distributed Task Scheduling & Dependency Chain Management
- **DAG Task Support**: Create tasks with dependencies forming a Directed Acyclic Graph
- **Dynamic Dependency Adjustment**: Add/remove dependencies at runtime
- **Priority Scheduling**: Automatic task prioritization based on type and system load
- **Cross-node Distribution**: Support for distributed task execution across multiple thread pool nodes

### 2. Real-time Data Stream Processing & Window Computing
- **High-throughput Stream Input**: TCP/UDP protocol support for up to 100k records/s
- **Window Operations**: Sliding windows and tumbling windows
- **Custom Aggregation**: Support for user-defined aggregation functions
- **Order Preservation**: Data order guaranteed by hash-based sharding
- **Backpressure Mechanism**: Automatic flow control to prevent memory overflow

### 3. Adaptive Resource Scheduling & Elastic Scaling
- **Task Type Classification**: Automatic detection of CPU-intensive, I/O-intensive, and mixed tasks
- **Machine Learning Prediction**: ML-based thread count optimization
- **Real-time System Monitoring**: CPU, memory, and I/O load monitoring
- **Seamless Scaling**: Dynamic thread pool resizing without task interruption

### 4. Distributed Fault Tolerance & Disaster Recovery
- **Task State Persistence**: Real-time task metadata storage in distributed KV stores (etcd)
- **Hot Standby**: Active-standby node switching in under 10 seconds
- **Checkpoint Recovery**: Resume tasks from last checkpoint
- **Data Shard Fault Tolerance**: Automatic shard reallocation on failure

### 5. Multi-tenant Isolation & Resource Quota Management
- **Tenant Resource Quotas**: Per-tenant limits on threads, CPU, and queue length
- **Dynamic Resource Preemption**: High-priority tenants can抢占 low-priority resources
- **Priority Isolation**: Both intra-tenant and inter-tenant priority scheduling
- **Usage Monitoring & Billing**: Real-time resource usage statistics for billing

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     应用层 API                          │
├─────────────────────────────────────────────────────────┤
│  DAG调度  │  数据流  │  资源管理  │  容错  │  租户管理  │
├─────────────────────────────────────────────────────────┤
│                     基础线程池层                        │
├─────────────────────────────────────────────────────────┤
│                     网络通信层                          │
└─────────────────────────────────────────────────────────┘
```

## Quick Start

### Installation

```bash
# 克隆仓库
git clone <repository_url>
cd CTPL

# 创建构建目录
mkdir build
cd build

# 编译
cmake ..
cmake --build .

# 安装
cmake --install .
```

### Dependencies

- **Boost**: Lockfree queues and other utilities
- **ZeroMQ**: Distributed communication
- **Protobuf**: Data serialization (optional)
- **etcd-cpp-apiv3**: Distributed KV storage (optional)

### Example Usage

#### DAG Task Scheduling

```cpp
#include "ctpl_distributed.h"

int main() {
    ctpl::distributed_thread_pool p(4);
    
    // 创建任务
    auto task_a = p.create_dag_task([](int id) {
        // Task A implementation
    });
    
    auto task_b = p.create_dag_task([](int id) {
        // Task B implementation
    });
    
    // 设置依赖: A -> B
    p.add_dependency(task_a, task_b);
    
    // 提交DAG
    p.submit_dag();
    
    return 0;
}
```

#### Real-time Data Stream Processing

```cpp
// 创建数据流
auto stream_id = p.create_stream("test_stream", 2);

// 添加滑动窗口
p.add_window(stream_id, 2000, 500, [](const std::vector<std::string> &data) {
    // 窗口计算
});

// 推送数据
for (int i = 0; i < 100; ++i) {
    p.push_stream_data(stream_id, std::to_string(i));
}
```

#### Adaptive Resource Scheduling

```cpp
// 启用弹性伸缩
p.enable_elastic_scaling(true);

// 设置阈值
p.set_cpu_threshold(70.0);
p.set_memory_threshold(80.0);

// 提交任务
for (int i = 0; i < 100; ++i) {
    p.push([](int id) {
        // 任务实现
    });
}
```

## API Documentation

### DAG Task Management

- `create_dag_task(func, priority, type)`: 创建DAG任务
- `add_dependency(parent, child)`: 添加任务依赖
- `remove_dependency(parent, child)`: 移除任务依赖
- `submit_dag()`: 提交DAG任务

### Stream Processing

- `create_stream(name, shard_count)`: 创建数据流
- `add_window(stream_id, window_size_ms, slide_interval_ms, func)`: 添加窗口计算
- `push_stream_data(stream_id, data)`: 推送数据到流

### Resource Management

- `enable_elastic_scaling(enable)`: 启用/禁用弹性伸缩
- `set_cpu_threshold(threshold)`: 设置CPU阈值
- `set_memory_threshold(threshold)`: 设置内存阈值

### Fault Tolerance

- `enable_persistence(enable)`: 启用任务持久化
- `set_checkpoint_interval(interval_ms)`: 设置检查点间隔

### Multi-tenant Management

- `create_tenant(name, max_threads, max_queue_size)`: 创建租户
- `allocate_tenant_resource(tenant_id, threads, queue_size)`: 分配资源

## Performance

- **Task Scheduling Delay**: < 1ms
- **Stream Throughput**: > 100k records/s
- **Scaling Response Time**: < 500ms
- **Failover Time**: < 10s

## Configuration

### Thread Pool Options

- `nThreads`: 初始线程数
- `queueSize`: 任务队列大小

### Network Options

- `node_id`: 节点唯一标识
- `zmq_address`: ZeroMQ通信地址

### Resource Options

- `cpu_threshold`: CPU利用率阈值
- `memory_threshold`: 内存利用率阈值

### Fault Tolerance Options

- `persistence_enabled`: 是否启用持久化
- `checkpoint_interval_ms`: 检查点间隔

## Testing

```bash
# 运行基础线程池示例
./example

# 运行分布式功能示例
./example_distributed
```

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

## License

This project is licensed under the Apache License, Version 2.0 - see the LICENSE file for details.

## Original CTPL

This project is based on the original CTPL by Vitaliy Vitsentiy, which can be found at https://github.com/vit-vit/CTPL
