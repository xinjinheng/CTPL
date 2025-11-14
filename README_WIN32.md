# Windows环境下CTPL分布式线程池库使用指南

## 目录
- [1. 环境要求](#1-环境要求)
- [2. 安装依赖](#2-安装依赖)
- [3. 编译示例](#3-编译示例)
- [4. 使用库](#4-使用库)
- [5. 功能说明](#5-功能说明)

## 1. 环境要求

- **操作系统**: Windows 10/11
- **编译器**: GCC (MinGW-w64)
- **C++标准**: C++17
- **依赖库**: Boost Lockfree (仅分布式版本需要)

## 2. 安装依赖

### 2.1 安装MinGW-w64

1. 下载MinGW-w64安装包:
   - 官方网站: https://www.mingw-w64.org/downloads/
   - 推荐下载: mingw-w64-install.exe

2. 安装步骤:
   - 运行安装程序
   - 设置架构: x86_64
   - 设置线程模型: posix
   - 设置异常模型: seh
   - 安装路径: C:\mingw64
   - 完成安装

3. 配置环境变量:
   - 将C:\mingw64\bin添加到系统PATH环境变量
   - 验证安装: 打开命令行输入`gcc --version`，如果显示版本信息则安装成功

### 2.2 安装Boost库

1. 下载Boost库:
   - 官方网站: https://www.boost.org/
   - 推荐版本: 1.78.0 (稳定版本)
   - 下载zip压缩包: boost_1_78_0.zip

2. 解压Boost库:
   - 将下载的zip文件解压到C:\目录下
   - 解压后路径: C:\boost_1_78_0

3. 注意:
   - CTPL仅需要Boost Lockfree组件，无需编译整个Boost库
   - 只需将Boost头文件路径添加到编译命令即可

## 3. 编译示例

### 3.1 使用批处理脚本编译

1. 打开命令行并进入CTPL目录:
   ```bash
   cd d:\share\part-time\aicode\CTPL
   ```

2. 运行编译脚本:
   ```bash
   compile_example.bat
   ```

3. 脚本会自动:
   - 检查GCC是否安装
   - 编译基础示例 (example.cpp)
   - 检查Boost库是否存在
   - 编译分布式示例 (example_distributed.cpp)

### 3.2 手动编译

#### 编译基础示例

```bash
gcc -std=c++17 -o example.exe example.cpp
```

#### 编译分布式示例

```bash
gcc -std=c++17 -IC:\boost_1_78_0 -o example_distributed.exe example_distributed.cpp -lstdc++fs -lpthread
```

参数说明:
- `-std=c++17`: 使用C++17标准
- `-IC:\boost_1_78_0`: 指定Boost头文件路径
- `-lstdc++fs`: 链接文件系统库
- `-lpthread`: 链接线程库

## 4. 使用库

### 4.1 基础线程池使用

```cpp
#include "ctpl.h"
#include <iostream>

int main() {
    ctpl::thread_pool pool(4);  // 创建4个线程的线程池
    
    // 提交任务
    auto future = pool.push([](int id) {
        return id * id;
    });
    
    int result = future.get();  // 等待任务完成并获取结果
    std::cout << "Result: " << result << std::endl;
    
    return 0;
}
```

### 4.2 分布式线程池使用

```cpp
#include "ctpl_distributed.h"
#include <iostream>

int main() {
    ctpl::distributed_thread_pool pool(4);  // 创建4个线程的分布式线程池
    
    // 创建DAG任务
    std::string task1 = pool.create_dag_task([](int id) {
        std::cout << "Task 1 executed by thread " << id << std::endl;
    });
    
    std::string task2 = pool.create_dag_task([](int id) {
        std::cout << "Task 2 executed by thread " << id << std::endl;
    });
    
    // 设置依赖关系: task1完成后执行task2
    pool.add_dependency(task1, task2);
    
    // 提交DAG执行
    pool.submit_dag();
    
    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    return 0;
}
```

## 5. 功能说明

CTPL分布式线程池扩展了原有CTPL的功能，主要包括:

1. **DAG任务调度**: 支持任务依赖关系和动态调整
2. **实时数据流处理**: 支持滑动窗口、滚动窗口等计算
3. **自适应资源调度**: 基于负载动态调整线程数量
4. **分布式任务容错**: 支持任务状态持久化和故障恢复
5. **多租户管理**: 支持资源隔离和配额管控

所有新功能都保持了与原有CTPL接口的兼容性，方便用户迁移。