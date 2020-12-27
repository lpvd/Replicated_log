#include "replico_server.h"
#include "log_time_scope.h"
#include <boost/property_tree/json_parser.hpp>
#include "server_session.h"
#include "server_listener.h"

namespace
{
const std::string ROOT = "root";
}  // namespace

bool
RServer::start(int argc, char* argv[])
{
    m_is_root = ROOT == argv[1];

    std::vector<std::string> strs;
    boost::split(strs, argv[2], boost::is_any_of(":"));

    if (strs.size() != 2)
    {
        return false;
    }

    {
        auto ip = net::ip::make_address(strs[0]);
        unsigned short port = std::stoi(strs[1]);

        m_endpoint = tcp::endpoint{ip, port};
    }
    strs.clear();

    boost::split(strs, argv[3], boost::is_any_of(m_is_root ? ";" : ":"));

    if (m_is_root)
    {
        std::vector<std::string> parts;
        for (auto& s : strs)
        {
            boost::split(parts, s, boost::is_any_of(":"));
            auto ip = net::ip::make_address(parts[0]);
            unsigned short port = std::stoi(parts[1]);

            m_nodes.emplace_back(RNode{parts[0], parts[1]});
            parts.clear();
        }
    }
    else
    {
        if (strs.size() != 2)
        {
            return false;
        }

        auto ip = net::ip::make_address(strs[0]);
        unsigned short port = std::stoi(strs[1]);

        m_root_endpoint = tcp::endpoint{ip, port};
    }

    m_thread_number = std::max<int>(1, std::atoi(argv[4]));

    // The io_context is required for all I/O
    m_ioc = std::make_shared<net::io_context>(m_thread_number);

    // Create and launch a listening port
    std::make_shared<ServerListener>(this)->run();

    // Run the I/O service on the requested number of threads

    m_executors.reserve(m_thread_number);
    for (auto i = m_thread_number; i > 0; --i)
    {
        m_executors.emplace_back([this] { m_ioc->run(); });
    }

    return true;
}

bool
RServer::stop()
{
    if (!m_ioc)
    {
        return false;
    }

    for (auto& t : m_executors)
    {
        t.join();
    }

    return true;
}
