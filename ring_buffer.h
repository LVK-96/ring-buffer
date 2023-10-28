#include <array>
#include <vector>
#include <optional>
#include <random>
#include <mutex>
#include <thread>
#include <chrono>

#pragma once

// Interface to a thread-safe ring buffer/circular buffer
namespace ring_buffer {
template <typename T>
class BaseRingBuffer
{
    protected:
        size_t head; // Write idx
        size_t tail; // Read idx
        mutable std::mutex acces_mutex; // Mutual exclusion

    public:
        BaseRingBuffer() : head(0), tail(0) {}

        virtual std::optional<T> read() = 0;
        virtual void write(const T& data) = 0;
        virtual void clear() = 0;
        virtual bool full() const = 0;
        virtual bool empty() const = 0;
        virtual size_t size() const = 0;
        virtual size_t capacity() const = 0;
};

// Interface to a ring buffer whose capacity is known at compile time
template<typename T, size_t S>
class StaticCapacityRingBuffer : public BaseRingBuffer<T>
{
    public:
        StaticCapacityRingBuffer() : BaseRingBuffer<T>() {}

        size_t capacity() const override
        {
            return S;
        }

};

// A ring buffer implemented using a guard element
template <typename T, size_t S>
class GuardElemRingBuffer final : public StaticCapacityRingBuffer<T, S>
{
    private:
        constexpr static size_t true_capacity = S + 1; // One guard element
        std::array<T, true_capacity> buf;

        size_t idx_increment(const size_t idx) const
        {
            size_t new_idx = idx + 1;
            return new_idx % true_capacity;
        }

    public:
        GuardElemRingBuffer() : StaticCapacityRingBuffer<T, S>(), buf({}) {}

        std::optional<T> read() override
        {
            std::scoped_lock (this->acces_mutex);
            if (empty()) return {};
            T res = this->buf[this->tail];
            this->tail = idx_increment(this->tail);
            return res;
        }

        void write(const T& data) override
        {
            std::scoped_lock (this->acces_mutex);
            this->buf[this->head] = data;
            // Move tail if buffer is full
            if (full()) {
                // We just wrote over the oldest entry so tail should be moved one step forward
                this->tail = idx_increment(this->tail);
            }
            this->head = idx_increment(this->head);
        }

        void clear() override
        {
            std::scoped_lock (this->acces_mutex);
            this->head = this->tail;
        }

        bool full() const override
        {
            std::scoped_lock (this->acces_mutex);
            return idx_increment(this->head) == this->tail;
        }

        bool empty() const override
        {
            std::scoped_lock (this->acces_mutex);
            return this->head == this->tail;
        }

        size_t size() const override
        {
            std::scoped_lock (this->acces_mutex);
            if (full()) return this->capacity();
            else if (this->head >= this->tail) return this->head - this->tail;
            // tail is always behind head ->
            // head is on the next round of the buffer
            return (true_capacity + this->head) - this->tail;
        }

};

// A ring buffer implemented using a full flag
template <typename T, size_t S>
class FullFlagRingBuffer final : public StaticCapacityRingBuffer<T, S>
{
    private:
        std::array<T, S> buf;
        bool _full; // Use a full flag instead of guard element

        size_t idx_increment(const size_t idx) const
        {
            size_t new_idx = idx + 1;
            return new_idx % S;
        }

    public:
        FullFlagRingBuffer() : StaticCapacityRingBuffer<T, S>(), buf({}), _full(false) {}

        std::optional<T> read() override
        {
            std::scoped_lock (this->acces_mutex);
            if (empty()) return {};
            T res = this->buf[this->tail];
            this->tail = idx_increment(this->tail);
            _full = false;
            return res; } void write(const T& data) override {
            std::scoped_lock(this->acces_mutex);
            this->buf[this->head] = data;
            // Move tail if buffer is full
            if (full()) {
                // We just wrote over the oldest entry so tail should be moved one step forward
                this->tail = idx_increment(this->tail);
            }
            this->head = idx_increment(this->head);
            // Tail (if necessary) and head have been moved, we can update the full flag
            _full = this->head == this->tail;
        }

        void clear() override
        {
            std::scoped_lock (this->acces_mutex);
            this->head = this->tail;
            _full = false;
        }

        bool full() const override
        {
            std::scoped_lock(this->acces_mutex);
            return _full;
        }

        bool empty() const override
        {
            std::scoped_lock(this->acces_mutex);
            return (this->head == this->tail) && !_full;
        }

        size_t size() const override
        {
            std::scoped_lock (this->acces_mutex);
            if (_full) return S;
            else if (this->head >= this->tail) return this->head - this->tail;
            // tail is always behind head ->
            // head is on the next round of the buffer
            return (S + this->head) - this->tail;
        }
};
} // namespace ring_buffer
