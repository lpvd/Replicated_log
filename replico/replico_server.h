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

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
namespace net = boost::asio;       // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.

// Report a failure
void fail(beast::error_code ec, char const* what);

class RServer;

struct LogTimeScope
{
    typedef std::chrono::high_resolution_clock clock;
    typedef std::chrono::duration<float, std::milli> duration;

    clock::time_point m_begin;
    std::string m_name = 0;
    bool m_isRoot = false;

    LogTimeScope(const std::string& name, bool isRoot)
        : m_begin(clock::now())
        , m_name(name)
        , m_isRoot(isRoot)
    {
    }

    ~LogTimeScope();
};

class ClientSession : public std::enable_shared_from_this<ClientSession>
{
    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;  // (Must persist between reads)
    http::request<http::string_body> req_;
    http::response<http::string_body> res_;

public:
    // Objects are constructed with a strand to
    // ensure that handlers do not execute concurrently.
    explicit ClientSession(net::io_context& ioc)
        : resolver_(net::make_strand(ioc))
        , stream_(net::make_strand(ioc))
    {
    }

    // Start the asynchronous operation
    void
    run(const std::string& host,
        const std::string& port,
        const std::string& target,
        std::string body)
    {
        // Set up an HTTP GET request message
        req_.version(11);
        req_.method(http::verb::post);
        req_.target(target);
        req_.set(http::field::host, host);
        req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        auto size = body.size();
        req_.body() = std::move(body);
        req_.content_length(size);

        // Look up the domain name
        resolver_.async_resolve(
            host, port, beast::bind_front_handler(&ClientSession::on_resolve, shared_from_this()));
    }

    void
    on_resolve(beast::error_code ec, tcp::resolver::results_type results)
    {
        if (ec)
            return fail(ec, "resolve");

        // Set a timeout on the operation
        stream_.expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        stream_.async_connect(
            results, beast::bind_front_handler(&ClientSession::on_connect, shared_from_this()));
    }

    void
    on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        if (ec)
            return fail(ec, "connect");

        // Set a timeout on the operation
        stream_.expires_after(std::chrono::seconds(30));

        // Send the HTTP request to the remote host
        http::async_write(stream_, req_,
                          beast::bind_front_handler(&ClientSession::on_write, shared_from_this()));
    }

    void
    on_write(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        // Receive the HTTP response
        http::async_read(stream_, buffer_, res_,
                         beast::bind_front_handler(&ClientSession::on_read, shared_from_this()));
    }

    void
    on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "read");

        // Gracefully close the socket
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec);

        // not_connected happens sometimes so don't bother reporting it.
        if (ec && ec != beast::errc::not_connected)
            return fail(ec, "shutdown");

        // If we get here then the connection is closed gracefully
    }
};

// Handles an HTTP server connection
class ServerSession : public std::enable_shared_from_this<ServerSession>
{
public:
    // This is the C++11 equivalent of a generic lambda.
    // The function object is used to send an HTTP message.
    struct send_lambda
    {
        ServerSession& self_;

        explicit send_lambda(ServerSession& self)
            : self_(self)
        {
        }

        template <bool isRequest, class Body, class Fields>
        void
        operator()(http::message<isRequest, Body, Fields>&& msg) const
        {
            // The lifetime of the message has to extend
            // for the duration of the async operation so
            // we use a shared_ptr to manage it.
            auto sp = std::make_shared<http::message<isRequest, Body, Fields>>(std::move(msg));

            // Store a type-erased version of the shared
            // pointer in the class to keep it alive.
            self_.res_ = sp;

            // Write the response
            http::async_write(self_.stream_, *sp,
                              beast::bind_front_handler(&ServerSession::on_write,
                                                        self_.shared_from_this(), sp->need_eof()));
        }
    };

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<void> res_;
    send_lambda lambda_;
    RServer* m_context;

public:
    // Take ownership of the stream
    ServerSession(tcp::socket&& socket, RServer* context)
        : stream_(std::move(socket))
        , lambda_(*this)
        , m_context(context)
    {
    }

    // Start the asynchronous operation
    void
    run()
    {
        do_read();
    }

    void
    do_read()
    {
        // Make the request empty before reading,
        // otherwise the operation behavior is undefined.
        req_ = {};

        // Set the timeout.
        stream_.expires_after(std::chrono::seconds(30));

        // Read a request
        http::async_read(stream_, buffer_, req_,
                         beast::bind_front_handler(&ServerSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred);

    void
    on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        if (close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return do_close();
        }

        // We're done with the response so delete it
        res_ = nullptr;

        // Read another request
        do_read();
    }

    void
    do_close()
    {
        // Send a TCP shutdown
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }
};

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

struct RNode
{
    std::string ip;
    std::string port;
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
        m_logs.push_back(std::move(log));
    }

    std::vector<std::string>
    get_logs()
    {
        std::lock_guard<std::mutex> g(m_log_lock);
        return m_logs;
    }

    std::mutex m_log_lock;
    std::vector<std::string> m_logs;
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
