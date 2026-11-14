"""Build using the existing GodOfWar2PC portable Linux compiler kit."""

import concurrent.futures
import os
from pathlib import Path
import subprocess
from threading import Event


def build(kit, base, generated, work, source):
    tc = kit / "toolchain"
    libs = kit / "lib"
    sdk = kit / "source"
    runtime = base / "PS2Recomp" / "ps2xRuntime"
    iop = base / "PS2Recomp" / "ps2xIOP"
    gcc = tc / "lib/gcc/x86_64-linux-gnu/13"
    syslib = tc / "sysroot/usr/lib/x86_64-linux-gnu"
    env = os.environ.copy()
    env.update({
        "LD_LIBRARY_PATH": str(tc / "lib"),
        "GCC_EXEC_PREFIX": str(tc / "lib/gcc") + "/",
        "PATH": str(tc / "bin") + os.pathsep + env["PATH"],
    })
    flags = [
        tc / "bin/g++", f"--sysroot={tc / 'sysroot'}", f"-B{gcc}/",
        "-std=c++20", "-O0", "-DNDEBUG", "-msse4.1", "-fno-strict-aliasing",
        "-nostdinc++", "-isystem", tc / "include/c++/13",
        "-isystem", tc / "include/x86_64-linux-gnu/c++/13",
        "-isystem", tc / "include/c++/13/backward",
        "-DPS2_RUNTIME_LOGS=1", "-DPS2X_ENABLE_IOP_RPC_TRACE=0",
        *(f"-I{path}" for path in (generated, runtime / "include",
                                    iop / "include", sdk / "raylib", runtime / "third_party/libmpeg2")),
    ]
    units = sorted(work.glob("functions-*.cpp")) + sorted(source.glob("*.cpp"))
    dispatcher = work / "Dispatcher.cpp"
    kernel = base / "PS2Recomp/ps2xRuntime/src/lib/Kernel"
    flags.extend(f"-I{kernel / subdir}" for subdir in ("", "Stubs", "Syscalls"))
    video = work.parent / "video"
    graphics = base / "PS2Recomp/ps2xRuntime/src/lib/gs"
    flags.extend(f"-I{path}" for path in (graphics.parent, graphics, graphics / "gpu"))
    iop_overlay = work.parent / "iop"
    iop_source = base / "PS2Recomp/ps2xIOP/src"
    flags.extend(f"-I{iop_source / subdir}"
                 for subdir in ("", "emulator", "emulator/core", "emulator/services"))
    units = [
        work / "register_functions.cpp", dispatcher,
        video / "EeScheduler.cpp", video / "MPEG.cpp", video / "gs_hw_backend.cpp",
        video / "gs_frontend.cpp", video / "ps2_memory.cpp",
        iop_overlay / "core/iop_memory.cpp", iop_overlay / "iop_emulator.cpp",
        iop_overlay / "services/iop_module_loader.cpp", *units,
    ]

    failed = Event()

    def compile_unit(unit):
        if failed.is_set():
            return None
        obj = work / (unit.stem + ".o")
        try:
            subprocess.run([*flags, "-c", unit, "-o", obj], env=env, check=True)
        except (OSError, subprocess.CalledProcessError):
            failed.set()
            raise
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        objects = []
        for obj in pool.map(compile_unit, units):
            objects.append(obj)
            if len(objects) % 25 == 0:
                print(f"C++ {len(objects)}/{len(units)}", flush=True)

    program = work / "Barbie12PC"
    libraries = [
        libs / "libps2_runtime.a", libs / "libps2_iop.a", libs / "liblibmpeg2.a",
        libs / "libraylib.a", libs / "libX11.so.6", syslib / "libGL.so",
    ]
    subprocess.run(
        [*flags, *objects, "-Wl,--start-group", *libraries, "-Wl,--end-group",
         "-static-libstdc++", "-static-libgcc", f"-Wl,-rpath-link,{libs}",
         f"-Wl,-rpath-link,{syslib}", f"-Wl,-rpath-link,{tc / 'lib'}",
         "-pthread", "-ldl", "-lm", "-o", program],
        env=env, check=True,
    )
    return program


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("kit", "base", "generated", "work", "source"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    print(build(*(getattr(args, name).resolve() for name in ("kit", "base", "generated", "work", "source"))))
