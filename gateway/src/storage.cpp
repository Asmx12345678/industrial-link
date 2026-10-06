#include "industrial_link/storage.hpp"
#include "industrial_link/json.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <sstream>

namespace industrial_link {

Storage::~Storage() {
    if (insert_event_) sqlite3_finalize(insert_event_);
    if (database_) sqlite3_close(database_);
}

bool Storage::open(const std::string& path, std::string& error) {
    if (path.empty()) return true;
    if (sqlite3_open_v2(path.c_str(), &database_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        error = database_ ? sqlite3_errmsg(database_) : "sqlite open failed";
        return false;
    }
    const char* schema =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS events("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " timestamp_ms INTEGER NOT NULL,"
        " type TEXT NOT NULL,"
        " device_id TEXT NOT NULL,"
        " detail TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS events_device_time ON events(device_id,timestamp_ms);";
    char* message = nullptr;
    if (sqlite3_exec(database_, schema, nullptr, nullptr, &message) != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(database_);
        sqlite3_free(message);
        return false;
    }
    const char* insert_sql =
        "INSERT INTO events(timestamp_ms,type,device_id,detail) VALUES(?,?,?,?)";
    if (sqlite3_prepare_v2(database_, insert_sql, -1, &insert_event_, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(database_);
        return false;
    }
    return true;
}

bool Storage::record_event(std::int64_t timestamp_ms, const std::string& type,
                           const std::string& device_id, const std::string& detail) {
    if (!insert_event_) return true;
    sqlite3_reset(insert_event_);
    sqlite3_clear_bindings(insert_event_);
    sqlite3_bind_int64(insert_event_, 1, timestamp_ms);
    sqlite3_bind_text(insert_event_, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_event_, 3, device_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insert_event_, 4, detail.c_str(), -1, SQLITE_TRANSIENT);
    return sqlite3_step(insert_event_) == SQLITE_DONE;
}

std::string Storage::recent_events_json(std::size_t limit, const std::string& device_id) {
    if (!database_) return "{\"events\":[]}";
    limit = std::min<std::size_t>(limit, 1000);
    const char* sql_all =
        "SELECT timestamp_ms,type,device_id,detail FROM events ORDER BY id DESC LIMIT ?";
    const char* sql_device =
        "SELECT timestamp_ms,type,device_id,detail FROM events WHERE device_id=? "
        "ORDER BY id DESC LIMIT ?";
    sqlite3_stmt* statement = nullptr;
    const char* sql = device_id.empty() ? sql_all : sql_device;
    if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK)
        return "{\"events\":[],\"error\":\"query failed\"}";
    int parameter = 1;
    if (!device_id.empty()) {
        sqlite3_bind_text(statement, parameter++, device_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(statement, parameter, static_cast<int>(limit));
    std::ostringstream out;
    out << "{\"events\":[";
    bool first = true;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (!first) out << ',';
        first = false;
        const auto text = [&](int column) {
            const auto* value = sqlite3_column_text(statement, column);
            return value ? reinterpret_cast<const char*>(value) : "";
        };
        out << "{\"timestamp\":" << sqlite3_column_int64(statement, 0)
            << ",\"type\":" << json_string(text(1))
            << ",\"device_id\":" << json_string(text(2))
            << ",\"detail\":" << json_string(text(3)) << '}';
    }
    sqlite3_finalize(statement);
    out << "]}";
    return out.str();
}

}  // namespace industrial_link
