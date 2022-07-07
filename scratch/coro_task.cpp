#include <coroutine>
#include <exception>
#include <iostream>
#include <utility>
#include <vector>

#include "coro_future.h"

class task {
public:
    class promise_type {
    public:
        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept {
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept { m_exception = std::current_exception(); }

        struct final_awaiter {
            bool await_ready() noexcept {
                return false;
            }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                return h.promise().m_continuation;
            }
            void await_resume() noexcept {
            }
        };

        final_awaiter final_suspend() noexcept {
            return {};
        }

        std::coroutine_handle<> m_continuation;
        std::exception_ptr m_exception;
    };

    task(task && t) noexcept
        : m_coro(std::exchange(t.m_coro, {})) {}

    ~task() {
        if (m_coro)
            m_coro.destroy();
    }

    class awaiter {
    public:
        bool await_ready() noexcept { return !m_coro || m_coro.done(); }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
            m_coro.promise().m_continuation = continuation;
            return m_coro;
        }

        void await_resume() noexcept {
            auto & promise = m_coro.promise();
            if (promise.m_exception) {
                std::rethrow_exception(promise.m_exception);
            }
        }

    private:
        friend task;
        explicit awaiter(std::coroutine_handle<promise_type> h) noexcept
            : m_coro(h) {}

        std::coroutine_handle<promise_type> m_coro;
    };

    awaiter operator co_await() && noexcept {
        return awaiter{m_coro};
    }

private:
    explicit task(std::coroutine_handle<promise_type> h) noexcept
        : m_coro(h) {}

    std::coroutine_handle<promise_type> m_coro;
};

struct sync_wait_task {
    struct promise_type {
        sync_wait_task get_return_object() noexcept {
            return sync_wait_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { m_exception = std::current_exception(); }

        std::exception_ptr m_exception;
    };

    explicit sync_wait_task(std::coroutine_handle<promise_type> h) noexcept
        : m_coro(h) {}

    sync_wait_task(sync_wait_task && t) noexcept
        : m_coro(std::exchange(t.m_coro, {})) {}

    ~sync_wait_task() {
        if (m_coro)
            m_coro.destroy();
    }

    static sync_wait_task start(task && t) {
        co_await std::move(t);
    }

    bool done() {
        if (!m_coro) {
            return true;
        }
        if (m_coro.promise().m_exception)
            std::rethrow_exception(m_coro.promise().m_exception);
        return m_coro.done();
    }

    std::coroutine_handle<promise_type> m_coro;
};

struct manual_executor {
    struct schedule_op {
        manual_executor & m_executor;
        schedule_op * m_next = nullptr;
        std::coroutine_handle<> m_continuation;

        explicit schedule_op(manual_executor & e)
            : m_executor(e) {}

        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> continuation) noexcept {
            m_continuation = continuation;
            m_next = std::exchange(m_executor.m_head, this);
        }
        void await_resume() noexcept {}
    };

    schedule_op schedule() noexcept { return schedule_op{*this}; }

    void drain() {
        while (m_head != nullptr) {
            auto * item = m_head;
            m_head = item->m_next;
            item->m_continuation.resume();
        }
    }

    void sync_wait(task && t) {
        auto t2 = sync_wait_task::start(std::move(t));
        while (!t2.done()) {
            drain();
        }
    }

    void sync_wait(std::vector<task> & ts) {
        std::vector<sync_wait_task> waiting_tasks;
        for (auto & t : ts) {
            waiting_tasks.push_back(sync_wait_task::start(std::move(t)));
            for (auto & t : waiting_tasks) {
                while (!t.done()) {
                    drain();
                }
            }
        }
    }

    schedule_op * m_head = nullptr;
};

task foo(manual_executor & ex) {
    std::cout << "inside foo()\n";
    co_await ex.schedule();
    std::cout << "about to return from foo()\n";
    co_return;
}

task bar(manual_executor & ex) {
    std::cout << "about to call foo()\n";
    co_await foo(ex);
    std::cout << "done calling foo()\n";
}

void producer(coro_future<int> & event) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // event.set(10);
    try {
        throw std::runtime_error("Example");
    } catch (...) {
        event.set_exception(std::current_exception());
    }
}

task f() {
    co_return;
}

task consumer(coro_future<int> & event) {
    co_await f();
    try {
        auto v = co_await event;
        std::cout << "got event " << v << std::endl;
    } catch (...) {
        std::cout << "got exception: unknown" << std::endl;
    }
}

int main() {
    coro_future<int> event;
    std::vector<task> tasks;
    tasks.push_back(consumer(event));
    tasks.push_back(consumer(event));
    tasks.push_back(consumer(event));
    tasks.push_back(consumer(event));
    tasks.push_back(consumer(event));
    std::thread t([&event] { producer(event); });
    manual_executor ex;
    try {
        ex.sync_wait(tasks);
    } catch (...) {
        std::cout << "got exception in main" << std::endl;
    }
    t.join();
    return 0;
}
