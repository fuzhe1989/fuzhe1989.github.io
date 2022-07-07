#include <coroutine>
#include <iostream>
#include <optional>
#include <string>

template <typename T>
inline std::ostream & operator<<(std::ostream & out, const std::optional<T> & o) {
    if (o.has_value()) {
        out << "optional(" << o.value() << ")";
    } else {
        out << "optional()";
    }
    return out;
}

inline std::ostream & operator<<(std::ostream & out, std::nullopt_t) {
    out << "optional()";
    return out;
}

namespace detail {
template <typename Value>
struct OptionalPromise;

template <typename T>
struct OptionalPromiseReturn {
    // The staging object that is returned (by copy/move) to the caller of the coroutine.
    std::optional<T> stage;

    // When constructed, we assign a pointer to ourselves to the supplied reference to
    // pointer.
    OptionalPromiseReturn(OptionalPromise<T> & p)
        : stage{} {
        //std::cout << "OptionalPromiseReturn::p_ctor" << std::endl;
        p.data = &stage;
    }

    // Copying doesn't make any sense (which copy should the pointer refer to?).
    OptionalPromiseReturn(OptionalPromiseReturn const &) = delete;
    // To move, we just update the pointer to point at the new object.
    OptionalPromiseReturn(OptionalPromiseReturn && other) = delete;
    /*
      : stage(std::move(other.stage)), p(other.p) {
    std::cout << "OptionalPromiseReturn::move_ctor" << std::endl;
    p = this;
  }
  */

    // Assignment doesn't make sense.
    void operator=(OptionalPromiseReturn const &) = delete;
    void operator=(OptionalPromiseReturn &&) = delete;

    // A non-trivial destructor is required until
    // https://bugs.llvm.org//show_bug.cgi?id=28593 is fixed.
    ~OptionalPromiseReturn() {
        // std::cout << this << ": ~OptionalPromiseReturn" << std::endl;
    }

    // We assume that we will be converted only once, so we can move from the staging
    // object. We also assume that `emplace` has been called at least once.
    operator std::optional<T>() && {
        // std::cout << this << ": operator T " << stage << std::endl;
        return std::move(stage);
    }
};

template <typename Value>
struct OptionalPromise {
    std::optional<Value> * data = nullptr;
    OptionalPromise() {
        // std::cout << "OptionalPromise::ctor" << std::endl;
    }
    OptionalPromise(OptionalPromise const &) = delete;
    // This should work regardless of whether the compiler generates:
    //    folly::Optional<Value> retobj{ p.get_return_object(); } // MSVC
    // or:
    //    auto retobj = p.get_return_object(); // clang
    auto get_return_object() noexcept {
        // std::cout << "OptionalPromise::get_return_object " << data << std::endl;
        return OptionalPromiseReturn(*this);
    }
    std::suspend_never initial_suspend() const noexcept {
        // std::cout << "OptionalPromise::initial_suspend" << std::endl;
        return {};
    }
    std::suspend_never final_suspend() const noexcept {
        // std::cout << "OptionalPromise::final_suspend" << std::endl;
        return {};
    }
    void return_value(Value x) {
        // std::cout << "OptionalPromise::return_value " << x << std::endl;
        *data = std::move(x);
    }
    void unhandled_exception() {
        // Technically, throwing from unhandled_exception is underspecified:
        // https://github.com/GorNishanov/CoroutineWording/issues/17
        throw;
    }
};

template <typename Value>
struct OptionalAwaitable {
    std::optional<Value> o_;

    bool await_ready() const noexcept {
        // std::cout << "OptionalAwaitable::await_ready " << o_.has_value() << std::endl;
        return o_.has_value();
    }
    Value await_resume() {
        // std::cout << "OptionalAwaitable::await_resume " << o_ << std::endl;
        return std::move(o_.value());
    }

    // Explicitly only allow suspension into an OptionalPromise
    template <typename U>
    void await_suspend(std::coroutine_handle<OptionalPromise<U>> h) const {
        // std::cout << "OptionalAwaitable::await_suspend" << std::endl;
        // Abort the rest of the coroutine. resume() is not going to be called
        h.destroy();
    }
};
} // namespace detail

template <typename Value>
inline detail::OptionalAwaitable<Value> operator co_await(std::optional<Value> o) {
    // std::cout << "perator co_await(std::optional<Value>) " << o << std::endl;
    return {std::move(o)};
}

namespace std {
template <typename T, typename... Args>
struct coroutine_traits<std::optional<T>, Args...> {
    using promise_type = detail::OptionalPromise<T>;
};

template <typename T>
struct coroutine_traits<detail::OptionalAwaitable<T>> {
    using promise_type = detail::OptionalPromise<T>;
};
} // namespace std

template <typename T>
inline detail::OptionalAwaitable<T> co_awaitUnwrap(std::optional<T> o) {
    // std::cout << "co_awaitUnwrap(std::optional<Value>) " << o << std::endl;
    return {std::move(o)};
}

