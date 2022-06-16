#include "abort_source_example.hpp"
#include <seastar/core/abortable_fifo.hh>
#include <seastar/core/smp.hh>
namespace {
struct Client {
    std::chrono::milliseconds timeout;
    abort_source& as;
    Client(std::chrono::milliseconds timeout, abort_source& as) : timeout(timeout), as(as) {
        std::cout << "Client created\n";
    }
    ~Client(){
        std::cout << "Client deleted\n";
    }
    future<int> respond4(){
        as.check(); // check: if (abort_requested()) { throw exception } else { do_nothing }
        co_await sleep(timeout).then([] { std::cout << "firth\n"; });
        co_return 1;
    }
    future<int> respond3(){
        as.check();
        co_await sleep(timeout).then([] { std::cout << "third\n"; });
        co_return co_await respond4();
    }
    future<int> respond2(){
        as.check();
        co_await sleep(timeout).then([] { std::cout << "second\n" ; });
        co_return co_await respond3();
    }
    future<int> start_coro_chain(){
        as.check();
        co_await sleep(timeout).then([] { std::cout << "first\n"; });
        co_return co_await respond2();
    }
};
}// namespace

future<int> run_abort_source(std::chrono::milliseconds& client_timeout, std::chrono::milliseconds& abort_timeout){
    abort_source a; // pass it to handle all services
    (void)async([&]{ // simulate some work and abort all after timeout
        sleep(abort_timeout).then([&] { std::cout << "abort requested\n"; a.request_abort(); }).get();
    });
    Client c{client_timeout, a};
    auto x = co_await c.start_coro_chain();
    co_return x;
}

future<int> test_abort(std::chrono::milliseconds client_timeout, std::chrono::milliseconds abort_timeout){
    auto f = co_await run_abort_source(client_timeout, abort_timeout).
            handle_exception([] (auto e) {
                std::cout << "Exception handled: aborted\n";
                return 0;
    }); // as.check() may throw an exception so catch it
    co_return f;
}
// it can be done without throwing at all
// instead of as.check() we can use abort_requested() e.g.:
// future<int> start_coro_chain(){
//    if (abort_requested()) {
//        // clean some data or call other funcs
//        return 0;
//    }
//    co_await sleep(timeout).then([] { std::cout << "first\n"; });
//    co_return co_await respond2();
//}
