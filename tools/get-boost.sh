#! /bin/sh

build_dir=$2

branch="master"
if [ $1 == "master" ]; then
    branch="develop"
fi

git clone -b $branch --depth 1 https://github.com/boostorg/boost.git boost-root

cd boost-root

git submodule update --init libs/headers
git submodule update --init tools/build
git submodule update --init tools/boost_install
git submodule update --init tools/boostdep
git submodule update --init libs/align
git submodule update --init libs/asio
git submodule update --init libs/assert
git submodule update --init libs/config
git submodule update --init libs/core
git submodule update --init libs/endian
git submodule update --init libs/filesystem
git submodule update --init libs/intrusive
git submodule update --init libs/locale
git submodule update --init libs/optional
git submodule update --init libs/smart_ptr
git submodule update --init libs/static_assert
git submodule update --init libs/system
git submodule update --init libs/throw_exception
git submodule update --init libs/type_traits
git submodule update --init libs/utility
git submodule update --init libs/winapi

git submodule update --init libs/algorithm
git submodule update --init libs/array
git submodule update --init libs/atomic
git submodule update --init libs/bind
git submodule update --init libs/chrono
git submodule update --init libs/concept_check
git submodule update --init libs/container
git submodule update --init libs/container_hash
git submodule update --init libs/context
git submodule update --init libs/conversion
git submodule update --init libs/coroutine
git submodule update --init libs/date_time
git submodule update --init libs/detail
git submodule update --init libs/exception
git submodule update --init libs/function
git submodule update --init libs/function_types
git submodule update --init libs/functional
git submodule update --init libs/fusion
git submodule update --init libs/integer
git submodule update --init libs/io
git submodule update --init libs/iterator
git submodule update --init libs/lambda
git submodule update --init libs/lexical_cast
git submodule update --init libs/logic
git submodule update --init libs/math
git submodule update --init libs/move
git submodule update --init libs/mp11
git submodule update --init libs/mpl
git submodule update --init libs/numeric/conversion
git submodule update --init libs/pool
git submodule update --init libs/predef
git submodule update --init libs/preprocessor
git submodule update --init libs/random
git submodule update --init libs/range
git submodule update --init libs/ratio
git submodule update --init libs/rational
git submodule update --init libs/serialization
git submodule update --init libs/thread
git submodule update --init libs/tokenizer
git submodule update --init libs/tuple
git submodule update --init libs/type_index
git submodule update --init libs/typeof
git submodule update --init libs/unordered

echo Submodule update complete