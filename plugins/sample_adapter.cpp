#include "industrial_link/plugin_api.h"

#include <cstdio>
#include <cstring>
#include <string>

// Example vendor frame: TEMP:23.5|VOLT:24.1|STATE:1
extern "C" {

INDUSTRIAL_LINK_EXPORT const char* industrial_link_plugin_name(void) {
    return "vendor_pipe";
}

INDUSTRIAL_LINK_EXPORT const char* industrial_link_plugin_version(void) {
    return "1.0.0";
}

INDUSTRIAL_LINK_EXPORT int industrial_link_decode(
    const char* input, char* output, size_t output_capacity) {
    if (!input || !output || output_capacity == 0) return -1;
    std::string normalized(input);
    for (char& ch : normalized) {
        if (ch == '|') ch = ';';
        else if (ch == ':') ch = '=';
    }
    if (normalized.size() + 1 > output_capacity) return -2;
    std::memcpy(output, normalized.data(), normalized.size());
    output[normalized.size()] = '\0';
    return static_cast<int>(normalized.size());
}

}  // extern "C"

