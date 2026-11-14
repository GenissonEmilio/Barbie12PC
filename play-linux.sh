#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 1 ]]; then
    printf 'Uso: %s <pasta preparada por build-linux.sh>\n' "$0" >&2
    exit 2
fi

work=$(cd -- "$1" && pwd)
IFS= read -r image < "$work/disc-image.txt"
cd -- "$work"
exec "$work/batches/Barbie12PC" "$work/disc/SLUS_215.79" "$image"
