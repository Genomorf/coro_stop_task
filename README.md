Программа может долго ожидать ответа от какой-либо сущности, например, сервера или I/O.  Seastar предоставляет широкий набор инструментов для прерывания таких ожиданий.
## 1.  seastar::with_timeout

Есть функция с долгим процессом выполнения slow_op(). Представим, что внутри происходят вычисления или же мы ждем ответа от сервера и т.д. В какой-то момент нам надоедает ждать и хочется выйти из этой функции и перестать выполнять внутренние операции, вернувшись к точке вызова этой функции.

with_timeout самый простой способ: метод принимает time_point, и future<T\>. Внутри заводится таймер на время, указанное в первом аргументе, при истечении которого future<T\> становится failed и содержит в себе std::exception_ptr на timed_out_error

и тут же возвращается в caller. co_await вызывает future.get() и записывает результат в res. Если произошел timeout, то exception_ptr сперва необходимо обработать, например, с помощью handle_exception. Если все прошло успешно и slow_op отработала до timeout'a, то получаем результат(1) из slow_op.

```c++
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <iostream>
#include <seastar/core/with_timeout.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/thread.hh>

using namespace std::chrono_literals;
using namespace seastar;

future<int> slow_op(){
    std::cout << "Start\n";
    co_await async([] {
        (void)sleep(10s).get();
    });
    std::cout << "Finished\n";
    co_return 1;
}
future<> run(){
    std::chrono::seconds timeout = 2s;
    int res = co_await with_timeout(std::chrono::steady_clock::now() + timeout,  slow_op()).
            handle_exception([] (std::exception_ptr e) {
                std::cout << "Exception\n"; return 0;
            });
    std::cout << "res is: " << res;
}

int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, run);
}

// output:
// Start
// Exception
// res is: 0
```
Можно даже переписать with_timeout под наши нужды. Идея кода довольна проста и понятна:

    Wait for either a future, or a timeout, whichever comes first

Вместо timeout'a можно ожидать что угодно. Например другую future, которая содержит в себе сообщение от сервера, которое сигнализирует, что нужно вернуть failed future и остановить выполнение функции.

Простой, некрасивый, но наглядный пример может быть таким:
```c++
template<typename... T, typename U>
future<T...> with_message(future<T...> f, promise<U>& msg_promise, U msg_value) {
    if (f.available()) {
        return f;
    }
    auto pr = std::make_unique<promise<T...>>();
    auto result = pr->get_future(); // async waiting for future1

    (void)async([&pr = *pr, &msg_promise, &msg_value] {
        auto msg = msg_promise.get_future(); // async waiting for future2
        if(msg.get() == msg_value)
            pr.set_exception(std::make_exception_ptr(std::runtime_error{"message said to stop"}));
    });
    // Future is returned indirectly.
    (void)f.then_wrapped([pr = std::move(pr)] (auto&& f) mutable {
            f.forward_to(std::move(*pr));
    });
    return result;
}

future<int> slow_op(){
    std::cout << "Start\n";
    co_await async([] {
        (void)sleep(3s).get();
    });
    std::cout << "Finished\n";
    co_return 1;
}

promise<int> p;
future<int> run_with_message(int msg_val, int failed_val){
    async([msg_val] {
        (void)sleep(1s).then([msg_val] { p.set_value(msg_val); }).get();
    });
    auto fut = slow_op();
    return with_message(std::move(fut), p, failed_val);
}

future<> run(){
    return run_with_message(1, 1).
                        handle_exception([] (std::exception_ptr e) {
                            std::cout << "Exception\n"; return 0;}).
    then([] (auto res) { std::cout << "res is: " << res; });
}


// output:
// Start
// Exception
// res is 0
```
promise<int\> может представлять собой что угодно, например сущность типа сервера, главное - он позволяет нам передать его в другую функцию и оттуда дожидаться get_future() и get().

Параллельно с slow_op мы запускаем sleep в 32 строке, по истечению которого в promise устанавливается значение int. Если это значение равно failed_value, то with_message возвращает failed future.

TODO: переписать на co_await и без глобального promise; в debug mode пример с with_message не работает =), почему-то failed exception не возвращается.

## 2. seastar::gate

Примеры выше работают хорошо только в том случае, когда у нас нет никаких дополнительных операций, которые будут выполняться в коде после возвращения future. На самом деле, если мы добавим в методе run() дополнительное ожидание перед окончанием выполнения программы, то все что было в slow_op выполнится до конца. А точнее sleep и std::cout << "Finished\n". Но значение return 1 мы все же не дождемся и потеряем.

```c++
future<int> slow_op(){
    std::cout << "Start\n";
    co_await async([] {
        sleep(3s).then([] { std::cout << "sleep was running all time\n"; }).get();
    });
    std::cout << "Finished\n";
    co_return 1;
}

// ...

future<> run(){
    return run_with_message(1, 1).
                        handle_exception([] (std::exception_ptr e) {
                            std::cout << "Exception\n"; return 0;}).
    then([] (auto res) { std::cout << "res is: " << res; });
    return sleep(10s);
}

// output:
// Start
// Exception
// res is 0
// sleep was running all time
// Finished
```
Все потому что:

    Note that timing out doesn't cancel any tasks associated with the original future.

    It also doesn't cancel the callback registerred on it.

Следовательно нам нужен более мощный инструмент. seastar::gate позволяет выборочно контролировать выполнение каждой функции и останавливать процесс выполнения и все дальнейшие операции внутри функции.

Код в этой репе.

Допустим, у нас есть цепочка корутин, которая вызывается друг за другом: с1 → c2 → c3 ... → cN.

Нужно остановить выполнение цепочки в любой момент времени. gate позволяет пометить нужные вызовы функций с помощью with_gate. При попытке вызова, в начале выполняется gate.enter(), а затем сама функция. В то же время, можно асинхронно закрыть gate методом gate.close() и любые дальнейшие попытки вызвать gate.enter() провалятся с ошибкой. А точнее будет throw gate_closed_exception(). \

Простой пример работы:

```c++
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
        // else you will catch "heap after use" on line 39
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
```
## 3. seastar::abort_source

Если gate грубо останавливает процесс исполнения, то seastar::abort_source - это более мягкий и гибкий способ. В сущности, это signaling механизм, который позволяет передать сигнал об остановке во все функции (сервисы) и далее что-то сделать. 

Объект abort_source передается в состояние класса, методы которого нужно будет контролировать. Далее можно установить callback, который будет вызван после запроса об остановке (опционально). Остановка выполнения происходит вызовом abort_source::request_abort(). В каждый метод, который необходимо контролировать, мы передаем проверку условия abort_source::abort_requested() или abort_source::check(). 

В первом случае это позволит работать будто бы с флагом, а во втором случае как с gate'ом - перед вызовом метода проверка check() выбросит throw abort_requested_exception(), если был вызов abort_requested().

Простой пример:

```c++
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
```
