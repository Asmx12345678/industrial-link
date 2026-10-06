#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace industrial_link {

class PluginManager {
public:
    PluginManager() = default;
    ~PluginManager();
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    bool load(const std::string& path, std::string& error);
    bool decode(const std::string& name, const std::string& input,
                std::string& output, std::string& error) const;
    std::vector<std::string> names() const;

private:
    struct Plugin {
        void* handle{};
        std::string version;
        int (*decode_fn)(const char*, char*, std::size_t){};
    };
    std::unordered_map<std::string, Plugin> plugins_;
};

}  // namespace industrial_link

