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

// Auxiliary struct to automatically print execution time for each request
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
    tcp::resolver m_resolver;
    beast::tcp_stream m_stream;
    beast::flat_buffer m_buffer;  // (Must persist between reads)
    http::request<http::string_body> m_req;
    http::response<http::string_body> m_res;
    RServer* m_context;
    size_t m_id;

public:
    // Objects are constructed with a strand to
    // ensure that handlers do not execute concurrently.
    explicit ClientSession(RServer* context);

    // Start the asynchronous operation
    void
    run(const std::string& host,
        const std::string& port,
        const std::string& target,
        std::string body,
        size_t id)
    {
        // Set up an HTTP GET request message
        m_id = id;
        m_req.version(11);
        m_req.method(http::verb::post);
        m_req.target(target);
        m_req.set(http::field::host, host);
        m_req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        auto size = body.size();
        m_req.body() = std::move(body);
        m_req.content_length(size);

        // Look up the domain name
        m_resolver.async_resolve(
            host, port, beast::bind_front_handler(&ClientSession::on_resolve, shared_from_this()));
    }

    void
    on_resolve(beast::error_code ec, tcp::resolver::results_type results)
    {
        if (ec)
            return fail(ec, "resolve");

        // Set a timeout on the operation
        m_stream.expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        m_stream.async_connect(
            results, beast::bind_front_handler(&ClientSession::on_connect, shared_from_this()));
    }

    void
    on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        if (ec)
            return fail(ec, "connect");

        // Set a timeout on the operation
        m_stream.expires_after(std::chrono::seconds(30));

        // Send the HTTP request to the remote host
        http::async_write(m_stream, m_req,
                          beast::bind_front_handler(&ClientSession::on_write, shared_from_this()));
    }

    void
    on_write(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        // Receive the HTTP response
        http::async_read(m_stream, m_buffer, m_res,
                         beast::bind_front_handler(&ClientSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred);
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
