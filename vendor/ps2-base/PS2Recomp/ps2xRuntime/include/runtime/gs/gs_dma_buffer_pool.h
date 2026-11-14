#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

// Retention limits apply to actual vector capacity, including rounding. In-flight
// buffers belong to their consumer and cannot be borrowed again. Oversize DMA
// still works, but its storage is released instead of entering the idle cache.
class GsDmaBufferPool
{
public:
    static constexpr size_t kTotalBytes = 16u << 20;
    static constexpr size_t kMaxBufferBytes = 8u << 20;
    static constexpr size_t kMaxBuffers = 8;
    static constexpr size_t kPerClass = 2;
    class Buffer
    {
    public:
        std::vector<uint8_t> bytes;
        Buffer(Buffer &&other) noexcept : bytes(std::move(other.bytes)), m_pool(std::exchange(other.m_pool, nullptr)) {}
        Buffer &operator=(Buffer &&other) noexcept
        {
            if (this != &other) { release(); bytes = std::move(other.bytes); m_pool = std::exchange(other.m_pool, nullptr); }
            return *this;
        }
        ~Buffer() { release(); }
        const uint8_t *data() const { return bytes.data(); }
        size_t size() const { return bytes.size(); }
        bool empty() const { return bytes.empty(); }
        Buffer() = default;
        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;
    private:
        friend class GsDmaBufferPool;
        explicit Buffer(GsDmaBufferPool *pool) : m_pool(pool) {}
        void release() noexcept { if (m_pool) m_pool->recycle(std::move(bytes)); m_pool = nullptr; }
        GsDmaBufferPool *m_pool = nullptr;
    };
    Buffer acquire(size_t requested = 0)
    {
        Buffer out(this);
        const size_t target = sizeClass(requested);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_spare.begin(); it != m_spare.end(); ++it)
                if (it->capacity() >= requested && sizeClass(it->capacity()) == target)
                {
                    m_retained -= it->capacity();
                    out.bytes = std::move(*it);
                    m_spare.erase(it);
                    break;
                }
        }
        if (out.bytes.capacity() < requested)
            out.bytes.reserve(requested <= kMaxBufferBytes ? target : requested);
        return out;
    }
    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_spare.clear();
        m_retained = 0;
    }
    size_t retainedBytes() const { std::lock_guard<std::mutex> lock(m_mutex); return m_retained; }
    size_t retainedBuffers() const { std::lock_guard<std::mutex> lock(m_mutex); return m_spare.size(); }
private:
    static size_t sizeClass(size_t bytes)
    {
        size_t size = 16;
        while (size < bytes && size < kMaxBufferBytes) size *= 2;
        return bytes > kMaxBufferBytes ? bytes : size;
    }
    void recycle(std::vector<uint8_t> &&buffer) noexcept
    {
        const size_t capacity = buffer.capacity();
        if (!capacity || capacity > kMaxBufferBytes) return;
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_spare.size() >= kMaxBuffers || capacity > kTotalBytes - m_retained) return;
            const size_t group = sizeClass(capacity);
            size_t count = 0;
            for (const auto &spare : m_spare) count += sizeClass(spare.capacity()) == group;
            if (count >= kPerClass) return;
            buffer.clear();
            m_spare.push_back(std::move(buffer));
            m_retained += capacity;
        }
        catch (...) { /* recycling never turns successful DMA into a failure */ }
    }
    mutable std::mutex m_mutex;
    std::vector<std::vector<uint8_t>> m_spare;
    size_t m_retained = 0;
};
