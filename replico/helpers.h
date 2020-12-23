#pragma once

#include "replico_server.h"
#include "server_session.h"

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

class RServer;

// Report a failure
void fail(beast::error_code ec, char const* what);
void handle_request(http::request<http::string_body>&& req,
                    ServerSession::send_lambda& send,
                    RServer* context,
                    tcp::endpoint remote_endpoint);
