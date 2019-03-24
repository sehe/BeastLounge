//
// Copyright (c) 2018-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/BeastLounge
//

// Test that header file is self-contained.
#include <boost/beast/_experimental/json/array.hpp>

#include <boost/beast/_experimental/unit_test/suite.hpp>

namespace boost {
namespace beast {
namespace json {

class array_test : public unit_test::suite
{
public:
    void
    testSpecial()
    {
        array a1{1, "two", false};
        array a2(std::move(a1));
    }

    void
    run() override
    {
        testSpecial();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(beast,json,array);

} // json
} // beast
} // boost
