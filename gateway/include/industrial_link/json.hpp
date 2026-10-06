#pragma once

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

namespace industrial_link {

inline std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(ch) << std::dec;
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    return out.str();
}

inline std::string json_string(const std::string& value) {
    return "\"" + json_escape(value) + "\"";
}

inline std::string extract_json_string(const std::string& body, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    std::size_t pos = body.find(token);
    if (pos == std::string::npos) return {};
    pos = body.find(':', pos + token.size());
    if (pos == std::string::npos) return {};
    pos = body.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos;
    std::string result;
    bool escaped = false;
    for (; pos < body.size(); ++pos) {
        const char ch = body[pos];
        if (escaped) {
            switch (ch) {
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(ch); break;
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            break;
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

}  // namespace industrial_link

