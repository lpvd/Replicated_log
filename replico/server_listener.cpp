#include "server_listener.h"
#include "replico_server.h"
#include "server_session.h"

ServerListener::ServerListener(RServer* context)
    : m_context(context)
    , acceptor_(net::make_strand(*context->m_ioc))
{
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(context->m_endpoint.protocol(), ec);
    if (ec)
    {
        fail(ec, "open");
        return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec)
    {
        fail(ec, "set_option");
        return;
    }

    // Bind to the server address
    acceptor_.bind(context->m_endpoint, ec);
    if (ec)
    {
        fail(ec, "bind");
        return;
    }

    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        fail(ec, "listen");
        return;
    }
}

void
ServerListener::do_accept()
{
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(*m_context->m_ioc),
        beast::bind_front_handler(&ServerListener::on_accept, shared_from_this()));
}

void
ServerListener::on_accept(beast::error_code ec, tcp::socket socket)
{
    if (ec)
    {
        fail(ec, "accept");
    }
    else
    {
        // Create the ServerSession and run it
        std::make_shared<ServerSession>(std::move(socket), m_context)->run();
    }

    // Accept another connection
    do_accept();
}
