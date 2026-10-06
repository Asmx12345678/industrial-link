#pragma once

#include <stddef.h>

#ifdef _WIN32
#define INDUSTRIAL_LINK_EXPORT __declspec(dllexport)
#else
#define INDUSTRIAL_LINK_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// A deliberately small C ABI keeps plugins compatible across C++ compilers.
INDUSTRIAL_LINK_EXPORT const char* industrial_link_plugin_name(void);
INDUSTRIAL_LINK_EXPORT const char* industrial_link_plugin_version(void);

// Converts a vendor payload into a normalized UTF-8 key=value list.
// Returns the number of bytes written, or a negative value on failure.
INDUSTRIAL_LINK_EXPORT int industrial_link_decode(
    const char* input,
    char* output,
    size_t output_capacity
);

#ifdef __cplusplus
}
#endif

