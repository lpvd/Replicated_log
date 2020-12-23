#pragma once

#include <string>
#include <chrono>

// Auxiliary struct to automatically print execution time for each request
struct LogTimeScope
{
    typedef std::chrono::high_resolution_clock clock;
    typedef std::chrono::duration<float, std::milli> duration;

    clock::time_point m_begin;
    std::string m_name = 0;
    bool m_isRoot = false;

    LogTimeScope(const std::string& name, bool isRoot);
    ~LogTimeScope();
};
