/**
 * @file rw_spin_lock.h
 * @author noahyzhang
 * @brief 
 * @version 0.1
 * @date 2023-04-18
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#pragma once

#include <atomic>
#include <thread>

namespace file_io_hook {

/**
 * @brief 读共享，写独占的自旋锁
 * 
 */
class RWSpinLock {
public:
    RWSpinLock() = default;
    ~RWSpinLock() = default;
    RWSpinLock(const RWSpinLock&) = delete;
    RWSpinLock& operator=(const RWSpinLock&) = delete;
    RWSpinLock(RWSpinLock&&) = delete;
    RWSpinLock& operator=(RWSpinLock&&) = delete;

public:
    /**
     * @brief 加锁，独占
     * 
     */
    void write_lock() noexcept {
        base_type tail = tail_.fetch_add(exclusive_step, std::memory_order_relaxed);
        for (;;) {
            base_type head = head_.load(std::memory_order_acquire);
            if (tail == head) break;
            std::this_thread::yield();
        }
    }

    /**
     * @brief 尝试加锁，独占
     * 
     * @return true 
     * @return false 
     */
    bool try_write_lock() noexcept {
        base_type head = head_.load(std::memory_order_acquire);
        base_type tail = tail_.load(std::memory_order_relaxed);
        return head == tail && tail_.compare_exchange_strong(tail, tail+exclusive_step, std::memory_order_relaxed);
    }

    /**
     * @brief 解锁，独占的解锁
     * 
     * @return true 
     * @return false 
     */
    void write_unlock() noexcept {
        base_type head = head_.load(std::memory_order_relaxed);
        head_.store(head + exclusive_step, std::memory_order_release);
    }

    /**
     * @brief 加锁，共享
     * 
     */
    void read_lock() noexcept {
        base_type tail = tail_.fetch_add(shared_step, std::memory_order_relaxed);
        tail &= exclusive_mask;
        for (;;) {
            base_type head = head_.load(std::memory_order_acquire);
            if (tail == (head & exclusive_mask)) break;
            std::this_thread::yield();
        }
    }

    /**
     * @brief 尝试加锁，共享
     * 
     * @return true 
     * @return false 
     */
    bool try_read_lock() noexcept {
        base_type head = head_.load(std::memory_order_acquire);
        base_type tail = tail_.load(std::memory_order_relaxed);
        return (head & exclusive_mask) == (tail & exclusive_mask)
            && tail_.compare_exchange_strong(tail, tail+shared_step, std::memory_order_relaxed);
    }

    /**
     * @brief 解锁，共享的锁
     * 
     */
    void read_unlock() noexcept {
        head_.fetch_add(shared_step, std::memory_order_release);
    }

private:
    using base_type = std::uint32_t;
    static constexpr base_type shared_step = 1 << (8 * sizeof(base_type) / 2);
    static constexpr base_type exclusive_mask = shared_step - 1;
    static constexpr base_type exclusive_step = 1;

    std::atomic<base_type> head_ = ATOMIC_VAR_INIT(0);
    std::atomic<base_type> tail_ = ATOMIC_VAR_INIT(0);
};

}  // namespace file_io_hook
