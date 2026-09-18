#include "config.hpp"
#include "engine.hpp"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/json.hpp>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <semaphore>

namespace gridwise {
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class ConnectionLimiter {
public:
    explicit ConnectionLimiter(std::size_t n) : slots_(static_cast<ptrdiff_t>(n)) {}
    bool try_acquire() { return slots_.try_acquire(); }
    void release() { slots_.release(); }
private:
    std::counting_semaphore<4096> slots_;
};

std::atomic<bool> stopping{false};
std::shared_ptr<tcp::acceptor> global_acceptor;

void stop_handler(int) {
    stopping.store(true, std::memory_order_relaxed);
    if (global_acceptor) {
        boost::system::error_code ec;
        global_acceptor->cancel(ec);
        global_acceptor->close(ec);
    }
}

http::response<http::string_body> json_response(http::status status, const boost::json::value& body, unsigned version, bool keep_alive) {
    http::response<http::string_body> res{status, version};
    res.set(http::field::server, "GridWise-C++/1.0");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(keep_alive);
    res.body() = boost::json::serialize(body);
    res.prepare_payload();
    return res;
}

void write_simple(tcp::socket& socket, http::status status, const boost::json::value& body, unsigned version = 11) {
    beast::error_code ec;
    auto res = json_response(status, body, version, false);
    http::write(socket, res, ec);
}

void session(tcp::socket socket, Engine& engine, ConnectionLimiter& limiter) {
    beast::error_code ec;
    beast::flat_buffer buffer;
    while (!stopping.load(std::memory_order_relaxed)) {
        http::request_parser<http::string_body> parser;
        parser.body_limit(1024 * 1024);
        http::read(socket, buffer, parser, ec);
        if (ec == http::error::end_of_stream) break;
        if (ec) break;
        auto req = parser.release();
        const bool keep_alive = req.keep_alive();

        if (req.method() == http::verb::get && req.target() == "/health") {
            auto res = json_response(http::status::ok, boost::json::object{{"status", "ok"}}, req.version(), keep_alive);
            http::write(socket, res, ec);
        } else if (req.method() == http::verb::post && req.target() == "/optimize-energy") {
            try {
                boost::system::error_code jec;
                const auto body = boost::json::parse(req.body(), jec);
                if (jec) {
                    auto res = json_response(http::status::bad_request, boost::json::object{{"error", "invalid_json"}}, req.version(), keep_alive);
                    http::write(socket, res, ec);
                } else {
                    auto result = engine.run_json(body);
                    auto res = json_response(http::status::ok, result, req.version(), keep_alive);
                    http::write(socket, res, ec);
                }
            } catch (const std::invalid_argument&) {
                auto res = json_response(http::status::bad_request, boost::json::object{{"error", "invalid_request"}}, req.version(), keep_alive);
                http::write(socket, res, ec);
            } catch (const std::exception&) {
                auto res = json_response(http::status::bad_gateway, boost::json::object{{"error", "llm_or_optimization_failure"}}, req.version(), keep_alive);
                http::write(socket, res, ec);
            }
        } else {
            auto res = json_response(http::status::not_found, boost::json::object{{"error", "not_found"}}, req.version(), keep_alive);
            http::write(socket, res, ec);
        }
        if (ec || !keep_alive) break;
    }
    socket.shutdown(tcp::socket::shutdown_send, ec);
    limiter.release();
}

int run_server(const Config& cfg) {
    net::io_context ioc(1);
    tcp::endpoint endpoint;
    boost::system::error_code ec;
    auto addr = net::ip::make_address(cfg.bind_host, ec);
    if (ec) {
        std::cerr << "Invalid bind address\n";
        return 2;
    }
    auto acceptor = std::make_shared<tcp::acceptor>(ioc);
    acceptor->open(tcp::v4(), ec);
    if (ec) return 2;
    acceptor->set_option(net::socket_base::reuse_address(true), ec);
    endpoint = {addr, static_cast<unsigned short>(cfg.port)};
    acceptor->bind(endpoint, ec);
    if (ec) return 2;
    acceptor->listen(net::socket_base::max_listen_connections, ec);
    if (ec) return 2;
    global_acceptor = acceptor;

    Engine engine(cfg);
    ConnectionLimiter limiter(cfg.max_pending);
    net::thread_pool workers(cfg.worker_threads);

    std::cerr << "GridWise C++ listening on " << cfg.bind_host << ':' << cfg.port
              << " with " << cfg.worker_threads << " workers\n";

    while (!stopping.load(std::memory_order_relaxed)) {
        tcp::socket socket(ioc);
        acceptor->accept(socket, ec);
        if (stopping.load(std::memory_order_relaxed)) break;
        if (ec) {
            if (ec == net::error::operation_aborted) break;
            continue;
        }
        if (!limiter.try_acquire()) {
            write_simple(socket, http::status::service_unavailable, boost::json::object{{"error", "server_busy"}});
            boost::system::error_code sec;
            socket.shutdown(tcp::socket::shutdown_both, sec);
            continue;
        }
        net::post(workers, [s = std::move(socket), &engine, &limiter]() mutable {
            session(std::move(s), engine, limiter);
        });
    }

    workers.join();
    global_acceptor.reset();
    return 0;
}

}

int main() {
    std::signal(SIGINT, gridwise::stop_handler);
    std::signal(SIGTERM, gridwise::stop_handler);
    const auto cfg = gridwise::load_config();
    if (cfg.llm_model.empty()) {
        std::cerr << "LLM_MODEL is required\n";
        return 2;
    }
    return gridwise::run_server(cfg);
}