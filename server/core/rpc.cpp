//
// Copyright (c) 2018-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/BeastLounge
//

#include "rpc.hpp"
#include "user.hpp"
#include <boost/beast/core/error.hpp>
#include <type_traits>

//------------------------------------------------------------------------------

namespace {

class rpc_error_codes : public beast::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "beast-lounge";
    }

    std::string
    message(int ev) const override
    {
        switch(static_cast<rpc_code>(ev))
        {
        case rpc_code::parse_error: return
            "An error occurred on the server while parsing the JSON text.";
        case rpc_code::invalid_request: return
            "The JSON sent is not a valid Request object";
        case rpc_code::method_not_found: return
            "The method does not exist or is not available";
        case rpc_code::invalid_params: return
            "Invalid method parameters";
        case rpc_code::internal_error: return
            "Internal JSON-RPC error";

        case rpc_code::expected_object: return
            "Expected object in JSON-RPC request";
        case rpc_code::expected_string_version: return
            "Expected string version in JSON-RPC request";
        case rpc_code::unknown_version: return
            "Uknown version in JSON-RPC request";
        case rpc_code::invalid_null_id: return
            "Invalid null id in JSON-RPC request";
        case rpc_code::expected_strnum_id: return
            "Expected string or number id in JSON-RPC request";
        case rpc_code::expected_id: return
            "Missing id in JSON-RPC request version 1";
        case rpc_code::missing_method: return
            "Missing method in JSON-RPC request";
        case rpc_code::expected_string_method: return
            "Expected string method in JSON-RPC request";
        case rpc_code::expected_structured_params: return
            "Expected structured params in JSON-RPC request version 2";
        case rpc_code::missing_params: return
            "Missing params in JSON-RPC request version 1";
        case rpc_code::expected_array_params: return
            "Expected array params in JSON-RPC request version 1";
        }
        if( ev >= -32099 && ev <= -32000)
            return "An implementation defined server error was received";
        return "Unknown RPC error #" + std::to_string(ev);
    }

    beast::error_condition
    default_error_condition(int ev) const noexcept override
    {
        return {ev, *this};
    }
};

} // (anon)

beast::error_code
make_error_code(rpc_code e)
{
    static rpc_error_codes const cat{};
    return {static_cast<std::underlying_type<
        rpc_code>::type>(e), cat};
}

//------------------------------------------------------------------------------

json::value
rpc_error::
to_json(
    boost::optional<json::value> const& id) const
{
    json::value jv(json::object_kind);
    auto& obj = jv.get_object();
    obj["jsonrpc"] = "2.0";
    auto& err = obj["error"].emplace_object();
    err["code"] = code_;
    err["message"] = msg_;
    if(id.has_value())
        obj["id"] = *id;
    return jv;
}

//------------------------------------------------------------------------------

rpc_call::
rpc_call(
    ::user& u_,
    json::storage_ptr sp)
    : u(boost::shared_from(&u_))
    , method(sp)
    , params(sp)
    , result(std::move(sp))
{
}

void
rpc_call::
extract(
    json::value&& jv,
    beast::error_code& ec)
{
    // must be object
    if(! jv.is_object())
    {
        ec = rpc_code::expected_object;
        return ;
    }

    auto& obj = jv.as_object();

    // extract id first so on error,
    // the response can include it.
    {
        auto it = obj.find("id");
        if(it != obj.end())
            id_.emplace(std::move(it->value()));
    }

    // now check the version
    {
        auto it = obj.find("jsonrpc");
        if(it != obj.end())
        {
            if(! it->value().is_string())
            {
                ec = rpc_code::expected_string_version;
                return;
            }
            auto const& s =
                it->value().as_string();
            if(s != "2.0")
            {
                ec = rpc_code::unknown_version;
                return;
            }
            version = 2;
        }
        else
        {
            version = 1;
        }
    }

    // validate the extracted id
    {
        if(version == 2)
        {
            if(id_.has_value())
            {
                // The use of Null as a value for the
                // id member in a Request is discouraged.
                if(id_->is_null())
                {
                    ec = rpc_code::invalid_null_id;
                    return;
                }

                if( ! id_->is_number() &&
                    ! id_->is_string())
                {
                    ec = rpc_code::expected_strnum_id;
                    return;
                }
            }
        }
        else
        {
            // id must be present in 1.0
            if(! id_.has_value())
            {
                ec = rpc_code::expected_id;
                return;
            }
        }
    }

    // extract method
    {
        auto it = obj.find("method");
        if(it == obj.end())
        {
            ec = rpc_code::missing_method;
            return;
        }
        if(! it->value().is_string())
        {
            ec = rpc_code::expected_string_method;
            return;
        }
        method = std::move(
            it->value().as_string());
    }

    // extract params
    {
        auto it = obj.find("params");
        if(version == 2)
        {
            if(it != obj.end())
            {
                if( ! it->value().is_object() &&
                    ! it->value().is_array())
                {
                    ec = rpc_code::expected_structured_params;
                    return;
                }
                params = std::move(it->value());
            }
        }
        else
        {
            if(it == obj.end())
            {
                ec = rpc_code::missing_params;
                return;
            }
            if(! it->value().is_array())
            {
                ec = rpc_code::expected_array_params;
                return;
            }
            params = std::move(it->value());
        }
    }
}

void
rpc_call::
complete()
{
    if(! id_.has_value())
        return;
    json::value res(
        json::object_kind,
        result.storage());
    auto& obj = res.get_object();
    obj.emplace("id", *id_);
    obj.emplace("result", std::move(result));
    u->send(res);
}

void
rpc_call::
complete(rpc_error const& e)
{
    if(! id_.has_value())
        return;
    u->send(e.to_json(id_));
}

//------------------------------------------------------------------------------

json::object&
checked_object(json::value& jv)
{
    if(! jv.is_object())
        throw rpc_error{
            "expected object"};
    return jv.as_object();
}

json::array&
checked_array(json::value& jv)
{
    if(! jv.is_array())
        throw rpc_error{
            "expected array"};
    return jv.as_array();
}

json::string&
checked_string(json::value& jv)
{
    if(! jv.is_string())
        throw rpc_error{
            "expected string"};
    return jv.as_string();
}

std::uint64_t&
checked_uint64(json::value& jv)
{
    if(! jv.is_number())
        throw rpc_error{
            "expected number"};
    return jv.get_uint64();
}

bool&
checked_bool(json::value& jv)
{
    if(! jv.is_bool())
        throw rpc_error{
            "expected bool"};
    return jv.as_bool();
}

void
checked_null(json::value& jv)
{
    if(! jv.is_null())
        throw rpc_error{
            "expected null"};
}

json::value&
checked_value(
    json::value& jv,
    beast::string_view key)
{
    auto& obj =
        checked_object(jv);
    auto it = obj.find(key);
    if(it == obj.end())
        throw rpc_error{
            "key '" + std::string(key) + "' not found"};
    return it->value();
}

json::object&
checked_object(
    json::value& jv,
    beast::string_view key)
{
    return checked_object(
        checked_value(jv, key));
}

json::array&
checked_array(
    json::value& jv,
    beast::string_view key)
{
    return checked_array(
        checked_value(jv, key));
}

json::string&
checked_string(
    json::value& jv,
    beast::string_view key)
{
    return checked_string(
        checked_value(jv, key));
}

std::uint64_t&
checked_uint64(
    json::value& jv,
    beast::string_view key)
{
    return checked_uint64(
        checked_value(jv, key));
}

bool&
checked_bool(
    json::value& jv,
    beast::string_view key)
{
    return checked_bool(
        checked_value(jv, key));
}

void
checked_null(
    json::value& jv,
    beast::string_view key)
{
    checked_null(checked_value(jv, key));
}
