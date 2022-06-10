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
    auto y = co_await c.start_coro_chain(); // if failed jump to run handle_exception
    std::cout << "y is: " << y << std::endl; // if success we will see this output
    co_return;
}

future<> run(){
    return f().handle_exception([] (auto e) { std::cout << "Gate closed, execution stopped\n"; }); // closing gate while coro is running will throw an  exception
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, run);
}
```
Console output:
```
Client created
first
second
Gate closed, execution stopped
Client deleted
```
Мы имеем цепчоку корутин. start_coro_chain() спит, потом запускает respond2(), которая спит, запускает respond3() и так далее, пока не дойдем до respond4() и не вернем 1.

Одновременно с этим запускаем в отдельном треде sleep(2500ms) после которого закрываем gate. Все процессы запущенные с with_gate() будут завершены. 

Такой тайм трейс:
0 сек - 2 сек: отработает start_coro_chain, распечатает first и запустит respond2.
2.5 сек: закрывается гейт.
2 сек - 4 сек: respond2 распечатает second, но не запустит respond3, который находится в with_gate, который уже закрыт

Так как после закрытия гейта мы продолжаем пытаться запустить функцию с with_gate, будет брошен exception, который ловим в run()

Если бы не было закрытия гейта, то вывод бы был очевидно такой:
```
Client created
first
second
third
firth
y is: 1
Client deleted
```
