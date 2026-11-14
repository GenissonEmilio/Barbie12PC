"""Import the US PS2 disc and translate its Emotion Engine code to C++."""

import argparse
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile

from compile_sources import prepare_sources
from compatibility import prepare_dma, prepare_dispatcher, prepare_graphics, prepare_iop, prepare_video


PROJECT = Path(__file__).resolve().parents[1]
BASE = PROJECT / "vendor/ps2-base"
BOOT = "SLUS_215.79"
BOOT_SHA256 = "71650530203adaad8423e19caa74ef17d10122e07e2fa5014601cf4cfee1a8c6"
ISO_NAME = "Barbie in the 12 Dancing Princesses (USA).iso"
ISO_SIZE = 1_485_602_816
RECOMP_CONFIG = PROJECT / "config/recomp.toml"


def import_disc(image, destination):
    spec = importlib.util.spec_from_file_location("iso9660", BASE / "tools/desktop/iso9660.py")
    iso9660 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(iso9660)
    with image.open("rb") as disc:
        if image.stat().st_size != ISO_SIZE:
            raise ValueError("O tamanho da imagem difere da edição USA v1.00.")
        files, _ = iso9660.scan(disc)
        entries = {name: (lba, size) for name, lba, size in files}
        if not {BOOT, "DATA.WAD", "DATA.STR", "MODULES/RTFSSIOP.IRX", "MODULES/STREAM.IRX"} <= entries.keys():
            raise ValueError("Estrutura de arquivos inesperada para a edição USA.")
        lba, size = entries[BOOT]
        disc.seek(lba * iso9660.SECTOR)
        boot = disc.read(size)
        if hashlib.sha256(boot).hexdigest() != BOOT_SHA256:
            raise ValueError("A revisão do executável de PS2 não corresponde à edição USA v1.00.")

        (destination / "disc").mkdir()
        for name, lba, size in files:
            target = destination / "disc" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            disc.seek(lba * iso9660.SECTOR)
            with target.open("wb") as output:
                remaining = size
                while remaining:
                    data = disc.read(min(remaining, 1024 * 1024))
                    if not data:
                        raise ValueError(f"Imagem truncada ao ler {name}.")
                    output.write(data)
                    remaining -= len(data)


def prepare(input_path, recompiler, destination, sevenzip):
    if destination.exists():
        raise ValueError(f"Escolha uma pasta nova para a preparação: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".barbie-", dir=destination.parent) as tmp:
        stage = Path(tmp) / "build"
        stage.mkdir()
        image = input_path
        if input_path.suffix.lower() == ".7z":
            image = stage / "disc.iso"
            with image.open("wb") as output:
                subprocess.run([sevenzip, "x", "-so", "-mmt=2", "--", input_path, ISO_NAME],
                               stdout=output, check=True)
        elif input_path.suffix.lower() != ".iso":
            raise ValueError("Informe a imagem .iso ou o .7z original.")
        import_disc(image, stage)

        config = stage / "recomp.toml"
        shutil.copyfile(RECOMP_CONFIG, config)
        with (stage / "recomp.log").open("wb") as log:
            result = subprocess.run([recompiler, config.name], cwd=stage, stdout=log,
                                    stderr=subprocess.STDOUT)
        if result.returncode:
            with (stage / "recomp.log").open(encoding="utf-8", errors="replace") as log:
                for line in log:
                    if "[error]" in line:
                        print(line.rstrip())
            raise ValueError(f"O PS2Recomp terminou com código {result.returncode}.")
        prepare_sources(stage / "generated", stage / "batches")
        prepare_dispatcher(BASE, stage / "batches/Dispatcher.cpp")
        prepare_iop(BASE, stage / "iop")
        prepare_video(BASE, stage / "video")
        prepare_graphics(BASE, stage / "video/gs_hw_backend.cpp")
        prepare_dma(BASE, stage / "video/ps2_memory.cpp")
        with (stage / "disc-image.txt").open("w", encoding="utf-8") as out:
            print(destination / image.name if image.parent == stage else image, file=out)

        stage.rename(destination)
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disc", type=Path)
    parser.add_argument("recompiler", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--7z", dest="sevenzip", default=shutil.which("7z") or "7z")
    args = parser.parse_args()
    try:
        print(prepare(args.disc.resolve(strict=True), args.recompiler.resolve(strict=True),
                      args.output.resolve(), args.sevenzip))
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"{exc}\n")
