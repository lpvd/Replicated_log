#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include "helpers.h"

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace net = boost::asio;       // from <boost/asio.hpp>

using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

class RServer;

// Aux structure to be used by main (master) to store data about secondaries
struct RNode
{
    std::string ip;
    std::string port;
};

struct LogEntry
{
    LogEntry(std::string data, size_t expected_wc)
        : m_data(std::move(data))
        , m_expected_wc(expected_wc)
        , m_actual_wc(1)
    {
    }

    LogEntry(std::string data)
        : m_data(std::move(data))
        , m_expected_wc(0)
        , m_actual_wc(0)
    {
    }

    // The log message itself
    std::string m_data;

    // Expected write concern
    size_t m_expected_wc;

    // Number of successful posts to secondaries
    size_t m_actual_wc;
};

// One node from cluster.
// Can be master or secondary.
// Communication between user and secondary: http
// Communication between main and secondary nodes: http
class RServer
{
public:
    bool start(int argc, char* argv[]);
    bool stop();

    ~RServer()
    {
        stop();
    }

    bool m_is_root = false;
    int m_thread_number = 1;

    std::shared_ptr<net::io_context> m_ioc;
    std::vector<std::thread> m_executors;

    // Endpoint of this object
    tcp::endpoint m_endpoint;

    // For secondary - master's endpoint
    tcp::endpoint m_root_endpoint;

    // For master - secondaries
    std::vector<RNode> m_nodes;

    void
    add_log(std::string log)
    {
        std::lock_guard<std::mutex> g(m_log_lock);
        m_logs.emplace_back(std::move(log));
        std::cout << "Added log: " << log << std::endl;
    }

    size_t
    add_log(std::string log, size_t wc)
    {
        std::lock_guard<std::mutex> g(m_log_lock);
        auto size = m_logs.size();
        m_logs.emplace_back(std::move(log), wc);
        return size;
    }

    // Update write concern
    // If request was successful - update wc for each log
    void
    update_wc(size_t id)
    {
        std::lock_guard<std::mutex> g(m_log_lock);
        ++m_logs[id].m_actual_wc;
    }

    std::vector<LogEntry>
    get_logs()
    {
        std::lock_guard<std::mutex> g(m_log_lock);
        return m_logs;
    }

    std::mutex m_log_lock;
    std::vector<LogEntry> m_logs;
};
