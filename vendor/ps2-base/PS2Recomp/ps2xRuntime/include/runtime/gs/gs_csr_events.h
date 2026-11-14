#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

// Owned by PS2Memory, outside GSRegisters to preserve the guest register ABI.
// The CSR change and its event number have one publication point. Acknowledge
// must use the same lock: a poll never observes half of an acknowledge/re-raise.
class GsCsrEvents
{
public:
    struct Snapshot { uint64_t csr; std::array<uint64_t, 2> count; };
    void raise(std::atomic<uint64_t> &csr, uint32_t bits)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t previous = csr.fetch_or(bits, std::memory_order_acq_rel);
        for (unsigned bit = 0; bit < 2; ++bit)
            if ((bits & (1u << bit)) && !(previous & (1u << bit)))
                ++m_count[bit];
    }
    Snapshot snapshot(const std::atomic<uint64_t> &csr) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {csr.load(std::memory_order_acquire), m_count};
    }
    template<class Update> void update(std::atomic<uint64_t> &csr, Update change)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        change(csr);
    }
    void acknowledge(std::atomic<uint64_t> &csr, uint64_t bits)
    {
        update(csr, [bits](auto &reg) { reg.fetch_and(~bits, std::memory_order_acq_rel); });
    }
    void reset(std::atomic<uint64_t> &csr, uint64_t value = 0)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        csr.store(value, std::memory_order_release);
        m_count = {};
    }
private:
    mutable std::mutex m_mutex;
    std::array<uint64_t, 2> m_count{};
};

// EE-owned edge state. Masked events are consumed, while unmasking a still-set
// level delivers once. Acknowledge/re-raise between polls changes the count even
// if the level is identical; an already-cleared event is never delivered late.
inline uint32_t gsCsrInterruptEdges(const GsCsrEvents::Snapshot &snapshot, uint32_t masked,
                                   uint32_t &latched, std::array<uint64_t, 2> &seen)
{
    const uint32_t active = static_cast<uint32_t>(snapshot.csr) & 3u & ~masked;
    uint32_t changed = 0;
    for (unsigned bit = 0; bit < 2; ++bit)
    {
        if (snapshot.count[bit] != seen[bit]) changed |= 1u << bit;
        seen[bit] = snapshot.count[bit];
    }
    const uint32_t rising = active & (~latched | changed);
    latched = active;
    return rising;
}
