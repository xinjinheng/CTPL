#include "ctpl_stream_processing.h"
#include <sstream>
#include <chrono>

CTPL_Stream_Processor::CTPL_Stream_Processor() : window_counter(0) {
}

CTPL_Stream_Processor::~CTPL_Stream_Processor() {
}

bool CTPL_Stream_Processor::create_stream(const std::string &stream_id, const std::string &data_type, int partition_count, bool ordered) {
    { 
        std::unique_lock<std::mutex> lock(streams_mutex);
        
        if (streams.find(stream_id) != streams.end()) {
            return false;
        }
        
        StreamMetadata metadata;
        metadata.stream_id = stream_id;
        metadata.data_type = data_type;
        metadata.partition_count = partition_count;
        metadata.ordered = ordered;
        metadata.at_least_once = true;
        metadata.exactly_once = false;
        
        streams[stream_id] = metadata;
    }
    
    return true;
}

bool CTPL_Stream_Processor::add_window(const std::string &stream_id, WindowType type, int size_ms, int slide_ms, int max_delay_ms) {
    { 
        std::unique_lock<std::mutex> lock(streams_mutex);
        
        if (streams.find(stream_id) == streams.end()) {
            return false;
        }
        
        WindowConfig config;
        config.stream_id = stream_id;
        config.type = type;
        config.size_ms = size_ms;
        config.slide_ms = (type == WindowType::SLIDING && slide_ms == 0) ? size_ms / 2 : slide_ms;
        config.max_delay_ms = max_delay_ms;
        
        stream_windows[stream_id].push_back(config);
    }
    
    return true;
}

bool CTPL_Stream_Processor::push_stream_data(const std::string &stream_id, const std::string &partition_id, const std::string &data, uint64_t timestamp, const std::string &key) {
    if (timestamp == 0) {
        timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
    }
    
    StreamData stream_data;
    stream_data.stream_id = stream_id;
    stream_data.partition_id = partition_id;
    stream_data.data = data;
    stream_data.timestamp = timestamp;
    stream_data.key = key;
    
    { 
        std::unique_lock<std::mutex> lock(streams_mutex);
        
        auto stream_it = streams.find(stream_id);
        if (stream_it == streams.end()) {
            return false;
        }
        
        // 调用数据回调
        if (stream_it->second.on_data) {
            stream_it->second.on_data(stream_id, data);
        }
        
        // 处理窗口
        process_windows(stream_id, stream_data);
        
        // 保存到流缓冲区
        stream_buffers[stream_id][partition_id].push(stream_data);
    }
    
    return true;
}

bool CTPL_Stream_Processor::register_data_callback(const std::string &stream_id, std::function<void(const std::string &stream_id, const std::string &data)> callback) {
    { 
        std::unique_lock<std::mutex> lock(streams_mutex);
        
        auto it = streams.find(stream_id);
        if (it == streams.end()) {
            return false;
        }
        
        it->second.on_data = callback;
    }
    
    return true;
}

bool CTPL_Stream_Processor::register_window_callback(const std::string &stream_id, std::function<void(const std::string &stream_id, const std::string &window_id, const std::vector<std::string> &data)> callback) {
    { 
        std::unique_lock<std::mutex> lock(streams_mutex);
        
        auto it = streams.find(stream_id);
        if (it == streams.end()) {
            return false;
        }
        
        it->second.on_window = callback;
    }
    
    return true;
}

void CTPL_Stream_Processor::process_windows(const std::string &stream_id, const StreamData &data) {
    auto window_configs_it = stream_windows.find(stream_id);
    if (window_configs_it == stream_windows.end()) {
        return;
    }
    
    auto stream_it = streams.find(stream_id);
    if (stream_it == streams.end()) {
        return;
    }
    
    for (const auto &config : window_configs_it->second) {
        uint64_t window_start = (data.timestamp / config.size_ms) * config.size_ms;
        uint64_t window_end = window_start + config.size_ms;
        
        std::stringstream window_ss;
        window_ss << window_start << "_" << window_end;
        std::string window_id = window_ss.str();
        
        // 保存数据到窗口缓冲区
        window_buffers[stream_id][window_id].push_back(data.data);
        
        // 检查窗口是否需要触发
        uint64_t current_time = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
        if (current_time >= window_end) {
            // 触发窗口回调
            if (stream_it->second.on_window) {
                const auto &window_data = window_buffers[stream_id][window_id];
                stream_it->second.on_window(stream_id, window_id, window_data);
            }
            
            // 清除窗口数据
            window_buffers[stream_id].erase(window_id);
        }
    }
}

bool CTPL_Stream_Processor::create_tenant_stream(const std::string &tenant_id, const std::string &stream_id, const std::string &data_type, int partition_count, bool ordered) {
    { 
        std::unique_lock<std::mutex> lock(streams_mutex);
        
        auto tenant_it = tenant_streams.find(tenant_id);
        if (tenant_it != tenant_streams.end()) {
            if (tenant_it->second.find(stream_id) != tenant_it->second.end()) {
                return false;
            }
        }
        
        StreamMetadata metadata;
        metadata.stream_id = stream_id;
        metadata.data_type = data_type;
        metadata.partition_count = partition_count;
        metadata.ordered = ordered;
        metadata.at_least_once = true;
        metadata.exactly_once = false;
        
        tenant_streams[tenant_id][stream_id] = metadata;
    }
    
    return true;
}

bool CTPL_Stream_Processor::push_tenant_stream_data(const std::string &tenant_id, const std::string &stream_id, const std::string &partition_id, const std::string &data, uint64_t timestamp, const std::string &key) {
    if (timestamp == 0) {
        timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
    }
    
    { 
        std::unique_lock<std::mutex> lock(streams_mutex);
        
        auto tenant_it = tenant_streams.find(tenant_id);
        if (tenant_it == tenant_streams.end()) {
            return false;
        }
        
        auto stream_it = tenant_it->second.find(stream_id);
        if (stream_it == tenant_it->second.end()) {
            return false;
        }
        
        // 调用数据回调
        if (stream_it->second.on_data) {
            stream_it->second.on_data(stream_id, data);
        }
    }
    
    return true;
}
