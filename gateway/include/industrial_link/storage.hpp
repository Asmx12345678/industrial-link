#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace industrial_link {

class Storage {
public:
    Storage() = default;
    ~Storage();
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    bool open(const std::string& path, std::string& error);
    bool record_event(std::int64_t timestamp_ms, const std::string& type,
                      const std::string& device_id, const std::string& detail);
    std::string recent_events_json(std::size_t limit, const std::string& device_id);
    bool enabled() const { return database_ != nullptr; }

private:
    sqlite3* database_{};
    sqlite3_stmt* insert_event_{};
};

}  // namespace industrial_link
