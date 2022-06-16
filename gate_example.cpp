#include "gate_example.hpp"
namespace {
struct Client {
    std::chrono::milliseconds timeout;
    shared_ptr<gate> g;
    Client(std::chrono::milliseconds timeout, shared_ptr<gate> g) : timeout(timeout), g(g) {
        std::cout << "Client created\n";
    }
    ~Client(){
        std::cout << "Client deleted\n";
    }
    future<int> respond4(){
        co_await sleep(timeout).then([] { std::cout << "firth\n"; });
        co_return 1;
    }
    future<int> respond3(){
        co_await sleep(timeout).then([] { std::cout << "third\n"; });
        // if gate is not closed than we can run this code
        // if gate is closed than trying running this line will throw an exception
        co_return co_await with_gate(*g, [this] { return respond4(); });
    }
    future<int> respond2(){
        co_await sleep(timeout).then([] { std::cout << "second\n"; });
        co_return co_await with_gate(*g, [this] { return respond3(); });
    }
    future<int> start_coro_chain(){
        co_await sleep(timeout).then([] { std::cout << "first\n"; });
        co_return co_await with_gate(*g, [this] { return respond2(); });
    }
};
} // namespace

future<int> f(std::chrono::milliseconds& client_timeout, std::chrono::milliseconds& gate_timeout){
        // make it shared to able to survive if sleep time > lifetime of everything
        // else you will catch "heap after use" on line39
        auto g = make_shared<gate>();
        Client c{client_timeout, g};
        // set timeout to 2.5s and stop executing after this
        (void)sleep(gate_timeout).then([g] { return g->close(); });
        auto y = co_await c.start_coro_chain();
        co_return y;
}

future<int> test_gate(std::chrono::milliseconds client_timeout, std::chrono::milliseconds gate_timeout){
    auto x = co_await f(client_timeout, gate_timeout).
            handle_exception([] (std::exception_ptr e){
                std::cout << "Exception handled: gate stopped\n";
                return 0;
    }); // gate.enter() while gate.closed() will throw an exception
    co_return x;
}
