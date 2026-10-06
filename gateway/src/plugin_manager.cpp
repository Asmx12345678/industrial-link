#include "industrial_link/plugin_manager.hpp"

#include <array>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace industrial_link {

namespace {
#ifdef _WIN32
void* open_library(const std::string& path) { return LoadLibraryA(path.c_str()); }
void close_library(void* handle) { if (handle) FreeLibrary(static_cast<HMODULE>(handle)); }
void* load_symbol(void* handle, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
}
std::string library_error() { return "LoadLibrary/GetProcAddress failed"; }
#else
void* open_library(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void close_library(void* handle) { if (handle) dlclose(handle); }
void* load_symbol(void* handle, const char* name) { return dlsym(handle, name); }
std::string library_error() { const char* e = dlerror(); return e ? e : "dlopen/dlsym failed"; }
#endif
}  // namespace

PluginManager::~PluginManager() {
    for (auto& entry : plugins_) close_library(entry.second.handle);
}

bool PluginManager::load(const std::string& path, std::string& error) {
    void* handle = open_library(path);
    if (!handle) {
        error = library_error();
        return false;
    }
    using TextFn = const char* (*)();
    const auto name_fn = reinterpret_cast<TextFn>(load_symbol(handle, "industrial_link_plugin_name"));
    const auto version_fn = reinterpret_cast<TextFn>(load_symbol(handle, "industrial_link_plugin_version"));
    const auto decode_fn = reinterpret_cast<int (*)(const char*, char*, std::size_t)>(
        load_symbol(handle, "industrial_link_decode"));
    if (!name_fn || !version_fn || !decode_fn) {
        error = "missing required plugin symbol: " + library_error();
        close_library(handle);
        return false;
    }
    const std::string name = name_fn() ? name_fn() : "";
    if (name.empty()) {
        error = "plugin name is empty";
        close_library(handle);
        return false;
    }
    if (plugins_.count(name)) {
        error = "duplicate plugin name: " + name;
        close_library(handle);
        return false;
    }
    plugins_.emplace(name, Plugin{handle, version_fn() ? version_fn() : "unknown", decode_fn});
    return true;
}

bool PluginManager::decode(const std::string& name, const std::string& input,
                           std::string& output, std::string& error) const {
    const auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        error = "plugin not loaded: " + name;
        return false;
    }
    std::array<char, 8192> buffer{};
    const int written = it->second.decode_fn(input.c_str(), buffer.data(), buffer.size());
    if (written < 0 || static_cast<std::size_t>(written) >= buffer.size()) {
        error = "plugin decode failed";
        return false;
    }
    output.assign(buffer.data(), static_cast<std::size_t>(written));
    return true;
}

std::vector<std::string> PluginManager::names() const {
    std::vector<std::string> result;
    result.reserve(plugins_.size());
    for (const auto& entry : plugins_) result.push_back(entry.first + "@" + entry.second.version);
    return result;
}

}  // namespace industrial_link

