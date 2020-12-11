#include "replico_server.h"
#include <random>

namespace
{
const std::string ROOT = "root";
std::mutex STDOUT_LOCK;

std::uniform_int_distribution<int> dice_distribution(1, 10);
std::mt19937 random_number_engine;  // pseudorandom number generator
auto rgen = std::bind(dice_distribution, random_number_engine);
}  // namespace

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
handle_request(http::request<http::string_body>&& req,
               ServerSession::send_lambda& send,
               RServer* context,
               tcp::endpoint remote_endpoint)
{
    // Returns a bad request response
    auto const bad_request = [&req](beast::string_view why) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    LogTimeScope g(__FUNCTION__, context->m_is_root);

    auto prefix = req.target();

    std::string body;
    auto isAdd = req.method() == http::verb::post && "/addlog" == prefix;
    auto isGet = req.method() == http::verb::get && prefix == "/getlog";

    if (context->m_is_root)
    {
        if (isAdd)
        {
            std::string log = req.body();
            {
                context->add_log(log);
            }
            body = "CONGRATS!";
            for (auto& n : context->m_nodes)
            {
                std::make_shared<ClientSession>(*context->m_ioc)->run(n.ip, n.port, "/addlog", log);
            }
        }
        else if (isGet)
        {
            std::vector<std::string> reg = context->get_logs();
            for (auto& l : reg)
            {
                body += l;
                body += "\n";
            }
        }
        else
        {
            return send(bad_request("Illegal request-target"));
        }
    }
    else
    {
        if (isAdd && remote_endpoint.address() == context->m_root_endpoint.address())
        {
            std::this_thread::sleep_for(std::chrono::seconds(rgen()));
            std::string log = req.body();
            context->add_log(log);
        }
        else if (isGet)
        {
            std::vector<std::string> reg = context->get_logs();
            for (auto& l : reg)
            {
                body += l;
                body += "\n";
            }
        }
        else
        {
            return send(bad_request("Illegal request-target"));
        }
    }

    // Cache the size since we need it after the move
    auto const size = body.size();

    http::response<http::string_body> res{std::piecewise_construct,
                                          std::make_tuple(std::move(body)),
                                          std::make_tuple(http::status::ok, req.version())};

    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.content_length(size);

    res.keep_alive(req.keep_alive());
    return send(std::move(res));
}

void
fail(beast::error_code ec, char const* what)
{
    std::lock_guard<std::mutex> lock(STDOUT_LOCK);
    std::cerr << what << ": " << ec.message() << "\n";
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

    boost::split(strs, argv[3], boost::is_any_of(m_is_root ? ";" : ":"));

    if (m_is_root)
    {
        std::vector<std::string> loc;
        for (auto& s : strs)
        {
            boost::split(loc, s, boost::is_any_of(":"));
            auto ip = net::ip::make_address(loc[0]);
            unsigned short port = std::stoi(loc[1]);

            m_nodes.emplace_back(RNode{loc[0], loc[1]});
            loc.clear();
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

    m_ioc->stop();
    for (auto& t : m_executors)
    {
        t.join();
    }

    return true;
}

void
ServerSession::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
        return do_close();

    if (ec)
        return fail(ec, "read");

    // Send the response
    handle_request(std::move(req_), lambda_, m_context, stream_.socket().remote_endpoint());
}

LogTimeScope::~LogTimeScope()
{
    duration dur = clock::now() - m_begin;
    {
        std::lock_guard<std::mutex> lock(STDOUT_LOCK);
        std::cout << (m_isRoot ? "[ROOT]" : "[NODE]") << " Executing " << m_name << " : "
                  << dur.count() << std::endl;
    }
}
