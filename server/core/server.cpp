//
// Copyright (c) 2018-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/BeastLounge
//
 
#include "channel.hpp"
#include "channel_list.hpp"
#include "listener.hpp"
#include "logger.hpp"
#include "server.hpp"
#include "service.hpp"
#include "utility.hpp"
#include <boost/json.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/basic_signal_set.hpp>
#include <boost/assert.hpp>
#include <boost/make_unique.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

//------------------------------------------------------------------------------

extern
void
make_blackjack_service(server&);

extern
std::unique_ptr<channel_list>
make_channel_list(server&);

extern
void
make_system_channel(server&);

//------------------------------------------------------------------------------

namespace boost {
namespace json {

    static
    net::ip::address
    tag_invoke(
            value_to_tag<net::ip::address>,
            json::value const& jv)
    {
        return net::ip::make_address(jv.as_string().c_str());
    }

} // json
} // boost

listener_config::
listener_config(json::value&& jv)
    : name(std::move(jv.at("name").as_string()))
    , address(json::value_to<net::ip::address>(jv.at("address")))
    , port_num(static_cast<unsigned short>(jv.at("port_num").as_int64()))
{
}

//------------------------------------------------------------------------------

namespace {

struct server_config
{
    unsigned num_threads = 1;
    json::string doc_root;

    server_config() = default;

    explicit
    server_config(json::value&& jv)
        : num_threads(static_cast<unsigned>(jv.at("threads").as_int64()))
        , doc_root(jv.at("doc-root").as_string())
    {
        if( num_threads < 1)
            num_threads = 1;
    }
};

//------------------------------------------------------------------------------

json::value
parse_file(
    char const* path,
    beast::error_code& ec)
{
    ec = {};
    auto const& gc =
        boost::system::generic_category();
  
    struct cleanup
    {
        FILE* f;
        ~cleanup()
        {
            ::fclose(f);
        }
    };

    auto f = ::fopen(path, "rb");
    if(! f)
    {
        ec = beast::error_code(errno, gc);
        return nullptr;
    }
    cleanup c{f};
    std::size_t result;
    result = ::fseek(f, 0, SEEK_END);
    if(result != 0)
    {
        ec = beast::error_code(errno, gc);
        return nullptr;
    }
    auto const size = ::ftell(f);
    if(size == -1L)
    {
        ec = beast::error_code(errno, gc);
        return nullptr;
    }
    result = ::fseek(f, 0, SEEK_SET);
    if(result != 0)
    {
        ec = beast::error_code(errno, gc);
        return nullptr;
    }
    char* buf = new char[size];
    auto nread = ::fread(buf, 1, size, f);
    if(std::ferror(f))
    {
        ec = beast::error_code(errno, gc);
        return nullptr;
    }

    auto jv = json::parse({ buf, nread }, ec);
    delete[] buf;
    return jv;
}

//------------------------------------------------------------------------------

class server_impl_base : public server
{
public:
    net::io_context ioc_;

    // This function is in a base class because `server_impl`
    // needs to call it from the ctor-initializer list, which
    // would be undefined if the member function was in the
    // derived class.

    executor_type
    make_executor() override
    {
    #ifdef LOUNGE_USE_SYSTEM_EXECUTOR
        return net::make_strand(
            net::system_executor{});
    #else
        return net::make_strand(
            ioc_.get_executor());
    #endif
    }
};

class server_impl
    : public server_impl_base
{
    using clock_type = std::chrono::steady_clock;
    using time_point = clock_type::time_point;

    server_config cfg_;
    std::unique_ptr<logger> log_;
    std::vector<std::unique_ptr<service>> services_;
    net::basic_waitable_timer<
        clock_type,
        boost::asio::wait_traits<clock_type>,
        executor_type> timer_;
    asio::basic_signal_set<executor_type> signals_;
    std::condition_variable cv_;
    std::mutex mutex_;
    time_point shutdown_time_;
    bool running_ = false;
    std::atomic<bool> stop_;

    std::unique_ptr<::channel_list> channel_list_;

    static
    std::chrono::steady_clock::time_point
    never() noexcept
    {
        return (time_point::max)();
    }

public:
    explicit
    server_impl(
        server_config cfg,
        std::unique_ptr<logger> log)
        : cfg_(std::move(cfg))
        , log_(std::move(log))
        , timer_(this->make_executor())
        , signals_(
            timer_.get_executor(),
            SIGINT,
            SIGTERM)
        , shutdown_time_(never())
        , stop_(false)
        , channel_list_(make_channel_list(*this))
    {
        timer_.expires_at(never());

        make_system_channel(*this);
    }

    ~server_impl()
    {
    }

    void
    insert(std::unique_ptr<service> sp) override
    {
        if(running_)
            throw std::logic_error(
                "server already running");

        services_.emplace_back(std::move(sp));
    }

    void
    run() override
    {
        if(running_)
            throw std::logic_error(
                "server already running");

        running_ = true;

        // Start all agents
        for(auto const& sp : services_)
            sp->on_start();

        // Capture SIGINT and SIGTERM to perform a clean shutdown
        signals_.async_wait(
            beast::bind_front_handler(
                &server_impl::on_signal,
                this));

    #ifndef LOUNGE_USE_SYSTEM_EXECUTOR
        std::vector<std::thread> vt;
        while(vt.size() < cfg_.num_threads)
            vt.emplace_back(
                [this]
                {
                    this->ioc_.run();
                });
    #endif
        // Block the main thread until stop() is called
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]{ return stop_.load(); });
        }

        // Notify all agents to stop
        auto agents = std::move(services_);
        for(auto const& sp : agents)
            sp->on_stop();

        // services must be kept alive until after
        // all executor threads are joined.

        // If we get here, then the server has
        // stopped, so join the threads before
        // destroying them.

    #ifdef LOUNGE_USE_SYSTEM_EXECUTOR
        net::system_executor{}.context().join();
    #else
        for(auto& t : vt)
            t.join();
    #endif
    }

    //--------------------------------------------------------------------------
    //
    // shutdown / stop
    //
    //--------------------------------------------------------------------------

    bool
    is_shutting_down() override
    {
        return stop_.load();
    }

    void
    shutdown(std::chrono::seconds cooldown) override
    {
        // Get on the strand
        if(! timer_.get_executor().running_in_this_thread())
            return net::post(
                timer_.get_executor(),
                beast::bind_front_handler(
                    &server_impl::shutdown,
                    this,
                    cooldown));

        // Only callable once
        if(timer_.expiry() != never())
            return;

        shutdown_time_ = clock_type::now() + cooldown;
        on_timer();
    }

    void
    on_timer(beast::error_code ec = {})
    {
        if(ec == net::error::operation_aborted)
            return;

        auto const remain =
            ::ceil<std::chrono::seconds>(
                shutdown_time_ - clock_type::now());

        // Countdown finished?
        if(remain.count() <= 0)
        {
            stop();
            return;
        }

        std::chrono::seconds amount(remain.count());
        if(amount.count() > 10)
            amount = std::chrono::seconds(10);

        // Notify users of impending shutdown
        auto c = this->channel_list().at(1);
        json::value jv(json::object_kind);
        auto& obj = jv.get_object();
        obj["verb"] = "say";
        obj["cid"] = c->cid();
        obj["name"] = c->name();
        obj["message"] = "Server is shutting down in " +
            std::to_string(remain.count()) + " seconds";
        c->send(jv);
        timer_.expires_after(amount);
        timer_.async_wait(
            beast::bind_front_handler(
                &server_impl::on_timer,
                this));
    }

    void
    on_signal(beast::error_code ec, int signum)
    {
        if(ec == net::error::operation_aborted)
            return;

        log_->cerr() <<
            "server_impl::on_signal: #" <<
            signum << ", " << ec.message() << "\n";
        if(timer_.expiry() == never())
        {
            // Capture signals again
            signals_.async_wait(
                beast::bind_front_handler(
                    &server_impl::on_signal,
                    this));

            this->shutdown(std::chrono::seconds(30));
        }
        else
        {
            // second time hard stop
            stop();
        }
    }

    void
    stop() override
    {
        // Get on the strand
        if(! timer_.get_executor().running_in_this_thread())
            return net::post(
                timer_.get_executor(),
                beast::bind_front_handler(
                    &server_impl::stop,
                    this));

        // Only callable once
        if(stop_)
            return;

        // Set stop_ and unblock the main thread
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            cv_.notify_all();
        }

        // Cancel our outstanding I/O
        timer_.cancel();
        beast::error_code ec;
        signals_.cancel(ec);
    }

    //--------------------------------------------------------------------------

    beast::string_view
    doc_root() const override
    {
        return cfg_.doc_root;
    }

    logger&
    log() noexcept override
    {
        return *log_;
    }

    ::channel_list&
    channel_list() override
    {
        return *channel_list_;
    }
};

} // (anon)

//------------------------------------------------------------------------------

std::unique_ptr<server>
make_server(
    char const* config_path,
    std::unique_ptr<logger> log)
{
    beast::error_code ec;

    // Read the JSON configuration file
    json::value jv = parse_file(config_path, ec);
    if(ec)
    {
        log->cerr() <<
            "json::parse_file: " << ec.message() << "\n";
        return nullptr;
    }

    // Read the log configuration
    {
        logger_config cfg;
        try
        {
            cfg = logger_config(std::move(jv));
            if(! log->open(std::move(cfg)))
                return nullptr;
        }
        catch(beast::system_error const& e)
        {
            log->cerr() <<
                "logger_config: " << e.code().message() << "\n";
            return nullptr;
        }
    }

    // Read the server configuration
    std::unique_ptr<server_impl> srv;
    {
        if( ! jv.is_object() ||
            ! jv.get_object().contains("server") ||
            ! jv.get_object()["server"].is_object())
        {
            ec = json::error::value_is_scalar; // TODO REVIEW
            log->cerr() <<
                "server_config: " << ec.message() << "\n";
            return nullptr;
        }
        try
        {
            auto& jo = jv.get_object()["server"];
            server_config cfg(std::move(jo));

            // Create the server
            srv = boost::make_unique<server_impl>(
                std::move(cfg),
                std::move(log));
        }
        catch(beast::system_error const& e)
        {
            log->cerr() <<
                "server_config: " << e.code().message() << "\n";
            return nullptr;
        }

    }

    // Add services
    make_blackjack_service(*srv);

    // Create listeners
    {
        if( ! jv.is_object() ||
            ! jv.get_object().contains("listeners") ||
            ! jv.get_object()["listeners"].is_array())
        {
            ec = json::error::value_is_scalar; // TODO REVIEW
            return nullptr;
        }
        for(auto& e :
            jv.get_object()["listeners"].get_array())
        {
            try
            {
                if(! run_listener(*srv, listener_config(std::move(e))))
                    return nullptr;
            }
            catch(beast::system_error const& e)
            {
                srv->log().cerr() <<
                    "listener_config: " << e.code().message() << "\n";
                return nullptr;
            }
        }
    }

    return srv;
}

