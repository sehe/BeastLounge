//
// Copyright (c) 2018-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/BeastLounge
//

#include "message.hpp"
#include <boost/beast/core/flat_static_buffer.hpp>
#include <boost/json/serializer.hpp>

//------------------------------------------------------------------------------

message
make_message(json::value const& jv)
{
    char buf[16384];
    json::serializer sr;
    sr.reset(&jv);
    auto const r = sr.read(buf, sizeof(buf));
    if(! sr.done())
    {
        // buffer overflow!
        return {};
    }
    return message(net::const_buffer(r.data(), r.size()));
}

