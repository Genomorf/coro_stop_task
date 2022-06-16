#ifndef GATE_EXAMPLE_HPP
#define GATE_EXAMPLE_HPP

#include <iostream>
#include <seastar/core/sleep.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/thread.hh>
using namespace std::chrono_literals;
using namespace seastar;


future<int> test_gate(std::chrono::milliseconds client_timeout, std::chrono::milliseconds gate_timeout);


#endif // GATE_EXAMPLE_HPP
