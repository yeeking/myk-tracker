#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
 #include <windows.h>
#elif defined(__APPLE__)
 #include <mach-o/dyld.h>
 #include <limits.h>
#elif defined(__linux__)
 #include <unistd.h>
 #include <limits.h>
#else
 #error "Unsupported platform"
#endif

/** Return the path to the current binary. Useful for locating the ui folder when running locally. */
inline std::filesystem::path getBinary()
{
#if defined(_WIN32)
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        throw std::runtime_error("Failed to get executable path on Windows");
    return std::filesystem::path(path).parent_path();
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
        throw std::runtime_error("Buffer too small for _NSGetExecutablePath");
    return std::filesystem::canonical(std::filesystem::path(path)).parent_path();
#elif defined(__linux__)
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    if (count == -1)
        throw std::runtime_error("Failed to read /proc/self/exe");
    return std::filesystem::path(std::string(path, static_cast<size_t>(count))).parent_path();
#endif
}
