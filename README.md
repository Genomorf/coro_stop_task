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
        co_return co_await with_gate(g, [this] { return respond4(); });
    }
    future<int> respond2(){
        co_await sleep(timeout).then([] { std::cout << "second\n" << std::endl; });
        co_return co_await with_gate(g, [this] { return respond3(); });
    }
    future<int> start_coro_chain(){
        co_await sleep(timeout).then([] { std::cout << "first\n" << std::endl; });
        co_return co_await with_gate(g, [this] { return respond2(); });
    }
};

Client c{2s}; // set sleep time to each func in Cilent to 2s

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
    // closing gate while coro is running will throw an  exception
    return f().handle_exception([] (auto e) { std::cout << "Gate closed, execution stopped\n"; }); 
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

Это пример кстати успешно заменяется на seastar::with_timeout, который в случае истечения таймера (первый аргумент) сделает нашу future failed и ее можно будет обработать в handle_exception.
```c++
future<> f(){
    auto fut = c.start_coro_chain();
    return with_timeout(std::chrono::steady_clock::now() + std::chrono::milliseconds(2500), std::move(fut)).
            handle_exception([] (std::exception_ptr e) {
                std::cout << "timeout happend\n";
                return 0;
            }).then([] (auto x) {
                std::cout << "x is : " << x << std::endl;
                return make_ready_future();
            });
}
```
Существенная разница, что ему плевать на все и он отменяет все сразу, а не выборочно. В предыдущем случае отменяется только то, что помечено with_gate, поэтому sleep respond2 мы не дождемся и в консоли будет только:
```
Client created
first
timeout happend
x is : 0
Client deleted
```

Однако есть случаи когда нам нужно отменить не по таймеру, а как-то еще.

Например.

1) Если придет сообщение от другой сущности.
```c++
promise<int> p;
future<> f(){
    sleep(3s).then([] { p.set_value(1); }); // simulate some work
    auto x = async([] {
        return with_gate(g, [] {
            auto fut = p.get_future(); // waiting for value to be set
            if(fut.get() == 1)
                return g.close();
            else{
                return make_ready_future();
            }
        }).get();
    });
    auto y = co_await c.start_coro_chain(); // if failed jump to run handle_exception
    std::cout << "y is: " << y << std::endl; // if success we will see this output
    co_return;
}

future<> run(){
    return f().handle_exception([] (auto e) { std::cout << "Gate closed, execution stopped\n"; }); // closing gate while coro is running will throw exception
}
```
В данном случае вместо promise<int\> p может быть любая сущность, например сервер или сервис. Он ожидает когда future будет готова и если там окажется 1, то гейт закроется.

Точно также можно асинхронно ждать какое-то сообщение от сервера и закончить выполнение когда оно придет.

Если нет, то исполнение продолжится. Вывод такой же как в 1 примере.

2) Можно остановить выполнение просто нажатием клавиши

```c++
future<> f(){
    auto x = async([] {
        return with_gate(g, [] {
            return smp::submit_to(1, [] { // cin will block this core
                int x;
                while(1) {
                    std::cin >> x;
                    if(x == 1)
                        return g.close();
                }
            });
        }).get();
    });
    auto y = co_await c.start_coro_chain(); // if failed jump to run handle_exception
    std::cout << "y is: " << y << std::endl; // if success we will see this output
    co_return;
}
```
