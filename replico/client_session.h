#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

class RServer;

// One ClientSession per request from master to secondary.
// Asynchronous. Once the session receive responce -
// it updates wc of the log message in master logs container
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
    explicit ClientSession(RServer* context);

    // Start the asynchronous operation
    void run(const std::string& host,
             const std::string& port,
             const std::string& target,
             std::string body,
             size_t id);

    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
};
