#include "industrial_link/gateway.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
industrial_link::Gateway* active_gateway = nullptr;

void handle_signal(int) {
    if (active_gateway) active_gateway->request_stop();
}

void print_help() {
    std::cout
        << "IndustrialLink gateway\n\n"
        << "Options:\n"
        << "  --bind ADDRESS          Bind address (default 127.0.0.1)\n"
        << "  --tcp-port PORT         Device TCP port (default 9000)\n"
        << "  --udp-port PORT         Device UDP port (default 9001)\n"
        << "  --http-port PORT        Dashboard/API port (default 8080)\n"
        << "  --heartbeat SEC         Heartbeat interval (default 5)\n"
        << "  --timeout SEC           Device timeout (default 15)\n"
        << "  --max-clients COUNT     Maximum TCP clients (default 2048)\n"
        << "  --event-log PATH        JSONL event log (default data/events.jsonl)\n"
        << "  --database PATH         SQLite event database (default data/industrial_link.db)\n"
        << "  --plugin PATH           Load protocol plugin; repeatable\n"
        << "  --help                  Show this help\n";
}

std::string require_value(int& index, int argc, char** argv) {
    if (++index >= argc) throw std::runtime_error(std::string("missing value for ") + argv[index - 1]);
    return argv[index];
}

std::uint16_t parse_port(const std::string& value) {
    const int port = std::stoi(value);
    if (port < 1 || port > 65535) throw std::runtime_error("invalid port: " + value);
    return static_cast<std::uint16_t>(port);
}
}  // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    industrial_link::GatewayConfig config;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") { print_help(); return 0; }
            if (arg == "--bind") config.bind_address = require_value(i, argc, argv);
            else if (arg == "--tcp-port") config.tcp_port = parse_port(require_value(i, argc, argv));
            else if (arg == "--udp-port") config.udp_port = parse_port(require_value(i, argc, argv));
            else if (arg == "--http-port") config.http_port = parse_port(require_value(i, argc, argv));
            else if (arg == "--heartbeat") config.heartbeat_seconds = std::stoi(require_value(i, argc, argv));
            else if (arg == "--timeout") config.timeout_seconds = std::stoi(require_value(i, argc, argv));
            else if (arg == "--max-clients") config.max_clients = std::stoul(require_value(i, argc, argv));
            else if (arg == "--event-log") config.event_log = require_value(i, argc, argv);
            else if (arg == "--database") config.database = require_value(i, argc, argv);
            else if (arg == "--plugin") config.plugin_paths.push_back(require_value(i, argc, argv));
            else throw std::runtime_error("unknown option: " + arg);
        }
        if (config.heartbeat_seconds < 1 || config.timeout_seconds <= config.heartbeat_seconds)
            throw std::runtime_error("timeout must be greater than heartbeat and both must be positive");
    } catch (const std::exception& error) {
        std::cerr << "configuration error: " << error.what() << "\n\n";
        print_help();
        return 1;
    }

    industrial_link::Gateway gateway(config);
    active_gateway = &gateway;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::string error;
    if (!gateway.initialize(error)) {
        std::cerr << "initialization failed: " << error << '\n';
        return 1;
    }
    return gateway.run();
}
