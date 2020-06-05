//
// Copyright (c) 2020 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/BeastLounge
//

#ifndef LOUNGE_IMPL_CHANNEL_HPP
#define LOUNGE_IMPL_CHANNEL_HPP

namespace lounge {

struct channel::handler
{
    virtual ~handler() = default;
};

template<class Handler>
boost::shared_ptr<channel>
channel::
create(
    Handler&& h)
{
    struct handler_impl : handler
    {
        Handler h;

        explicit
        handler_impl(
            Handler&& h_)
            : h(std::forward<
                Handler>(h_))
        {
        }
    };

    return create_impl(
        std::unique_ptr(
            new handler_impl(
                std::forward<Handler>(h))));
}

} // lounge

#endif