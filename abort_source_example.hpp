#ifndef ABORT_SOURCE_EXAMPLE_HPP
#define ABORT_SOURCE_EXAMPLE_HPP

#include <iostream>
#include <seastar/core/thread.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/abort_source.hh>
using namespace std::chrono_literals;
using namespace seastar;

future<int> test_abort(std::chrono::milliseconds client_timeout, std::chrono::milliseconds abort_timeout);


#endif // ABORT_SOURCE_EXAMPLE_HPP
