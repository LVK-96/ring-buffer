#include <iostream>
#include <memory>
#include <cassert>
#include <tuple>
#include <thread>
#include <chrono>
#include <algorithm>

#include "ring_buffer.h"

namespace {

template<typename T>
class BufferWriter
{
    private:
        std::shared_ptr<ring_buffer::BaseRingBuffer<T>> buf;
        std::random_device rng_dev;
        std::mt19937 rng;
        std::uniform_int_distribution<std::mt19937::result_type> dist;


    public:
        BufferWriter(std::shared_ptr<ring_buffer::BaseRingBuffer<T>> buf, int dist_min = 1, int dist_max = 1000) : buf(buf), rng(rng_dev()), dist(dist_min, dist_max) {}

        void random_write()
        {
            std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
            auto t2 = t1;
            std::chrono::duration<double, std::milli> time_span = t2 - t1;

            while(time_span.count() < 5000) {
                buf->write(dist(rng));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                t2 = std::chrono::high_resolution_clock::now();
                time_span = t2 - t1;
            }
        }

        void pattern_write(int seed = 0)
        {
            for (size_t i = seed; i < 10 + seed; ++i) {
                buf->write(i);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
};

template<typename T>
class BufferReader
{
    private:
        std::shared_ptr<ring_buffer::BaseRingBuffer<T>> buf;

    public:
        BufferReader(std::shared_ptr<ring_buffer::BaseRingBuffer<T>> buf) : buf(buf), read_values({}) {}
        BufferReader(std::shared_ptr<ring_buffer::BaseRingBuffer<T>> &&buf) : buf(buf), read_values({}) {}

        std::vector<T> read_values;

        void read()
        {
            std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
            auto t2 = t1;
            std::chrono::duration<double, std::milli> time_span = t2 - t1;

            while(time_span.count() < 5000) {
                auto read = buf->read();
                if (read) read_values.emplace_back(read.value());
                t2 = std::chrono::high_resolution_clock::now();
                time_span = t2 - t1;
            }
        }
};

void ring_buf_test(ring_buffer::BaseRingBuffer<int> &ring_buf)
{
    // Writer signals to reader using this shared boolean that it has written everything
    std::atomic_bool writer_done = false;

    auto write_helper = [&ring_buf, &writer_done](size_t nro_entries_to_write, bool multithreaded=false) {
        // Write to the buffer
        for (size_t i = 0; i < nro_entries_to_write; ++i) {
            while (multithreaded && ring_buf.full()) {
                // Sleep untill ring_buffer is not full again in multithreaded test
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            ring_buf.write(i);
        }
        // Check that buffer does not report size over max capacity
        assert(!((ring_buf.size() > ring_buf.capacity())) && "Ring buffer size over max capacity!");
        if (!multithreaded) {
            // Check that the size is as expected
            assert((ring_buf.size() == std::min(nro_entries_to_write, ring_buf.capacity())) && "Ring buffer size is wrong!");
        }
        writer_done = true;
    };

    auto read_helper = [&ring_buf, &writer_done](size_t expected_nro_reads, int expected_offset=0, bool multithreaded=false) {
        size_t entries_read = 0;
        for(size_t i = 0; !(writer_done && ring_buf.empty()); ++i) {
            auto elem = ring_buf.read();
            if (!multithreaded) {
                // Check that a value is returned and that it matches the expected value
                assert(elem && "Ring buffer should return value if it is not empty!");
                assert((elem.value() == (i + expected_offset)) && "Ring buffer returned wrong value!");
            }
            if (elem) entries_read++;
        }
        // Check the ring buffer is empty
        assert(ring_buf.empty() && "Ring buffer should be empty after reading all elements!");
        // Check we read the expected amount of values
        assert((entries_read == expected_nro_reads) && "Did not read expected number of enteries from read buffer!");
        writer_done = false;
    };

    // Write to the buffer 1 over capacity, should overwrite the first element
    write_helper(ring_buf.capacity() + 1);
    // Read the values written to the buffer
    read_helper(ring_buf.size(), 1);

    // Write to the buffer 10 over capacity, should overwrite 10 first elements
    write_helper(ring_buf.capacity() + 10);
    // Read the values written to the buffer
    read_helper(ring_buf.size(), 10);

    // Write to the buffer at capacity, should not overwrite any elements
    write_helper(ring_buf.capacity());
    // Read the values written to the buffer
    read_helper(ring_buf.size());

    // Write to the buffer under capacity, should not overwrite any elements
    write_helper(ring_buf.capacity() - 1);
    // Read the values written to the buffer
    read_helper(ring_buf.size());

    // Check that clear works
    ring_buf.write(1);
    ring_buf.clear();
    assert(ring_buf.empty() && "Ring buffer should be empty after a clear!");

    // Parallel read and write
    std::thread writer_thread(write_helper, ring_buf.capacity() + 42, true);
    std::thread reader_thread(read_helper, ring_buf.capacity() + 42, 0, true);
    writer_thread.join();
    reader_thread.join();

    std::thread writer_thread2(write_helper, std::max(static_cast<int>(ring_buf.capacity()) - 420, 10), true);
    std::thread reader_thread2(read_helper, std::max(static_cast<int>(ring_buf.capacity()) - 420, 10), 0, true);
    writer_thread2.join();
    reader_thread2.join();

    std::thread writer_thread3(write_helper, ring_buf.capacity() + 69, true);
    std::thread reader_thread3(read_helper, ring_buf.capacity() + 69, 0, true);
    writer_thread3.join();
    reader_thread3.join();
}

} // anonymous namespace

int main()
{
    // A small program to test the Ring Buffers
    constexpr bool do_simple  = true;
    constexpr bool do_pattern = true;
    constexpr bool do_random  = true;

    auto tests = [](std::shared_ptr<ring_buffer::BaseRingBuffer<int>> &&buf) {
        if constexpr(do_simple) {
            ring_buf_test(*buf);
        }

        if constexpr (do_pattern || do_random) {
            BufferWriter<int> writer1(buf);
            BufferReader<int> reader1(buf);
            BufferWriter<int> writer2(buf);
            BufferReader<int> reader2(buf);

            // Pre reserve some space in vectors for read values
            reader1.read_values.reserve(100);
            reader2.read_values.reserve(100);

            if constexpr (do_pattern) {
                // Write a static pattern
                std::thread writer1_thread_pattern(&BufferWriter<int>::pattern_write, &writer1, 0);
                std::thread reader1_thread_pattern(&BufferReader<int>::read, &reader1);
                std::thread writer2_thread_pattern(&BufferWriter<int>::pattern_write, &writer2, 20);
                std::thread reader2_thread_pattern(&BufferReader<int>::read, &reader2);
                writer1_thread_pattern.join();
                reader1_thread_pattern.join();
                writer2_thread_pattern.join();
                reader2_thread_pattern.join();
            }

            // Write some random numbers
            if constexpr (do_random) {
                std::thread writer1_thread_random(&BufferWriter<int>::random_write, &writer1);
                std::thread reader1_thread_random(&BufferReader<int>::read, &reader1);
                std::thread writer2_thread_random(&BufferWriter<int>::random_write, &writer2);
                std::thread reader2_thread_random(&BufferReader<int>::read, &reader2);
                writer1_thread_random.join();
                reader1_thread_random.join();
                writer2_thread_random.join();
                reader2_thread_random.join();
            }

            std::cout << "Values read by reader1:" << std::endl;
            for (const auto &it : reader1.read_values) std::cout << it << std::endl;
            std::cout << "Values read by reader2:" << std::endl;
            for (const auto &it : reader2.read_values) std::cout << it << std::endl;
        }
    };

    constexpr int capacity = 666;
    std::cout << "Testing FullFlagRingBuffer..." << std::endl;
    tests(
        static_cast<std::shared_ptr<ring_buffer::BaseRingBuffer<int>>>(
            std::make_shared<ring_buffer::FullFlagRingBuffer<int, capacity>>()
    ));
    std::cout << "Testing GuardElemRingBuffer..." << std::endl;
    tests(
        static_cast<std::shared_ptr<ring_buffer::BaseRingBuffer<int>>>(
            std::make_shared<ring_buffer::GuardElemRingBuffer<int, capacity>>()
    ));

    return 0;
}
