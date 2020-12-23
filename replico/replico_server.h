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

    std::string m_data;
    size_t m_expected_wc;
    size_t m_actual_wc;
};

class RServer
{
public:
    bool start(int argc, char* argv[]);
    bool stop();

    ~RServer()
    {
        stop();
    }

    std::string m_ip;
    std::string m_port;
    bool m_is_root = false;
    int m_thread_number = 1;

    std::shared_ptr<net::io_context> m_ioc;
    std::vector<std::thread> m_executors;

    tcp::endpoint m_endpoint;
    tcp::endpoint m_root_endpoint;
    std::vector<RNode> m_nodes;

    void
    add_log(std::string log)
    {
        std::lock_guard<std::mutex> g(m_log_lock);
        m_logs.emplace_back(std::move(log));
    }

    size_t
    add_log(std::string log, size_t wc)
    {
        std::lock_guard<std::mutex> g(m_log_lock);
        auto size = m_logs.size();
        m_logs.emplace_back(std::move(log), wc);
        return size;
    }

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

// int
// smain(int argc, char* argv[])
// {
//     // Check command line arguments.
//     if (argc != 5)
//     {
//         std::cerr << "Usage: replico <mode> <rootaddress> <nodeaddress1;nodeaddress2> <threads>"
//                   << std::endl
//                   << "Usage: replico <mode> <nodeadress> <rootaddress> <threads>" << std::endl
//                   << std::endl
//                   << "Example for main:\n"
//                   << "    replico root 0.0.0.0:8080 0.0.0.0:8081;0.0.0.0:8082 1" << std::endl
//                   << "    replico node 0.0.0.0:8081 0.0.0.0:8080 1" << std::endl
//                   << "    replico node 0.0.0.0:8082 0.0.0.0:8080 1" << std::endl;
//         return EXIT_FAILURE;
//     }
//
//     return EXIT_SUCCESS;
// }
