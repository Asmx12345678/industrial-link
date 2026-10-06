#include "industrial_link/gateway.hpp"
#include "industrial_link/json.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE_SOCKET close
#endif

namespace industrial_link {
namespace {

std::int64_t epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string socket_error() { return std::strerror(errno); }

bool set_nonblocking(int fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool set_blocking(int fd) {
#ifdef _WIN32
    u_long mode = 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0;
#endif
}

std::string peer_name(const sockaddr_in& address) {
    char host[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &address.sin_addr, host, sizeof(host));
    return std::string(host) + ":" + std::to_string(ntohs(address.sin_port));
}

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
        const auto count = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        const auto count = ::send(fd, data.data() + sent, data.size() - sent, 0);
#endif
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<double> extract_metric(const std::string& payload, const std::string& key) {
    std::size_t pos = payload.find(key + "=");
    if (pos != std::string::npos) {
        pos += key.size() + 1;
    } else {
        pos = payload.find("\"" + key + "\"");
        if (pos == std::string::npos) return std::nullopt;
        pos = payload.find(':', pos + key.size() + 2);
        if (pos == std::string::npos) return std::nullopt;
        ++pos;
    }
    while (pos < payload.size() && std::isspace(static_cast<unsigned char>(payload[pos]))) ++pos;
    char* end = nullptr;
    const double value = std::strtod(payload.c_str() + pos, &end);
    if (end == payload.c_str() + pos) return std::nullopt;
    return value;
}

std::string url_decode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const long decoded = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                result.push_back(static_cast<char>(decoded));
                i += 2;
                continue;
            }
        }
        result.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return result;
}

std::string query_value(const std::string& path, const std::string& key) {
    const auto query = path.find('?');
    if (query == std::string::npos) return {};
    std::size_t cursor = query + 1;
    while (cursor < path.size()) {
        const auto end = path.find('&', cursor);
        const auto pair = path.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
        const auto equals = pair.find('=');
        if (equals != std::string::npos && pair.substr(0, equals) == key)
            return url_decode(pair.substr(equals + 1));
        if (end == std::string::npos) break;
        cursor = end + 1;
    }
    return {};
}

std::string http_response(int status, const std::string& content_type,
                          const std::string& body) {
    const char* reason = status == 200 ? "OK" : status == 400 ? "Bad Request"
                                           : status == 404   ? "Not Found"
                                                             : "Internal Server Error";
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    return response.str();
}

const char* dashboard_html() {
    return R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>IndustrialLink Gateway</title>
<style>body{font:15px system-ui;margin:2rem;background:#f4f7fb;color:#152238}h1{margin-bottom:.3rem}.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:1rem}.card{background:white;padding:1rem;border-radius:10px;box-shadow:0 2px 12px #dbe3ee}.value{font-size:2rem;font-weight:700}table{width:100%;border-collapse:collapse;background:white;margin-top:1rem}th,td{padding:.65rem;border-bottom:1px solid #e7ebf0;text-align:left}.online{color:#087f5b}@media(max-width:800px){.grid{grid-template-columns:1fr 1fr}}</style></head>
<body><h1>IndustrialLink</h1><p>工业通信网关实时状态</p><div class="grid" id="metrics"></div>
<table><thead><tr><th>设备</th><th>协议</th><th>状态</th><th>地址</th><th>消息数</th><th>最后序号</th></tr></thead><tbody id="devices"></tbody></table>
<script>async function refresh(){const [m,d]=await Promise.all([fetch('/api/metrics').then(r=>r.json()),fetch('/api/devices').then(r=>r.json())]);const cards=[['在线设备',m.online_devices],['累计消息',m.messages_received],['接收字节',m.bytes_received],['运行秒数',m.uptime_seconds]];document.querySelector('#metrics').innerHTML=cards.map(x=>`<div class=card><div>${x[0]}</div><div class=value>${x[1]}</div></div>`).join('');document.querySelector('#devices').innerHTML=d.devices.map(x=>`<tr><td>${x.id}</td><td>${x.protocol}</td><td class=online>${x.state}</td><td>${x.peer}</td><td>${x.messages}</td><td>${x.last_sequence}</td></tr>`).join('')}refresh();setInterval(refresh,1000)</script></body></html>)HTML";
}

}  // namespace

Gateway::Gateway(GatewayConfig config) : config_(std::move(config)) {}

Gateway::~Gateway() {
    for (const auto& entry : sessions_) CLOSE_SOCKET(entry.first);
    if (tcp_listener_ >= 0) CLOSE_SOCKET(tcp_listener_);
    if (udp_listener_ >= 0) CLOSE_SOCKET(udp_listener_);
    if (http_listener_ >= 0) CLOSE_SOCKET(http_listener_);
}

bool Gateway::open_tcp_listener(std::uint16_t port, int& fd, std::string& error) {
    fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) { error = "socket: " + socket_error(); return false; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) != 1) {
        error = "invalid bind address: " + config_.bind_address;
        CLOSE_SOCKET(fd); fd = -1; return false;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        error = "bind port " + std::to_string(port) + ": " + socket_error();
        CLOSE_SOCKET(fd); fd = -1; return false;
    }
    if (::listen(fd, 128) < 0 || !set_nonblocking(fd)) {
        error = "listen/nonblocking: " + socket_error();
        CLOSE_SOCKET(fd); fd = -1; return false;
    }
    return true;
}

bool Gateway::open_udp_listener(std::uint16_t port, int& fd, std::string& error) {
    fd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (fd < 0) { error = "udp socket: " + socket_error(); return false; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) != 1 ||
        ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        !set_nonblocking(fd)) {
        error = "udp bind/nonblocking port " + std::to_string(port) + ": " + socket_error();
        CLOSE_SOCKET(fd); fd = -1; return false;
    }
    return true;
}

bool Gateway::initialize(std::string& error) {
    if (!open_tcp_listener(config_.tcp_port, tcp_listener_, error)) return false;
    if (!open_udp_listener(config_.udp_port, udp_listener_, error)) return false;
    if (!open_tcp_listener(config_.http_port, http_listener_, error)) return false;

    for (const auto& path : config_.plugin_paths) {
        std::string plugin_error;
        if (!plugins_.load(path, plugin_error)) {
            error = "cannot load plugin " + path + ": " + plugin_error;
            return false;
        }
    }
    if (!config_.event_log.empty()) {
        const std::filesystem::path path(config_.event_log);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        event_log_.open(config_.event_log, std::ios::app);
        if (!event_log_) {
            error = "cannot open event log: " + config_.event_log;
            return false;
        }
    }
    if (!config_.database.empty()) {
        const std::filesystem::path path(config_.database);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        if (!storage_.open(config_.database, error)) {
            error = "cannot open SQLite database: " + error;
            return false;
        }
    }
    started_at_ = std::chrono::steady_clock::now();
    add_event("gateway_started", "gateway",
              "tcp=" + std::to_string(config_.tcp_port) +
              ",udp=" + std::to_string(config_.udp_port) +
              ",http=" + std::to_string(config_.http_port));
    return true;
}

void Gateway::request_stop() { stop_.store(true); }

int Gateway::run() {
    std::cout << "IndustrialLink gateway listening on " << config_.bind_address
              << " tcp=" << config_.tcp_port << " udp=" << config_.udp_port
              << " http=" << config_.http_port << '\n';
    while (!stop_.load()) {
        fd_set read_set;
        FD_ZERO(&read_set);
        int max_fd = std::max({tcp_listener_, udp_listener_, http_listener_});
        FD_SET(tcp_listener_, &read_set);
        FD_SET(udp_listener_, &read_set);
        FD_SET(http_listener_, &read_set);
        for (const auto& entry : sessions_) {
            if (entry.first < FD_SETSIZE) {
                FD_SET(entry.first, &read_set);
                max_fd = std::max(max_fd, entry.first);
            }
        }
        timeval timeout{0, 250000};
        const int ready = select(max_fd + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "select failed: " << socket_error() << '\n';
            return 2;
        }
        if (ready > 0) {
            if (FD_ISSET(tcp_listener_, &read_set)) accept_device();
            if (FD_ISSET(udp_listener_, &read_set)) receive_udp();
            if (FD_ISSET(http_listener_, &read_set)) accept_http();
            std::vector<int> readable;
            for (const auto& entry : sessions_) {
                if (entry.first < FD_SETSIZE && FD_ISSET(entry.first, &read_set))
                    readable.push_back(entry.first);
            }
            for (const int fd : readable) receive_device(fd);
        }
        maintenance();
    }
    add_event("gateway_stopped", "gateway", "graceful shutdown");
    return 0;
}

void Gateway::accept_device() {
    while (true) {
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        const int fd = static_cast<int>(::accept(tcp_listener_, reinterpret_cast<sockaddr*>(&peer), &length));
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return;
        }
        if (sessions_.size() >= config_.max_clients || fd >= FD_SETSIZE || !set_nonblocking(fd)) {
            send_all(fd, "ERROR gateway_capacity\n");
            CLOSE_SOCKET(fd);
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        DeviceSession session;
        session.fd = fd;
        session.id = "pending-" + std::to_string(fd);
        session.peer = peer_name(peer);
        session.connected_at = now;
        session.last_seen = now;
        session.last_ping = now;
        sessions_.emplace(fd, std::move(session));
        ++metrics_.accepted_connections;
        add_event("connection_opened", "pending", peer_name(peer));
    }
}

void Gateway::receive_device(int fd) {
    auto it = sessions_.find(fd);
    if (it == sessions_.end()) return;
    char buffer[8192];
    while (true) {
        const auto received = ::recv(fd, buffer, sizeof(buffer), 0);
        if (received > 0) {
            metrics_.bytes_received += static_cast<std::uint64_t>(received);
            it->second.bytes += static_cast<std::uint64_t>(received);
            it->second.receive_buffer.append(buffer, static_cast<std::size_t>(received));
            it->second.last_seen = std::chrono::steady_clock::now();
            if (it->second.receive_buffer.size() > 1024 * 1024) {
                close_device(fd, "receive_buffer_limit");
                return;
            }
            std::size_t newline = 0;
            while ((newline = it->second.receive_buffer.find('\n')) != std::string::npos) {
                std::string line = trim(it->second.receive_buffer.substr(0, newline));
                it->second.receive_buffer.erase(0, newline + 1);
                if (!line.empty()) process_line(it->second, line);
                it = sessions_.find(fd);
                if (it == sessions_.end()) return;
            }
            continue;
        }
        if (received == 0) { close_device(fd, "peer_closed"); return; }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        close_device(fd, "recv_error");
        return;
    }
}

void Gateway::process_line(DeviceSession& session, const std::string& line) {
    std::istringstream input(line);
    std::string type;
    input >> type;
    if (type == "HELLO") {
        std::string id, protocol;
        input >> id >> protocol;
        if (id.empty() || id.size() > 128) {
            ++metrics_.malformed_messages;
            send_all(session.fd, "ERROR invalid_device_id\n");
            return;
        }
        const auto existing = device_index_.find(id);
        if (existing != device_index_.end() && existing->second != session.fd)
            close_device(existing->second, "replaced_by_new_session");
        if (session.id.rfind("pending-", 0) != 0) device_index_.erase(session.id);
        session.id = id;
        session.protocol = protocol.empty() ? "tcp" : protocol;
        session.state = "online";
        device_index_[id] = session.fd;
        send_all(session.fd, "WELCOME heartbeat=" + std::to_string(config_.heartbeat_seconds) + "\n");
        add_event("device_online", id, "protocol=" + session.protocol + ",peer=" + session.peer);
        return;
    }
    if (session.state != "online") {
        ++metrics_.malformed_messages;
        send_all(session.fd, "ERROR hello_required\n");
        return;
    }
    if (type == "PROXY_DATA") {
        if (session.protocol.rfind("bridge:", 0) != 0) {
            ++metrics_.malformed_messages;
            send_all(session.fd, "ERROR bridge_protocol_required\n");
            return;
        }
        std::string device_id, protocol;
        std::uint64_t sequence = 0;
        input >> device_id >> protocol >> sequence;
        std::string payload;
        std::getline(input, payload);
        payload = trim(payload);
        if (device_id.empty() || protocol.empty() || input.fail()) {
            ++metrics_.malformed_messages;
            send_all(session.fd, "ERROR invalid_proxy_data\n");
            return;
        }
        auto& device = udp_devices_[device_id];
        if (device.id.empty()) {
            device.id = device_id;
            device.fd = -1;
            device.protocol = protocol;
            device.peer = "via:" + session.id;
            device.state = "online";
            device.connected_at = std::chrono::steady_clock::now();
            add_event("device_online", device_id,
                      "protocol=" + protocol + ",peer=" + device.peer);
        }
        device.last_seen = std::chrono::steady_clock::now();
        if (device.messages > 0 && sequence <= device.last_sequence) {
            ++metrics_.duplicate_messages;
        } else {
            device.last_sequence = sequence;
            ++device.messages;
            ++metrics_.messages_received;
            add_event("telemetry", device_id,
                      "seq=" + std::to_string(sequence) + ",payload=" + payload);
            evaluate_alarms(device_id, payload);
        }
        ++session.messages;
        send_all(session.fd, "ACK_PROXY " + device_id + " " + std::to_string(sequence) + "\n");
    } else if (type == "DATA") {
        std::uint64_t sequence = 0;
        if (!(input >> sequence)) {
            ++metrics_.malformed_messages;
            send_all(session.fd, "ERROR invalid_sequence\n");
            return;
        }
        std::string payload;
        std::getline(input, payload);
        payload = trim(payload);
        if (session.messages > 0 && sequence <= session.last_sequence) {
            ++metrics_.duplicate_messages;
            send_all(session.fd, "DUP " + std::to_string(sequence) + "\n");
            return;
        }
        if (session.protocol.rfind("plugin:", 0) == 0) {
            std::string decoded, error;
            if (!plugins_.decode(session.protocol.substr(7), payload, decoded, error)) {
                ++metrics_.malformed_messages;
                send_all(session.fd, "ERROR plugin_decode\n");
                add_event("decode_error", session.id, error);
                return;
            }
            payload = decoded;
        }
        session.last_sequence = sequence;
        ++session.messages;
        ++metrics_.messages_received;
        add_event("telemetry", session.id,
                  "seq=" + std::to_string(sequence) + ",payload=" + payload);
        evaluate_alarms(session.id, payload);
        send_all(session.fd, "ACK " + std::to_string(sequence) + "\n");
    } else if (type == "PONG") {
        std::string sequence;
        input >> sequence;
        add_event("heartbeat", session.id, "pong=" + sequence);
    } else if (type == "ACK") {
        std::string command_id;
        input >> command_id;
        add_event("command_ack", session.id, command_id);
    } else {
        ++metrics_.malformed_messages;
        send_all(session.fd, "ERROR unknown_message\n");
    }
}

void Gateway::receive_udp() {
    char buffer[65536];
    while (true) {
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        const auto received = recvfrom(udp_listener_, buffer, sizeof(buffer) - 1, 0,
                                       reinterpret_cast<sockaddr*>(&peer), &length);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return;
        }
        buffer[received] = '\0';
        std::istringstream input(trim(std::string(buffer, static_cast<std::size_t>(received))));
        std::string type, id, payload;
        std::uint64_t sequence = 0;
        input >> type >> id >> sequence;
        std::getline(input, payload);
        payload = trim(payload);
        if (type != "DATA" || id.empty() || input.fail()) {
            ++metrics_.malformed_messages;
            continue;
        }
        auto& device = udp_devices_[id];
        const bool first_seen = device.id.empty();
        if (first_seen) {
            device.id = id;
            device.fd = -1;
            device.protocol = "udp";
            device.peer = peer_name(peer);
            device.state = "online";
            device.connected_at = std::chrono::steady_clock::now();
            add_event("device_online", id, "protocol=udp,peer=" + device.peer);
        }
        device.last_seen = std::chrono::steady_clock::now();
        device.bytes += static_cast<std::uint64_t>(received);
        ++metrics_.bytes_received;
        ++metrics_.udp_messages;
        if (device.messages > 0 && sequence <= device.last_sequence) {
            ++metrics_.duplicate_messages;
        } else {
            device.last_sequence = sequence;
            ++device.messages;
            ++metrics_.messages_received;
            add_event("telemetry", id, "seq=" + std::to_string(sequence) + ",payload=" + payload);
            evaluate_alarms(id, payload);
        }
        const std::string response = "ACK " + std::to_string(sequence) + "\n";
        sendto(udp_listener_, response.data(), response.size(), 0,
               reinterpret_cast<sockaddr*>(&peer), length);
    }
}

void Gateway::close_device(int fd, const std::string& reason) {
    const auto it = sessions_.find(fd);
    if (it == sessions_.end()) return;
    const std::string id = it->second.id;
    add_event("device_offline", id, reason);
    const auto indexed = device_index_.find(id);
    if (indexed != device_index_.end() && indexed->second == fd) device_index_.erase(indexed);
    CLOSE_SOCKET(fd);
    sessions_.erase(it);
    ++metrics_.closed_connections;
}

void Gateway::maintenance() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> stale;
    for (auto& entry : sessions_) {
        auto& session = entry.second;
        if (now - session.last_seen > std::chrono::seconds(config_.timeout_seconds)) {
            stale.push_back(entry.first);
            continue;
        }
        if (session.state == "online" &&
            now - session.last_ping > std::chrono::seconds(config_.heartbeat_seconds)) {
            if (!send_all(entry.first, "PING " + std::to_string(epoch_ms()) + "\n"))
                stale.push_back(entry.first);
            session.last_ping = now;
        }
    }
    for (const int fd : stale) close_device(fd, "heartbeat_timeout");

    std::vector<std::string> stale_udp;
    for (const auto& entry : udp_devices_) {
        if (now - entry.second.last_seen > std::chrono::seconds(config_.timeout_seconds))
            stale_udp.push_back(entry.first);
    }
    for (const auto& id : stale_udp) {
        add_event("device_offline", id, "udp_timeout");
        udp_devices_.erase(id);
    }
}

void Gateway::add_event(const std::string& type, const std::string& device_id,
                        const std::string& detail) {
    const auto timestamp = epoch_ms();
    std::ostringstream event;
    event << "{\"timestamp\":" << timestamp
          << ",\"type\":" << json_string(type)
          << ",\"device_id\":" << json_string(device_id)
          << ",\"detail\":" << json_string(detail) << '}';
    events_.push_back(event.str());
    while (events_.size() > config_.max_events) events_.pop_front();
    if (event_log_) {
        event_log_ << event.str() << '\n';
        event_log_.flush();
    }
    if (!storage_.record_event(timestamp, type, device_id, detail)) {
        std::cerr << "warning: failed to persist event to SQLite\n";
    }
}

void Gateway::evaluate_alarms(const std::string& device_id, const std::string& payload) {
    const auto temperature = extract_metric(payload, "temperature");
    if (temperature && *temperature > 80.0) {
        ++metrics_.alarms_triggered;
        add_event("alarm", device_id,
                  "HIGH_TEMPERATURE value=" + std::to_string(*temperature) + ",threshold=80");
    }
    const auto voltage = extract_metric(payload, "voltage");
    if (voltage && (*voltage < 20.0 || *voltage > 28.0)) {
        ++metrics_.alarms_triggered;
        add_event("alarm", device_id,
                  "VOLTAGE_OUT_OF_RANGE value=" + std::to_string(*voltage) + ",range=[20,28]");
    }
}

std::string Gateway::devices_json() const {
    std::ostringstream out;
    out << "{\"devices\":[";
    bool first = true;
    auto append = [&](const DeviceSession& session) {
        if (!first) out << ',';
        first = false;
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - session.last_seen).count();
        out << "{\"id\":" << json_string(session.id)
            << ",\"protocol\":" << json_string(session.protocol)
            << ",\"peer\":" << json_string(session.peer)
            << ",\"state\":" << json_string(session.state)
            << ",\"messages\":" << session.messages
            << ",\"bytes\":" << session.bytes
            << ",\"last_sequence\":" << session.last_sequence
            << ",\"last_seen_ms_ago\":" << age << '}';
    };
    for (const auto& entry : sessions_) {
        if (entry.second.state == "online") append(entry.second);
    }
    for (const auto& entry : udp_devices_) append(entry.second);
    out << "]}";
    return out.str();
}

std::string Gateway::metrics_json() const {
    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - started_at_).count();
    std::size_t online = udp_devices_.size();
    for (const auto& entry : sessions_) if (entry.second.state == "online") ++online;
    std::ostringstream out;
    out << "{\"online_devices\":" << online
        << ",\"open_tcp_sessions\":" << sessions_.size()
        << ",\"accepted_connections\":" << metrics_.accepted_connections
        << ",\"closed_connections\":" << metrics_.closed_connections
        << ",\"messages_received\":" << metrics_.messages_received
        << ",\"bytes_received\":" << metrics_.bytes_received
        << ",\"duplicate_messages\":" << metrics_.duplicate_messages
        << ",\"malformed_messages\":" << metrics_.malformed_messages
        << ",\"commands_sent\":" << metrics_.commands_sent
        << ",\"udp_messages\":" << metrics_.udp_messages
        << ",\"alarms_triggered\":" << metrics_.alarms_triggered
        << ",\"uptime_seconds\":" << uptime << '}';
    return out.str();
}

std::string Gateway::events_json() const {
    std::ostringstream out;
    out << "{\"events\":[";
    for (std::size_t i = 0; i < events_.size(); ++i) {
        if (i) out << ',';
        out << events_[events_.size() - 1 - i];
    }
    out << "]}";
    return out.str();
}

std::string Gateway::plugins_json() const {
    const auto names = plugins_.names();
    std::ostringstream out;
    out << "{\"plugins\":[";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) out << ',';
        out << json_string(names[i]);
    }
    out << "]}";
    return out.str();
}

bool Gateway::send_command(const std::string& device_id, const std::string& command,
                           std::string& error) {
    const auto indexed = device_index_.find(device_id);
    if (indexed == device_index_.end()) { error = "device is not online"; return false; }
    if (command.empty() || command.size() > 4096 || command.find('\n') != std::string::npos) {
        error = "invalid command";
        return false;
    }
    const std::string command_id = std::to_string(epoch_ms());
    if (!send_all(indexed->second, "COMMAND " + command_id + " " + command + "\n")) {
        error = "send failed";
        return false;
    }
    ++metrics_.commands_sent;
    add_event("command_sent", device_id, "id=" + command_id + ",command=" + command);
    return true;
}

void Gateway::accept_http() {
    while (true) {
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        const int fd = static_cast<int>(::accept(http_listener_, reinterpret_cast<sockaddr*>(&peer), &length));
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return;
        }
        // Accepted sockets inherit O_NONBLOCK on some BSD/macOS versions.
        // The HTTP handler is intentionally short-lived and blocking, so make
        // that contract explicit before attempting the first recv().
        set_blocking(fd);
        handle_http_request(fd);
        CLOSE_SOCKET(fd);
    }
}

void Gateway::handle_http_request(int client_fd) {
    timeval timeout{1, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::string request;
    char buffer[4096];
    std::size_t expected = 0;
    while (request.size() < 65536) {
        const auto count = recv(client_fd, buffer, sizeof(buffer), 0);
        if (count <= 0) break;
        request.append(buffer, static_cast<std::size_t>(count));
        const auto header_end = request.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            if (expected == 0) {
                const auto content_pos = request.find("Content-Length:");
                if (content_pos != std::string::npos) {
                    const auto value_start = content_pos + 15;
                    expected = header_end + 4 + static_cast<std::size_t>(
                        std::strtoul(request.c_str() + value_start, nullptr, 10));
                } else expected = header_end + 4;
            }
            if (request.size() >= expected) break;
        }
    }
    std::istringstream first_line(request.substr(0, request.find("\r\n")));
    std::string method, path, version;
    first_line >> method >> path >> version;
    const std::string route = path.substr(0, path.find('?'));
    if (method == "OPTIONS") {
        send_all(client_fd, http_response(200, "application/json", "{}"));
        return;
    }
    std::string body;
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    if (method == "GET" && route == "/") {
        body = dashboard_html();
        content_type = "text/html; charset=utf-8";
    } else if (method == "GET" && route == "/health") {
        body = "{\"status\":\"ok\"}";
    } else if (method == "GET" && route == "/api/devices") {
        body = devices_json();
    } else if (method == "GET" && route == "/api/metrics") {
        body = metrics_json();
    } else if (method == "GET" && route == "/api/events") {
        body = events_json();
    } else if (method == "GET" && route == "/api/history") {
        std::size_t limit = 100;
        const std::string requested_limit = query_value(path, "limit");
        if (!requested_limit.empty()) {
            try { limit = std::stoul(requested_limit); } catch (...) { limit = 100; }
        }
        body = storage_.recent_events_json(limit, query_value(path, "device_id"));
    } else if (method == "GET" && route == "/api/plugins") {
        body = plugins_json();
    } else if (method == "POST" && route == "/api/command") {
        const auto body_pos = request.find("\r\n\r\n");
        const std::string request_body = body_pos == std::string::npos ? "" : request.substr(body_pos + 4);
        const std::string device_id = extract_json_string(request_body, "device_id");
        const std::string command = extract_json_string(request_body, "command");
        std::string error;
        if (send_command(device_id, command, error)) {
            body = "{\"status\":\"sent\"}";
        } else {
            status = error == "device is not online" ? 404 : 400;
            body = "{\"error\":" + json_string(error) + '}';
        }
    } else {
        status = 404;
        body = "{\"error\":\"not found\"}";
    }
    send_all(client_fd, http_response(status, content_type, body));
}

}  // namespace industrial_link
