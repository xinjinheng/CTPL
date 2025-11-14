#ifndef __ctpl_stream_processing_H__
#define __ctpl_stream_processing_H__

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>

// 窗口类型枚举
enum class WindowType {
    TUMBLING,
    SLIDING
};

// 窗口配置结构体
struct WindowConfig {
    std::string stream_id;
    WindowType type;
    int size_ms;
    int slide_ms;
    int max_delay_ms;
};

// 数据流元数据结构体
struct StreamMetadata {
    std::string stream_id;
    std::string data_type;
    int partition_count;
    bool ordered;
    bool at_least_once;
    bool exactly_once;
    std::function<void(const std::string &stream_id, const std::string &data)> on_data;
    std::function<void(const std::string &stream_id, const std::string &window_id, const std::vector<std::string> &data)> on_window;
};

// 数据流数据结构体
struct StreamData {
    std::string stream_id;
    std::string partition_id;
    std::string data;
    uint64_t timestamp;
    std::string key;
};

// 数据流处理模块类
class CTPL_Stream_Processor {
public:
    CTPL_Stream_Processor();
    ~CTPL_Stream_Processor();
    
    // 数据流创建与管理
    bool create_stream(const std::string &stream_id, const std::string &data_type, int partition_count, bool ordered = true);
    
    bool add_window(const std::string &stream_id, WindowType type, int size_ms, int slide_ms = 0, int max_delay_ms = 5000);
    
    bool push_stream_data(const std::string &stream_id, const std::string &partition_id, const std::string &data, uint64_t timestamp = 0, const std::string &key = "");
    
    bool register_data_callback(const std::string &stream_id, std::function<void(const std::string &stream_id, const std::string &data)> callback);
    
    bool register_window_callback(const std::string &stream_id, std::function<void(const std::string &stream_id, const std::string &window_id, const std::vector<std::string> &data)> callback);
    
    // 多租户流隔离
    bool create_tenant_stream(const std::string &tenant_id, const std::string &stream_id, const std::string &data_type, int partition_count, bool ordered = true);
    
    bool push_tenant_stream_data(const std::string &tenant_id, const std::string &stream_id, const std::string &partition_id, const std::string &data, uint64_t timestamp = 0, const std::string &key = "");
    
private:
    // 窗口处理辅助函数
    void process_windows(const std::string &stream_id, const StreamData &data);
    
    std::unordered_map<std::string, StreamMetadata> streams;
    std::unordered_map<std::string, std::vector<WindowConfig>> stream_windows;
    std::unordered_map<std::string, std::unordered_map<std::string, std::queue<StreamData>>> stream_buffers;
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<StreamData>>> window_buffers;
    
    // 多租户数据流
    std::unordered_map<std::string, std::unordered_map<std::string, StreamMetadata>> tenant_streams;
    
    std::mutex streams_mutex;
    std::atomic<int> window_counter;
};

#endif // __ctpl_stream_processing_H__