#include "ctpl_network_manager.h"
#include <chrono>
#include <thread>
#include <sstream>
#include <random>

CTPL_Network_Manager::CTPL_Network_Manager() 
    : is_initialized(false), is_running(false), is_heartbeat_enabled(false),
      port(0), heartbeat_interval(5000), heartbeat_timeout(15000) {
}

CTPL_Network_Manager::~CTPL_Network_Manager() {
    if (is_running) {
        is_running = false;
        
        if (network_thread_ptr && network_thread_ptr->joinable()) {
            network_thread_ptr->join();
        }
        
        if (heartbeat_thread_ptr && heartbeat_thread_ptr->joinable()) {
            heartbeat_thread_ptr->join();
        }
        
        if (message_handler_thread_ptr && message_handler_thread_ptr->joinable()) {
            message_handler_thread_ptr->join();
        }
    }
}

bool CTPL_Network_Manager::init(const std::string &node_id, const std::string &ip_address, int port, const std::vector<std::string> &initial_nodes) {
    if (is_initialized) {
        return false;
    }
    
    this->node_id = node_id;
    this->ip_address = ip_address;
    this->port = port;
    
    // 注册本地节点
    NetworkNode local_node;
    local_node.node_id = node_id;
    local_node.ip_address = ip_address;
    local_node.port = port;
    local_node.available_cpus = 4;
    local_node.available_gpus = 0;
    local_node.available_memory = 8 * 1024 * 1024 * 1024;
    local_node.load_factor = 0;
    local_node.active = true;
    local_node.last_heartbeat = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
    
    { 
        std::unique_lock<std::mutex> lock(nodes_mutex);
        nodes[node_id] = local_node;
    }
    
    // 添加初始节点
    for (const auto &node_str : initial_nodes) {
        // 简单解析节点字符串格式: node_id@ip:port
        size_t at_pos = node_str.find('@');
        size_t colon_pos = node_str.find(':');
        
        if (at_pos != std::string::npos && colon_pos != std::string::npos) {
            std::string id = node_str.substr(0, at_pos);
            std::string ip = node_str.substr(at_pos + 1, colon_pos - at_pos - 1);
            int p = std::stoi(node_str.substr(colon_pos + 1));
            
            NetworkNode node;
            node.node_id = id;
            node.ip_address = ip;
            node.port = p;
            node.available_cpus = 4;
            node.available_gpus = 0;
            node.available_memory = 8 * 1024 * 1024 * 1024;
            node.load_factor = 0;
            node.active = true;
            node.last_heartbeat = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
            
            { 
                std::unique_lock<std::mutex> lock(nodes_mutex);
                nodes[id] = node;
            }
        }
    }
    
    // 默认负载均衡策略: 随机选择
    load_balancing_strategy = [](const std::vector<NetworkNode> &nodes, int task_type) -> std::string {
        if (nodes.empty()) {
            return "";
        }
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, nodes.size() - 1);
        return nodes[dis(gen)].node_id;
    };
    
    is_initialized = true;
    is_running = true;
    
    // 启动网络线程
    network_thread_ptr = std::make_unique<std::thread>(&CTPL_Network_Manager::network_thread, this);
    
    // 启动消息处理线程
    message_handler_thread_ptr = std::make_unique<std::thread>(&CTPL_Network_Manager::message_handler_thread, this);
    
    return true;
}

bool CTPL_Network_Manager::set_timeout(int timeout_ms) {
    // 这里可以添加超时设置逻辑
    return true;
}

bool CTPL_Network_Manager::register_node(const NetworkNode &node) {
    { 
        std::unique_lock<std::mutex> lock(nodes_mutex);
        nodes[node.node_id] = node;
    }
    
    return true;
}

bool CTPL_Network_Manager::unregister_node(const std::string &node_id) {
    { 
        std::unique_lock<std::mutex> lock(nodes_mutex);
        nodes.erase(node_id);
    }
    
    return true;
}

bool CTPL_Network_Manager::update_node_status(const std::string &node_id, const NetworkNode &node) {
    { 
        std::unique_lock<std::mutex> lock(nodes_mutex);
        auto it = nodes.find(node_id);
        if (it != nodes.end()) {
            it->second = node;
            return true;
        }
    }
    
    return false;
}

std::vector<NetworkNode> CTPL_Network_Manager::get_active_nodes() {
    std::vector<NetworkNode> active_nodes;
    uint64_t current_time = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
    
    { 
        std::unique_lock<std::mutex> lock(nodes_mutex);
        
        for (const auto &pair : nodes) {
            if (pair.second.active && (current_time - pair.second.last_heartbeat) < heartbeat_timeout) {
                active_nodes.push_back(pair.second);
            }
        }
    }
    
    return active_nodes;
}

bool CTPL_Network_Manager::send_message(const std::string &destination_node_id, NetworkMessageType type, const std::string &payload) {
    NetworkNode dest_node;
    
    { 
        std::unique_lock<std::mutex> lock(nodes_mutex);
        auto it = nodes.find(destination_node_id);
        if (it == nodes.end()) {
            return false;
        }
        dest_node = it->second;
    }
    
    uint64_t timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
    
    NetworkMessage msg;
    msg.type = type;
    msg.source_ip = ip_address;
    msg.source_port = port;
    msg.destination_ip = dest_node.ip_address;
    msg.destination_port = dest_node.port;
    msg.payload = payload;
    msg.timestamp = timestamp;
    msg.message_id = "msg_" + std::to_string(timestamp) + "_" + std::to_string(rand());
    
    { 
        std::unique_lock<std::mutex> lock(outgoing_mutex);
        outgoing_messages.push(msg);
        outgoing_cv.notify_one();
    }
    
    return true;
}

bool CTPL_Network_Manager::send_message_to_all(NetworkMessageType type, const std::string &payload) {
    std::vector<NetworkNode> active_nodes = get_active_nodes();
    
    for (const auto &node : active_nodes) {
        send_message(node.node_id, type, payload);
    }
    
    return true;
}

bool CTPL_Network_Manager::register_message_handler(std::function<void(const NetworkMessage &)> handler) {
    message_handler = handler;
    return true;
}

bool CTPL_Network_Manager::enable_load_balancing(int strategy) {
    // 根据策略选择负载均衡算法
    switch (strategy) {
        case 0: // 随机
            load_balancing_strategy = [](const std::vector<NetworkNode> &nodes, int task_type) -> std::string {
                if (nodes.empty()) return "";
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, nodes.size() - 1);
                return nodes[dis(gen)].node_id;
            };
            break;
            
        case 1: // 轮询
        {
            static int current_index = 0;
            load_balancing_strategy = [](const std::vector<NetworkNode> &nodes, int task_type) -> std::string {
                if (nodes.empty()) return "";
                current_index = (current_index + 1) % nodes.size();
                return nodes[current_index].node_id;
            };
            break;
        }
            
        case 2: // 最少连接
            load_balancing_strategy = [](const std::vector<NetworkNode> &nodes, int task_type) -> std::string {
                if (nodes.empty()) return "";
                
                auto min_it = std::min_element(nodes.begin(), nodes.end(), [](const NetworkNode &a, const NetworkNode &b) {
                    return a.load_factor < b.load_factor;
                });
                
                return min_it->node_id;
            };
            break;
    }
    
    return true;
}

std::string CTPL_Network_Manager::select_node_for_task(int task_type) {
    std::vector<NetworkNode> active_nodes = get_active_nodes();
    
    if (active_nodes.empty() || !load_balancing_strategy) {
        return "";
    }
    
    return load_balancing_strategy(active_nodes, task_type);
}

bool CTPL_Network_Manager::enable_heartbeat(int interval_ms, int timeout_ms) {
    if (is_heartbeat_enabled) {
        return false;
    }
    
    is_heartbeat_enabled = true;
    heartbeat_interval = interval_ms;
    heartbeat_timeout = timeout_ms;
    
    heartbeat_thread_ptr = std::make_unique<std::thread>(&CTPL_Network_Manager::heartbeat_thread, this);
    
    return true;
}

void CTPL_Network_Manager::network_thread() {
    while (is_running) {
        // 模拟网络通信: 这里可以替换为实际的ZeroMQ或其他网络库实现
        NetworkMessage msg;
        bool has_message = false;
        
        { 
            std::unique_lock<std::mutex> lock(outgoing_mutex);
            if (!outgoing_messages.empty()) {
                msg = outgoing_messages.front();
                outgoing_messages.pop();
                has_message = true;
            }
        }
        
        if (has_message) {
            // 这里可以添加实际的消息发送逻辑
            // 模拟网络延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 模拟消息接收 (回声测试)
            NetworkMessage reply;
            reply.type = msg.type;
            reply.source_ip = msg.destination_ip;
            reply.source_port = msg.destination_port;
            reply.destination_ip = msg.source_ip;
            reply.destination_port = msg.source_port;
            reply.payload = "ECHO: " + msg.payload;
            reply.timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
            reply.message_id = "reply_" + msg.message_id;
            
            { 
                std::unique_lock<std::mutex> lock(incoming_mutex);
                incoming_messages.push(reply);
                incoming_cv.notify_one();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void CTPL_Network_Manager::heartbeat_thread() {
    while (is_heartbeat_enabled && is_running) {
        // 更新本地节点的心跳时间
        uint64_t current_time = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
        
        { 
            std::unique_lock<std::mutex> lock(nodes_mutex);
            auto it = nodes.find(node_id);
            if (it != nodes.end()) {
                it->second.last_heartbeat = current_time;
            }
        }
        
        // 发送心跳消息到所有节点
        send_message_to_all(NetworkMessageType::HEARTBEAT, "HEARTBEAT from " + node_id);
        
        // 检查节点活性
        std::vector<NetworkNode> active_nodes = get_active_nodes();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval));
    }
}

void CTPL_Network_Manager::message_handler_thread() {
    while (is_running) {
        NetworkMessage msg;
        bool has_message = false;
        
        { 
            std::unique_lock<std::mutex> lock(incoming_mutex);
            if (incoming_cv.wait_for(lock, std::chrono::milliseconds(1000), [this] { return !incoming_messages.empty(); })) {
                msg = incoming_messages.front();
                incoming_messages.pop();
                has_message = true;
            }
        }
        
        if (has_message && message_handler) {
            message_handler(msg);
        }
    }
}
