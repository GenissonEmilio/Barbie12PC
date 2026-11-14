#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 ]]; then
    printf 'Uso: %s <disco.iso ou disco.7z> <nova pasta de compilação>\n' "$0" >&2
    exit 2
fi

project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
base="$project/vendor/ps2-base"
kit="${BARBIE_LINUX_KIT:-$project/compiler}"
if [[ ! -x "$kit/bin/ps2_recomp" ]]; then
    kit="${BARBIE_LINUX_KIT:-$project/../GodOfWar2PC/artifacts/compiler}"
fi
work=$2
if [[ -x "$kit/bin/ps2_recomp" ]]; then
    python3 -B "$project/tools/prepare.py" "$1" "$kit/bin/ps2_recomp" "$work"
    python3 -B "$project/tools/linux_build.py" "$kit" "$base" "$work/generated" "$work/batches" "$project/src"
else
    cmake_build="${work%/}-cmake"
    config=(
        -S "$base/PS2Recomp" -B "$cmake_build"
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_PROJECT_PS2RetroX_INCLUDE=$project/tools/desktop.cmake"
        -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_ANALYZER=OFF
        -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_RUNNER_PCH=OFF
        -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_IOP_RPC_TRACE=OFF
        -DPS2X_ENABLE_SCCACHE=OFF
    )
    cmake "${config[@]}" -DPS2X_BUILD_RUNTIME=OFF -DBARBIE_BATCH_DIR=
    cmake --build "$cmake_build" --target ps2_recomp --parallel 2
    python3 -B "$project/tools/prepare.py" "$1" "$cmake_build/ps2xRecomp/ps2_recomp" "$work"
    work=$(cd -- "$work" && pwd)
    cmake "${config[@]}" -DPS2X_BUILD_RUNTIME=ON \
        "-DBARBIE_BATCH_DIR=$work/batches" "-DBARBIE_GENERATED_DIR=$work/generated" \
        "-DBARBIE_SOURCE_DIR=$project/src"
    cmake --build "$cmake_build" --target Barbie12PC --parallel 2
    cp -- "$cmake_build/Barbie12PC" "$work/batches/Barbie12PC"
fi
printf 'Execute ./play-linux.sh %q\n' "$work"
