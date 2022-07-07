#pragma once

#include <atomic>
#include <coroutine>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

class async_manual_reset_event {
public:
    explicit async_manual_reset_event(bool initially_set = false) noexcept;

    async_manual_reset_event(const async_manual_reset_event&) = delete;
    async_manual_reset_event(async_manual_reset_event&&) = delete;
    async_manual_reset_event& operator=(const async_manual_reset_event&) = delete;
    async_manual_reset_event& operator=(async_manual_reset_event&&) = delete;

    bool is_set() const noexcept;

    struct awaiter;
    awaiter operator co_await() const noexcept;

    void set() noexcept;
    void reset() noexcept;
private:
    friend struct awaiter;

    mutable std::atomic<void*> m_state;
};

struct async_manual_reset_event::awaiter {
    explicit awaiter(const async_manual_reset_event & event) noexcept
        : m_event(event) {}

    bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept;
    void await_resume() noexcept {}

    const async_manual_reset_event & m_event;
    std::coroutine_handle<> m_awaiting_coroutine;
    awaiter * m_next;
};

bool async_manual_reset_event::awaiter::await_ready() const noexcept {
    return m_event.is_set();
}

bool async_manual_reset_event::awaiter::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept {
    const void * const setState = &m_event;
    m_awaiting_coroutine = awaiting_coroutine;
    void * old_value = m_event.m_state.load(std::memory_order_acquire);
    do {
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

async_manual_reset_event::async_manual_reset_event(bool initially_set) noexcept
    :m_state(initially_set ? this : nullptr) {}

bool async_manual_reset_event::is_set() const noexcept {
    return m_state.load(std::memory_order_acquire) == this;
}

void async_manual_reset_event::reset() noexcept {
    void * old_value = this;
    m_state.compare_exchange_strong(old_value, nullptr, std::memory_order_acquire);
}

void async_manual_reset_event::set() noexcept {
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

async_manual_reset_event::awaiter async_manual_reset_event::operator co_await() const noexcept {
    return awaiter{*this};
}

struct trivial_task {
    struct promise_type {
        trivial_task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

