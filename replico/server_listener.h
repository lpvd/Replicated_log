#pragma once

#include <boost/beast/core.hpp>
#include <boost/asio/strand.hpp>


namespace beast = boost::beast;    // from <boost/beast.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

class RServer;

// Accepts incoming connections and launches the ServerSessions
class ServerListener : public std::enable_shared_from_this<ServerListener>
{
    tcp::acceptor acceptor_;
    RServer* m_context;

public:
    ServerListener(RServer* context);

    // Start accepting incoming connections
    void
        run()
    {
        do_accept();
    }

private:
    void do_accept();

    void on_accept(beast::error_code ec, tcp::socket socket);
};

