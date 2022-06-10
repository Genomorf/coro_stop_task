# coro_stop_task
```c++
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <iostream>
#include <numeric>
#include <seastar/core/thread.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/when_any.hh>
#include <seastar/core/with_timeout.hh>
#include <seastar/core/gate.hh>
#include <seastar/util/closeable.hh>
using namespace std::chrono_literals;
using namespace seastar;

gate g;
struct Client {
    std::chrono::seconds timeout;
    Client(std::chrono::seconds timeout) : timeout(timeout) {
        std::cout << "Client created\n";
    }
    ~Client(){
        std::cout << "Client deleted\n";
    }
    future<int> respond4(){
        co_await sleep(timeout).then([] { std::cout << "firth\n" << std::endl; });
        co_return 1;
    }
    future<int> respond3(){
        co_await sleep(timeout).then([] { std::cout << "third\n" << std::endl; });
        co_return co_await with_gate(g, [this] () -> future<int> { return respond4(); });
    }
    future<int> respond2(){
        co_await sleep(timeout).then([] { std::cout << "second\n" << std::endl; });
        co_return co_await with_gate(g, [this] () -> future<int> { return respond3(); });
    }
    future<int> start_coro_chain(){
        co_await sleep(timeout).then([] { std::cout << "first\n" << std::endl; });
        co_return co_await with_gate(g, [this] () -> future<int>  { return respond2(); });
    }
};

Client c{2s}; // set timeout to 2s

future<> f(){
    auto x = async([] {
        return with_gate(g, [] {
            return sleep(2500ms).then([] { // set timeout to 2.5s and stop executing after this
                return g.close();
            });
        }).get();
    });
    auto y = co_await c.start_coro_chain();
    std::cout << "y is: " << y << std::endl; // if success we will see this output
    co_return;
}

future<> run(){
    return f().handle_exception([] (auto e) { std::cout << "ex"; }); // closing gate while coro is running will throw exception
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, run);
}
```
