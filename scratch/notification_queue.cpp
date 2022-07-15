#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdint>

#include "magic_enum.hpp"

struct Node {
    enum class Status {
        READER_WAITING,
        READER_LEFT,
        NOTIFIED
    };
    std::mutex mu;
    std::condition_variable cv;
    std::atomic<Status> status = Status::READER_WAITING;
    Node * next = nullptr;
};

class NotificationQueue {
public:
    bool wait() {
        auto * node = new Node();
        Node * old_head = nullptr;
        do {
            old_head = head.load(std::memory_order_acquire);
            node->next = old_head;
        } while (!head.compare_exchange_weak(old_head, node, std::memory_order_release, std::memory_order_acquire));

        {
            std::unique_lock lock(node->mu);
            node->cv.wait_for(lock, std::chrono::milliseconds(1), [node] {
                return node->status.load(std::memory_order_acquire) == Node::Status::NOTIFIED;
            });
            auto expected = Node::Status::READER_WAITING;
            if (!node->status.compare_exchange_strong(expected, Node::Status::READER_LEFT)) {
                node->status.store(Node::Status::READER_LEFT, std::memory_order_release);
                return true;
            }
            return false;
        }
    }

    bool notify() {
        while (true) {
            Node * node = nullptr;
            do {
                node = head.load(std::memory_order_acquire);
            } while (node && !head.compare_exchange_weak(node, node->next, std::memory_order_release, std::memory_order_acquire));
            if (!node) {
                return false;
            }
            auto expected = Node::Status::READER_WAITING;
            if (!node->status.compare_exchange_strong(expected, Node::Status::NOTIFIED)) {
                if (expected == Node::Status::READER_LEFT) {
                    delete node;
                    continue;
                } else {
                    std::cout << "Encounter unexpected status " << magic_enum::enum_name(expected) << std::endl;
                    exit(1);
                }
            }
            node->cv.notify_one();
            while (node->status.load(std::memory_order_acquire) != Node::Status::READER_LEFT) {
                std::this_thread::yield();
            }
            delete node;
            return true;
        }
    }

private:
    std::atomic<Node *> head = nullptr;
};

int main() {
    std::atomic_bool stop = false;
    std::atomic_int notified = 0;
    std::atomic_int try_notified = 0;
    std::atomic_int try_waited = 0;
    std::atomic_int got = 0;
    std::vector<std::thread> threads;
    NotificationQueue queue;
    for (int i = 0; i < 1; ++i) {
        threads.emplace_back([&] {
            while (!stop) {
                try_notified.fetch_add(1, std::memory_order_relaxed);
                auto res = queue.notify();
                notified.fetch_add(res, std::memory_order_relaxed);
            }
        });
    }
    for (int i = 0; i < 1; ++i) {
        threads.emplace_back([&] {
            while (!stop) {
                try_waited.fetch_add(1, std::memory_order_relaxed);
                auto waited = queue.wait();
                got.fetch_add(waited, std::memory_order_relaxed);
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
    stop = true;
    for (auto & t : threads)
        t.join();
    std::cout << "try_notified " << try_notified << std::endl
              << "notified " << notified << std::endl
              << "try_waited " << try_waited << std::endl
              << "got " << got << std::endl;
}
