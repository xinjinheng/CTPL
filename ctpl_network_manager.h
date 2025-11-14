#ifndef __ctpl_network_manager_H__
#define __ctpl_network_manager_H__

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>

// 网络消息类型枚举
enum class NetworkMessageType {
    TASK_SUBMIT,
    TASK_COMPLETE,
    TASK_FAILED,
    TASK_MIGRATE,
    RESOURCE_REQUEST,
    RESOURCE_RESPONSE,
    CHECKPOINT_SYNC,
    HEARTBEAT
};

// 网络消息结构体
struct NetworkMessage {
    NetworkMessageType type;
    std::string source_ip;
    int source_port;
    std::string destination_ip;
    int destination_port;
    std::string payload;
    uint64_t timestamp;
    std::string message_id;
};

// 网络节点信息结构体
struct NetworkNode {
    std::string node_id;
    std::string ip_address;
    int port;
    int available_cpus;
    int available_gpus;
    size_t available_memory;
    int load_factor;
    bool active;
    uint64_t last_heartbeat;
};

// 网络通信模块类
class CTPL_Network_Manager {
public:
    CTPL_Network_Manager();
    ~CTPL_Network_Manager();
    
    // 初始化与配置
    bool init(const std::string &node_id, const std::string &ip_address, int port, const std::vector<std::string> &initial_nodes);
    
    bool set_timeout(int timeout_ms);
    
    // 节点管理
    bool register_node(const NetworkNode &node);
    
    bool unregister_node(const std::string &node_id);
    
    bool update_node_status(const std::string &node_id, const NetworkNode &node);
    
    std::vector<NetworkNode> get_active_nodes();
    
    // 消息传递
    bool send_message(const std::string &destination_node_id, NetworkMessageType type, const std::string &payload);
    
    bool send_message_to_all(NetworkMessageType type, const std::string &payload);
    
    bool register_message_handler(std::function<void(const NetworkMessage &)> handler);
    
    // 负载均衡
    bool enable_load_balancing(int strategy = 0);
    
    std::string select_node_for_task(int task_type = 0);
    
    // 心跳机制
    bool enable_heartbeat(int interval_ms = 5000, int timeout_ms = 15000);
    
private:
    // 网络线程函数
    void network_thread();
    
    // 心跳线程函数
    void heartbeat_thread();
    
    // 消息处理线程函数
    void message_handler_thread();
    
    // 负载均衡策略
    std::function<std::string(const std::vector<NetworkNode> &, int)> load_balancing_strategy;
    
    std::string node_id;
    std::string ip_address;
    int port;
    
    std::unordered_map<std::string, NetworkNode> nodes;
    std::mutex nodes_mutex;
    
    std::queue<NetworkMessage> incoming_messages;
    std::queue<NetworkMessage> outgoing_messages;
    
    std::mutex incoming_mutex;
    std::mutex outgoing_mutex;
    
    std::condition_variable incoming_cv;
    std::condition_variable outgoing_cv;
    
    std::function<void(const NetworkMessage &)> message_handler;
    
    std::atomic<bool> is_initialized;
    std::atomic<bool> is_running;
    std::atomic<bool> is_heartbeat_enabled;
    
    int heartbeat_interval;
    int heartbeat_timeout;
    
    std::unique_ptr<std::thread> network_thread_ptr;
    std::unique_ptr<std::thread> heartbeat_thread_ptr;
    std::unique_ptr<std::thread> message_handler_thread_ptr;
};

#endif // __ctpl_network_manager_H__