#include "runtime/gs/gs_stage.h"

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    struct Packet
    {
        GifPathId path = GifPathId::Path1;
        std::vector<uint8_t> data;
        std::function<void()> call;
        size_t charge = 16;
    };
    constexpr size_t kMaxQueuedBytes = 4u << 20;
    struct Stage
    {
        std::mutex mutex;
        std::condition_variable work, space, idle;
        std::deque<Packet> queue;
        std::vector<std::vector<uint8_t>> spare;
        size_t spareBytes = 0;
        // Includes the executing item. A packet larger than the budget is admitted
        // alone, so legal large DMA is neither truncated nor stranded forever.
        size_t queuedBytes = 0;
        bool busy = false;
        bool stopping = false;
        bool started = false;
        std::thread thread;
        std::thread::id threadId;
        ps2_gs_stage::ProcessFn process;
        std::function<void()> afterBatch;
    };
    Stage &stage() { static Stage s; return s; }
    void loop()
    {
        Stage &s = stage();
        for (;;)
        {
            Packet packet;
            {
                std::unique_lock<std::mutex> lock(s.mutex);
                s.work.wait(lock, [&] { return s.stopping || !s.queue.empty(); });
                if (s.queue.empty()) return;
                packet = std::move(s.queue.front());
                s.queue.pop_front();
                s.busy = true;
            }
            try
            {
                if (packet.call) packet.call();
                else s.process(packet.path, packet.data.data(), static_cast<uint32_t>(packet.data.size()));
                if (s.afterBatch) s.afterBatch();
            }
            catch (const std::exception &error)
            {
                std::fprintf(stderr, "[gs-stage] item failed: %s\n", error.what());
            }
            catch (...) { std::fprintf(stderr, "[gs-stage] item failed with unknown exception\n"); }
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                const size_t capacity = packet.data.capacity();
                if (capacity && capacity <= (1u << 20) && s.spare.size() < 8 && capacity <= kMaxQueuedBytes - s.spareBytes)
                {
                    packet.data.clear();
                    try { s.spare.push_back(std::move(packet.data)); s.spareBytes += capacity; }
                    catch (...) { /* idle storage is optional */ }
                }
                s.queuedBytes -= packet.charge;
                s.busy = false;
                if (s.queue.empty()) s.idle.notify_all();
            }
            s.space.notify_all();
        }
    }
    void enqueue(Packet packet, const uint8_t *data = nullptr, uint32_t size = 0)
    {
        Stage &s = stage();
        std::unique_lock<std::mutex> lock(s.mutex);
        // The stage is the only consumer. Waiting here from its own callback
        // would deadlock when the executing item has exhausted the byte budget.
        // Reject even a small nested item: accepting it sometimes would make
        // callback behavior depend on queue occupancy and weaken FIFO ordering.
        if (s.started && std::this_thread::get_id() == s.threadId)
            throw std::logic_error("GS stage callbacks cannot enqueue stage work");
        s.space.wait(lock, [&]
        {
            return !s.started || s.stopping || s.queuedBytes == 0 ||
                (packet.charge <= kMaxQueuedBytes && s.queuedBytes <= kMaxQueuedBytes - packet.charge);
        });
        if (!s.started || s.stopping) throw std::runtime_error("GS stage is not accepting work");
        if (size)
        {
            auto spare = std::find_if(s.spare.begin(), s.spare.end(), [size](const auto &buffer) { return buffer.capacity() >= size; });
            if (spare != s.spare.end())
            {
                s.spareBytes -= spare->capacity();
                packet.data = std::move(*spare);
                s.spare.erase(spare);
            }
            packet.data.assign(data, data + size);
        }
        const size_t charge = packet.charge;
        s.queue.push_back(std::move(packet)); // publish accounting only after successful insertion
        s.queuedBytes += charge;
        lock.unlock();
        s.work.notify_one();
    }
}

namespace ps2_gs_stage
{
    void start(ProcessFn process, std::function<void()> afterBatch)
    {
        Stage &s = stage();
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.started) return;
        if (!process) throw std::invalid_argument("GS stage requires a packet consumer");
        s.process = std::move(process);
        s.afterBatch = std::move(afterBatch);
        s.stopping = false;
        s.busy = false;
        s.queuedBytes = 0;
        s.thread = std::thread(loop);
        s.threadId = s.thread.get_id();
        s.started = true;
        std::fprintf(stderr, "[gs-stage] GS frontend pipelined behind VU1 (PS2X_GS_PIPELINE=0 disables)\n");
    }
    void stop()
    {
        Stage &s = stage();
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            if (!s.started) return;
            if (std::this_thread::get_id() == s.threadId)
                throw std::logic_error("GS stage cannot join itself");
            s.stopping = true;
        }
        s.work.notify_all();
        s.space.notify_all();
        s.thread.join(); // drain accepted items, including ordered display callbacks
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            s.started = false;
            s.threadId = {};
            s.process = {};
            s.afterBatch = {};
            s.queuedBytes = 0;
            s.spare.clear();
            s.spareBytes = 0;
        }
        s.space.notify_all();
        s.idle.notify_all();
    }
    bool running()
    {
        Stage &s = stage();
        std::lock_guard<std::mutex> lock(s.mutex);
        return s.started && !s.stopping;
    }
    bool onStageThread()
    {
        Stage &s = stage();
        std::lock_guard<std::mutex> lock(s.mutex);
        return s.started && std::this_thread::get_id() == s.threadId;
    }
    void push(GifPathId path, const uint8_t *data, uint32_t size)
    {
        if (!data || !size) return;
        Packet packet;
        packet.path = path;
        packet.charge = std::max<size_t>(16, size);
        enqueue(std::move(packet), data, size);
    }
    void pushCall(std::function<void()> call)
    {
        if (!call) return;
        Packet packet;
        packet.call = std::move(call);
        enqueue(std::move(packet));
    }
    void waitIdle()
    {
        if (onStageThread()) return;
        Stage &s = stage();
        std::unique_lock<std::mutex> lock(s.mutex);
        s.idle.wait(lock, [&] { return !s.started || (s.queue.empty() && !s.busy); });
    }
}
