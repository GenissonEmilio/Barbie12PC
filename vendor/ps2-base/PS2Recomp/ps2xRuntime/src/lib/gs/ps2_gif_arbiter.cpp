#include "runtime/gs/ps2_gif_arbiter.h"
#include <algorithm>
#include <cstring>

GifArbiter::GifArbiter(ProcessPacketFn processFn)
{
    setProcessPacketFn(std::move(processFn));
}

// GOW-Port: compatibilidad con clientes que no necesitan el identificador del PATH.
void GifArbiter::setProcessPacketFn(ProcessPacketFn fn)
{
    if (!fn) { m_processFn = {}; return; }
    m_processFn = [fn = std::move(fn)](GifPathId, const uint8_t *data, uint32_t size) { fn(data, size); };
}

bool GifArbiter::trackPath3Packet(const uint8_t *data, uint32_t sizeBytes)
{
    // GOW-Port: recorrer solo las etiquetas; saltar registros, pixels y el padding REGLIST.
    // El cursor sigue el FIFO PATH3 entre submit/drain sin retener sus payloads.
    auto &input = m_path3Input;
    bool containsImage = false;
    uint32_t offset = 0u;
    while (offset < sizeBytes)
    {
        if (input.payloadBytes)
        {
            containsImage |= input.format >= 2u; // GOW-Port: IMAGE e IMAGE2.
            const uint32_t bytes = std::min(input.payloadBytes, sizeBytes - offset);
            input.payloadBytes -= bytes;
            offset += bytes;
            continue;
        }

        const uint8_t *tag = data + offset;
        if (input.tagBytes || sizeBytes - offset < 16u)
        {
            const uint32_t bytes = std::min(16u - input.tagBytes, sizeBytes - offset);
            std::memcpy(input.tag.data() + input.tagBytes, data + offset, bytes);
            input.tagBytes += bytes;
            offset += bytes;
            if (input.tagBytes < 16u) break;
            tag = input.tag.data();
            input.tagBytes = 0u;
        }
        else
            offset += 16u;

        uint64_t lo = 0u;
        std::memcpy(&lo, tag, sizeof(lo));
        const uint32_t loops = static_cast<uint32_t>(lo & 0x7FFFu);
        input.format = static_cast<uint8_t>((lo >> 58u) & 3u);
        uint32_t nreg = static_cast<uint32_t>((lo >> 60u) & 15u);
        if (!nreg) nreg = 16u;
        if (input.format == 0u)
            input.payloadBytes = loops * nreg * 16u;
        else if (input.format == 1u)
            input.payloadBytes = ((loops * nreg + 1u) / 2u) * 16u;
        else
            input.payloadBytes = loops * 16u;
        containsImage |= input.format >= 2u && loops != 0u;
    }
    return containsImage;
}

void GifArbiter::submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl)
{
    if (!data || sizeBytes < 16 || !m_processFn || pathId < GifPathId::Path1 || pathId > GifPathId::Path3)
        return;

    GifArbiterPacket pkt;
    pkt.pathId = pathId;
    pkt.path2DirectHl = (pathId == GifPathId::Path2) && path2DirectHl;
    pkt.path3Image = (pathId == GifPathId::Path3) && trackPath3Packet(data, sizeBytes);
    pkt.data.resize(sizeBytes);
    std::memcpy(pkt.data.data(), data, sizeBytes);
    m_queue.push_back(std::move(pkt));
}

namespace
{
    struct ProcessingGuard
    {
        bool &processing;
        explicit ProcessingGuard(bool &value) : processing(value) { processing = true; }
        ~ProcessingGuard() { processing = false; }
    };
}

void GifArbiter::submitAndDrain(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl)
{
    if (data && sizeBytes >= 16u && m_processFn && !m_processing && m_queue.empty() &&
        (pathId == GifPathId::Path1 || pathId == GifPathId::Path2))
    {
        {
            ProcessingGuard guard(m_processing);
            m_processFn(pathId, data, sizeBytes);
        }
        // Reentrant submissions own their data and wait until the borrowed packet finishes.
        drain();
        return;
    }
    submit(pathId, data, sizeBytes, path2DirectHl);
    drain();
}

void GifArbiter::drain()
{
    if (!m_processFn || m_processing)
        return;
    ProcessingGuard guard(m_processing);
    while (!m_queue.empty())
    {
        // Preserve FIFO within each path. DIRECTHL/IMAGE is a head-of-path decision,
        // not a strict weak ordering suitable for stable_sort.
        std::stable_sort(m_queue.begin(), m_queue.end(),
                         [](const GifArbiterPacket &a, const GifArbiterPacket &b)
                         { return pathPriority(a.pathId) < pathPriority(b.pathId); });
        const uint64_t generation = m_resetGeneration;
        size_t remaining = m_queue.size();
        while (remaining && !m_queue.empty())
        {
            size_t selected = 0u;
            if (m_queue.front().pathId == GifPathId::Path2 && m_queue.front().path2DirectHl)
            {
                size_t path3 = 0u;
                while (path3 < remaining && m_queue[path3].pathId != GifPathId::Path3)
                    ++path3;
                if (path3 < remaining && m_queue[path3].path3Image)
                    selected = path3;
            }
            // Remove before invoking user code: appends cannot invalidate the packet,
            // nested drain() cannot replay it, and an exception retains only pending work.
            GifArbiterPacket packet = std::move(m_queue[selected]);
            m_queue.erase(m_queue.begin() + selected);
            --remaining;
            m_processFn(packet.pathId, packet.data.data(), static_cast<uint32_t>(packet.data.size()));
            // reset() discards the current round. Newly submitted packets belong
            // to a new round and must be sorted again before arbitration.
            if (generation != m_resetGeneration)
                break;
        }
    }
}

uint8_t GifArbiter::pathPriority(GifPathId id)
{
    return static_cast<uint8_t>(id);
}
