#include "client_session.h"
#include "replico_server.h"
#include "helpers.h"
#include <random>
#include <boost/property_tree/json_parser.hpp>
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

ClientSession::ClientSession(RServer* context)
    : m_resolver(net::make_strand(*context->m_ioc))
    , m_stream(net::make_strand(*context->m_ioc))
    , m_context(context)
{
}

void
ClientSession::run(const std::string& host,
                   const std::string& port,
                   const std::string& target,
                   std::string body,
                   size_t id)
{
    // Set up an HTTP GET request message
    m_id = id;
    m_req.version(11);
    m_req.method(http::verb::post);

    // command (addlog)
    m_req.target(target);
    m_req.set(http::field::host, host);
    m_req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    // body - the message itself
    auto size = body.size();
    m_req.body() = std::move(body);
    m_req.content_length(size);

    // Look up the domain name
    m_resolver.async_resolve(
        host, port, beast::bind_front_handler(&ClientSession::on_resolve, shared_from_this()));
}

void
ClientSession::on_resolve(beast::error_code ec, tcp::resolver::results_type results)
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
ClientSession::on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
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
ClientSession::on_write(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if (ec)
        return fail(ec, "write");

    // Receive the HTTP response
    http::async_read(m_stream, m_buffer, m_res,
                     beast::bind_front_handler(&ClientSession::on_read, shared_from_this()));
}

void
ClientSession::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if (ec)
        return fail(ec, "read");

    // Gracefully close the socket
    m_stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    // Update wc of the log in main node
    m_context->update_wc(m_id);

    // not_connected happens sometimes so don't bother reporting it.
    if (ec && ec != beast::errc::not_connected)
        return fail(ec, "shutdown");

    // If we get here then the connection is closed gracefully
}
