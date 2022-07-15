#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <iostream>
#include <thread>
#include <vector>

template <typename T = void>
class coro_future;

class coro_future_base {
public:
    coro_future_base() noexcept = default;

    coro_future_base(const coro_future_base &) = delete;
    coro_future_base(coro_future_base &&) = delete;
    coro_future_base & operator=(const coro_future_base &) = delete;
    coro_future_base & operator=(coro_future_base &&) = delete;

    bool is_set() const noexcept {
        return m_state.load(std::memory_order_acquire) == this;
    }

    struct awaiter {
        explicit awaiter(coro_future_base & event) noexcept
            : m_event(event) {}

        bool await_ready() const noexcept {
            return m_event.is_set();
        }
        bool await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept {
            const void * const setState = &m_event;
            m_awaiting_coroutine = awaiting_coroutine;
            do {
                void * old_value = m_event.m_state.load(std::memory_order_acquire);
                if (old_value == setState)
                    return false;
                m_next = static_cast<awaiter *>(old_value);
            } while (!m_event.m_state.compare_exchange_weak(
                old_value,
                this,
                std::memory_order_release,
                std::memory_order_acquire));
            return true;
        }
        void await_resume() noexcept {}

        coro_future_base & m_event;
        std::coroutine_handle<> m_awaiting_coroutine;
        awaiter * m_next;
    };

    awaiter operator co_await() noexcept {
        return awaiter{*this};
    }
protected:
    void set() noexcept {
        void * old_value = m_state.exchange(this, std::memory_order_acq_rel);
        if (old_value != this) {
            auto * waiters = static_cast<awaiter *>(old_value);
            while (waiters != nullptr) {
                auto * next = waiters->m_next;
                waiters->m_awaiting_coroutine.resume();
                waiters = next;
            }
        }
    }

    void reset() noexcept {
        void * old_value = this;
        m_state.compare_exchange_strong(old_value, nullptr, std::memory_order_acquire);
    }

private:
    friend struct awaiter;

    mutable std::atomic<void *> m_state{nullptr};
};

template <>
class coro_future<void> : public coro_future_base {
public:
    coro_future() noexcept = default;

    using coro_future_base::set;
    using coro_future_base::reset;
};

template <typename T>
class coro_future : public coro_future_base {
public:
    coro_future() = default;

    template <typename U>
    void set(U && u) {
        m_value.emplace(std::forward<U>(u));
        coro_future_base::set();
    }

    void set_exception(std::exception_ptr e) {
        m_exception = std::move(e);
        coro_future_base::set();
    }

    void reset() {
        coro_future_base::reset();
        m_value.reset();
    }

    struct awaiter : coro_future_base::awaiter {
        explicit awaiter(coro_future & event) noexcept
            : coro_future_base::awaiter(event) {}

        T&& await_resume() {
            auto & f = static_cast<coro_future &>(m_event);
            if (f.m_exception)
                std::rethrow_exception(f.m_exception);
            return std::move(f.m_value.value());
        }
    };

    awaiter operator co_await() noexcept {
        return awaiter{*this};
    }
private:
    std::optional<T> m_value;
    std::exception_ptr m_exception;
};

struct trivial_task {
    struct promise_type {
        trivial_task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};
