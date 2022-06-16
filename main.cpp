#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <iostream>
#include <seastar/core/with_timeout.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/thread.hh>

using namespace std::chrono_literals;
using namespace seastar;

future<> f(){
    throw std::runtime_error("aaa");
}
future<> run(){
    return sleep(1s).then([] { return f(); }).then([] { std::cout << "here\n"; });
//    return f().handle_exception([] (auto e) { std::cout << "ex\n"; });
//    return f();
}
future<> run2(){
    try{
//        return run().handle_exception([] (auto e) { std::cout << "ex2\n"; });
        return run();
    } catch(std::exception& e){
        std::cout << e.what();
        return make_ready_future();
    }
}
int main(int argc, char** argv) {
    seastar::app_template app;
    app.run(argc, argv, run2);
}
