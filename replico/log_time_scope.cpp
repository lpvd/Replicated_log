#include "log_time_scope.h"
#include <mutex>
#include <iostream>

std::mutex STDOUT_LOCK;

LogTimeScope::LogTimeScope(const std::string& name, bool isRoot)
    : m_begin(clock::now())
    , m_name(name)
    , m_isRoot(isRoot)
{
}

LogTimeScope::~LogTimeScope()
{
    duration dur = clock::now() - m_begin;
    {
        std::lock_guard<std::mutex> lock(STDOUT_LOCK);
        std::cout << (m_isRoot ? "[ROOT]" : "[NODE]") << " Executing " << m_name << " : "
                  << dur.count() << std::endl;
    }
}

