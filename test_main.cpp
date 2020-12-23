#include "gtest/gtest.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

#include "replico_server.h"

namespace
{
std::string
send_log(const std::string& host, const std::string& port, const std::string& log_content)
{
    boost::property_tree::ptree pt;
    boost::property_tree::ptree wc_node, log_node;

    wc_node.put("", 3);
    pt.add_child("wc", wc_node);

    log_node.put("", log_content);
    pt.add_child("data", log_node);
    std::stringstream ss;

    write_json(ss, pt);

    std::string data = ss.str();

    namespace beast = boost::beast;  // from <boost/beast.hpp>
    namespace http = beast::http;    // from <boost/beast/http.hpp>
    namespace net = boost::asio;     // from <boost/asio.hpp>
    using tcp = net::ip::tcp;        // from <boost/asio/ip/tcp.hpp>

    // The io_context is required for all I/O
    net::io_context ioc;

    // These objects perform our I/O
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    beast::error_code ec;
    // Look up the domain name
    auto const results = resolver.resolve(host, port, ec);

    if (ec)
    {
        return "ERROR";
    }
    // Make the connection on the IP address we get from a lookup
    stream.connect(results);

    // Set up an HTTP GET request message
    http::request<http::string_body> req{http::verb::post, "/addlog", 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.body() = data;
    req.content_length(data.size());

    // Send the HTTP request to the remote host
    http::write(stream, req);

    // This buffer is used for reading and must be persisted
    beast::flat_buffer buffer;

    // Declare a container to hold the response
    http::response<http::dynamic_body> res;

    // Receive the HTTP response
    http::read(stream, buffer, res);

    // Gracefully close the socket

    stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    // not_connected happens sometimes
    // so don't bother reporting it.
    //
    if (ec && ec != beast::errc::not_connected)
    {
        return "ERROR";
    }
    return "";
}

std::string
get_logs(const std::string& host, const std::string& port)
{
    namespace beast = boost::beast;  // from <boost/beast.hpp>
    namespace http = beast::http;    // from <boost/beast/http.hpp>
    namespace net = boost::asio;     // from <boost/asio.hpp>
    using tcp = net::ip::tcp;        // from <boost/asio/ip/tcp.hpp>

    // The io_context is required for all I/O
    net::io_context ioc;

    // These objects perform our I/O
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    beast::error_code ec;
    // Look up the domain name
    auto const results = resolver.resolve(host, port, ec);

    if (ec)
    {
        return "";
    }
    // Make the connection on the IP address we get from a lookup
    stream.connect(results);

    // Set up an HTTP GET request message
    http::request<http::string_body> req{http::verb::get, "/getlog", 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    // Send the HTTP request to the remote host
    http::write(stream, req);

    // This buffer is used for reading and must be persisted
    beast::flat_buffer buffer;

    // Declare a container to hold the response
    http::response<http::string_body> res;

    // Receive the HTTP response
    http::read(stream, buffer, res);

    // Gracefully close the socket

    stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    // not_connected happens sometimes
    // so don't bother reporting it.
    //
    if (ec && ec != beast::errc::not_connected)
    {
        return "error";
    }
    return res.body();
}
std::vector<std::string>
parse_and_sort(const std::string& msg)
{
    std::vector<std::string> r;
    boost::split(r, msg, boost::is_any_of("\n"));
    r.erase(std::remove(r.begin(), r.end(), ""), r.end());

    std::sort(r.begin(), r.end());
    return r;
}

}  // namespace

TEST(ReplicoTests, HappyPath)
{
    RServer root_server;

    char* root_args[] = {"", "root", "127.0.0.1:8081", "127.0.0.1:8082;127.0.0.1:8083", "4"};

    ASSERT_TRUE(root_server.start(sizeof(root_args) / sizeof(char*), root_args));

    RServer node1_server;

    char* node1_args[] = {"", "node", "127.0.0.1:8082", "127.0.0.1:8081", "4"};

    ASSERT_TRUE(node1_server.start(sizeof(node1_args) / sizeof(char*), node1_args));

    RServer node2_server;

    char* node2_args[] = {"", "node", "127.0.0.1:8083", "127.0.0.1:8081", "4"};

    ASSERT_TRUE(node2_server.start(sizeof(node2_args) / sizeof(char*), node2_args));

    std::vector<std::string> expected;
    for (int i = 0; i < 10; ++i)
    {
        auto log = "log with id i : " + std::to_string(i);
        auto res = send_log("127.0.0.1", "8081", log);
        expected.push_back(log);
    }

    std::this_thread::sleep_for(std::chrono::seconds(60));

    auto root_logs = parse_and_sort(get_logs("127.0.0.1", "8081"));
    ASSERT_EQ(expected, root_logs);



    auto node1_logs = parse_and_sort(get_logs("127.0.0.1", "8082"));
    ASSERT_EQ(expected, node1_logs);

    auto node2_logs = parse_and_sort(get_logs("127.0.0.1", "8083"));
    ASSERT_EQ(expected, node2_logs);
}
