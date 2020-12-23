#include "replico_server.h"
#include "client_session.h"
#include "log_time_scope.h"
#include "server_session.h"
#include <random>
#include <boost/property_tree/json_parser.hpp>


namespace
{
std::uniform_int_distribution<int> dice_distribution(1, 10);
std::mt19937 random_number_engine;  // pseudorandom number generator
auto rgen = std::bind(dice_distribution, random_number_engine);
}  // namespace


extern std::mutex STDOUT_LOCK;

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
            boost::property_tree::ptree pt;
            std::stringstream ss;
            ss << req.body();
            read_json(ss, pt);

            auto log = pt.get<std::string>("data");
            auto wc = pt.get<size_t>("wc");
            auto id = context->add_log(log, wc);

            body = "CONGRATS!";
            size_t wc_idx = 0;

            // Schedule write to secondaries
            for (auto& n : context->m_nodes)
            {
                if (wc_idx >= wc)
                {
                    break;
                }
                std::make_shared<ClientSession>(context)->run(n.ip, n.port, "/addlog", log, id);
                ++wc_idx;
            }
        }
        else if (isGet)
        {
            auto reg = context->get_logs();
            for (auto& l : reg)
            {
                body += l.m_data;
                body += " [" + std::to_string(l.m_actual_wc) + "/" +
                        std::to_string(l.m_expected_wc) + "]";
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
            auto reg = context->get_logs();
            for (auto& l : reg)
            {
                body += l.m_data;
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
