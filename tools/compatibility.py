"""Adapt the pinned runtime's syscall dispatcher for the Barbie SDK."""

from pathlib import Path


def prepare_dispatcher(base, destination):
    kernel = base / "PS2Recomp/ps2xRuntime/src/lib/Kernel"
    text = (kernel / "Syscalls/Dispatcher.cpp").read_text(encoding="utf-8")
    anchor = "        case 0x7F:\n"
    if text.count(anchor) != 1 or "case 0x7A:" in text:
        raise ValueError("Dispatcher incompatível com a revisão esperada do GodOfWar2PC.")
    # Numeric SIF syscalls are issued by Sony's SDK wrappers during IOP boot.
    sif_cases = (
        "        case 0x79:\n"
        "            ps2_stubs::sceSifSetReg(rdram, ctx, runtime);\n"
        "            return true;\n"
        "        case 0x7A:\n"
        "            ps2_stubs::sceSifGetReg(rdram, ctx, runtime);\n"
        "            return true;\n"
    )
    text = text.replace('#include "Common.h"', '#include "Syscalls/Common.h"')
    text = text.replace('#include "Dispatcher.h"', '#include "Syscalls/Dispatcher.h"')
    text = text.replace('#include "System.h"', '#include "Syscalls/System.h"')
    destination.write_text('#include "Stubs/SIF.h"\n' + text.replace(anchor, sif_cases + anchor),
                           encoding="utf-8")
    return kernel / "Syscalls"


def prepare_iop(base, destination):
    """Stage the three IOP units that depend on the module/heap boundary."""
    source = base / "PS2Recomp/ps2xIOP/src/emulator"
    for name in ("core/iop_memory.h", "core/iop_memory.cpp", "services/iop_module_loader.cpp", "iop_emulator.cpp"):
        text = (source / name).read_text(encoding="utf-8")
        if name == "core/iop_memory.h":
            expected = "HeapBase = 0x00070000u"
            if text.count(expected) != 1:
                raise ValueError("Limite da arena IOP incompatível com a revisão esperada.")
            # The final disc module extends to 0x7dd90 in low IOP RAM.
            text = text.replace(expected, "HeapBase = 0x00080000u")
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")


def prepare_video(base, destination):
    """Remove game-specific guest pointers and let bounded stream callbacks return."""
    runtime = base / "PS2Recomp/ps2xRuntime/src/lib/Kernel"
    mpeg = (runtime / "Stubs/MPEG.cpp").read_text(encoding="utf-8")
    scheduler = (runtime / "EeScheduler.cpp").read_text(encoding="utf-8")

    start = mpeg.index("        static const uint32_t kRefGlobalAddrs[]")
    end = mpeg.index("        setReturnU32(ctx, 1u);", start)
    mpeg = mpeg[:start] + mpeg[end:]
    start = mpeg.index("        mpegGuestWrite32(rdram, 0x1717BCu")
    end = mpeg.index("        uint32_t setDynamicRet", start)
    mpeg = mpeg[:start] + mpeg[end:]
    for line in (
        "        mpegGuestWrite32(rdram, uVar3 + 0x1C, 0x1209F8u);\n",
        "        mpegGuestWrite32(rdram, uVar3 + 0x24, 0x120A08u);\n",
        "        mpegGuestWrite32(rdram, 0x171904u, 0u);\n",
    ):
        if mpeg.count(line) != 1:
            raise ValueError("Implementação de MPEG incompatível com a revisão esperada.")
        mpeg = mpeg.replace(line, "")

    anchor = "                R5900Context nested = callbackCtx;"
    if mpeg.count(anchor) != 1:
        raise ValueError("Callback MPEG incompatível com a revisão esperada.")
    mpeg = mpeg.replace(anchor, """                struct CallbackScope {
                    CallbackScope() { ++barbieMpegCallbackDepth; }
                    ~CallbackScope() { --barbieMpegCallbackDepth; }
                } callbackScope;
""" + anchor)
    mpeg = mpeg.replace('#include "Common.h"', '#include "Stubs/Common.h"')
    mpeg = "extern thread_local unsigned barbieMpegCallbackDepth;\n" + mpeg
    anchor = "bool EeScheduler::checkpointDue(uint32_t cycles) noexcept\n{\n    accountCycles(cycles);"
    if scheduler.count(anchor) != 1:
        raise ValueError("Agendador EE incompatível com a revisão esperada.")
    # The existing callback dispatcher has its own 4096-step bound. Keep counting
    # guest cycles and defer cooperative scheduler checkpoints during its call.
    scheduler = scheduler.replace(anchor, "thread_local unsigned barbieMpegCallbackDepth = 0;\n\n" + anchor
                                  + "\n    if (barbieMpegCallbackDepth != 0)\n"
                                    "        return m_stopRequested.load(std::memory_order_acquire);")
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "MPEG.cpp").write_text(mpeg, encoding="utf-8")
    (destination / "EeScheduler.cpp").write_text(scheduler, encoding="utf-8")


def prepare_graphics(base, destination):
    source = base / "PS2Recomp/ps2xRuntime/src/lib/gs/gpu/gs_hw_backend.cpp"
    text = source.read_text(encoding="utf-8")
    start = text.index("        // a draw through a different-format view")
    end = text.index("        if (havePending", start)
    # This title switches a framebuffer between 32-bit and 16-bit render
    # targets. Flush old work and recreate the host target at the new format.
    replacement = """        for (RenderTarget &t : targets)
            if (t.fbp == s.context.frame.fbp && t.usedW != 0u && family16(t.psm) != family16(fpsm))
            {
                flushPending();
                destroy(t);
                createTarget(t, s.context.frame.fbp, s.context.frame.fbw);
                break;
            }
"""
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text[:start] + replacement + text[end:], encoding="utf-8")

    frontend = (source.parent.parent / "gs_frontend.cpp").read_text(encoding="utf-8")
    anchor = "    auto &input = m_gifInput[pathIndex];"
    if frontend.count(anchor) != 1:
        raise ValueError("Leitor de pacotes GIF incompatível com a revisão esperada.")
    # The opening movie can leave an incomplete Path 3 packet in this runtime.
    # RenderWare opens its independent image chains with two packed A+D tags.
    # Match their complete transfer preamble before resetting the Path 3 cursor.
    packet_start = """
    if (input.pending() && path == GifPathId::Path3 && sizeBytes >= 112u &&
        loadLE64(data) == 0x1000000000000001ull && loadLE64(data + 8u) == 0x0E &&
        loadLE64(data + 24u) == GS_REG_BITBLTBUF &&
        loadLE64(data + 32u) == 0x1000000000000003ull && loadLE64(data + 40u) == 0x0E &&
        loadLE64(data + 56u) == GS_REG_TRXPOS && loadLE64(data + 72u) == GS_REG_TRXREG &&
        loadLE64(data + 88u) == GS_REG_TRXDIR)
    {
        input = {};
    }
"""
    destination.with_name("gs_frontend.cpp").write_text(
        frontend.replace(anchor, anchor + packet_start), encoding="utf-8")


def prepare_dma(base, destination):
    source = base / "PS2Recomp/ps2xRuntime/src/lib/ps2_memory.cpp"
    text = source.read_text(encoding="utf-8")
    anchor = "        if (end || irq)\n"
    if text.count(anchor) != 1:
        raise ValueError("Leitor de cadeias DMA incompatível com a revisão esperada.")
    text = text.replace(anchor, "        if (end || (irq && config != m_ioRegisters.end() && (config->second & 0x80u)))\n")
    # Synchronous DMA must copy tags whose SPR bit selects scratchpad memory.
    anchor = "        if (transferTags)\n            bytes.insert"
    if text.count(anchor) != 1:
        raise ValueError("Transferência de tags DMA incompatível com a revisão esperada.")
    text = text.replace(anchor, "        if ((tag >> 63u) != 0u && (id == 0u || id == 3u || id == 4u))\n"
                                "            data = 0x70000000u | (data & 0x3FFFu);\n"
                                + anchor)
    destination.write_text(text, encoding="utf-8")
