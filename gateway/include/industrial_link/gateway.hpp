#pragma once

#include "industrial_link/plugin_manager.hpp"
#include "industrial_link/storage.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace industrial_link {

struct GatewayConfig {
    std::string bind_address{"127.0.0.1"};
    std::uint16_t tcp_port{9000};
    std::uint16_t udp_port{9001};
    std::uint16_t http_port{8080};
    int heartbeat_seconds{5};
    int timeout_seconds{15};
    std::size_t max_clients{2048};
    std::size_t max_events{500};
    std::string event_log{"data/events.jsonl"};
    std::string database{"data/industrial_link.db"};
    std::vector<std::string> plugin_paths;
};

struct DeviceSession {
    int fd{-1};
    std::string id;
    std::string protocol{"tcp"};
    std::string peer;
    std::string state{"connecting"};
    std::string receive_buffer;
    std::chrono::steady_clock::time_point connected_at;
    std::chrono::steady_clock::time_point last_seen;
    std::chrono::steady_clock::time_point last_ping;
    std::uint64_t last_sequence{0};
    std::uint64_t messages{0};
    std::uint64_t bytes{0};
};

struct GatewayMetrics {
    std::uint64_t accepted_connections{0};
    std::uint64_t closed_connections{0};
    std::uint64_t messages_received{0};
    std::uint64_t bytes_received{0};
    std::uint64_t duplicate_messages{0};
    std::uint64_t malformed_messages{0};
    std::uint64_t commands_sent{0};
    std::uint64_t udp_messages{0};
    std::uint64_t alarms_triggered{0};
};

class Gateway {
public:
    explicit Gateway(GatewayConfig config);
    ~Gateway();
    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    bool initialize(std::string& error);
    int run();
    void request_stop();

private:
    bool open_tcp_listener(std::uint16_t port, int& fd, std::string& error);
    bool open_udp_listener(std::uint16_t port, int& fd, std::string& error);
    void accept_device();
    void accept_http();
    void receive_udp();
    void receive_device(int fd);
    void process_line(DeviceSession& session, const std::string& line);
    void close_device(int fd, const std::string& reason);
    void maintenance();
    void add_event(const std::string& type, const std::string& device_id,
                   const std::string& detail);
    void evaluate_alarms(const std::string& device_id, const std::string& payload);
    std::string devices_json() const;
    std::string metrics_json() const;
    std::string events_json() const;
    std::string plugins_json() const;
    void handle_http_request(int client_fd);
    bool send_command(const std::string& device_id, const std::string& command,
                      std::string& error);

    GatewayConfig config_;
    int tcp_listener_{-1};
    int udp_listener_{-1};
    int http_listener_{-1};
    std::atomic<bool> stop_{false};
    std::unordered_map<int, DeviceSession> sessions_;
    std::unordered_map<std::string, int> device_index_;
    std::unordered_map<std::string, DeviceSession> udp_devices_;
    std::deque<std::string> events_;
    GatewayMetrics metrics_;
    PluginManager plugins_;
    Storage storage_;
    std::ofstream event_log_;
    std::chrono::steady_clock::time_point started_at_;
};

}  // namespace industrial_link
