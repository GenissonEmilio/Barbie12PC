"""Prepare generated sources for desktop C++ compilers with bounded memory."""

import itertools
import re
from pathlib import Path


ASSIGNMENT = re.compile(r"        g_ps2RecompiledFunctionTable\[(\d+)\] = (\w+); // 0x[\da-f]+\n")


def compact_registration(source, destination):
    text = source.read_text(encoding="utf-8")
    start = "    GeneratedFunctionTableInitializer() {\n"
    end = "    }\n};\n"
    before, found, tail = text.partition(start)
    if not found:
        raise ValueError("Tabela de funções incompatível com a base do GodOfWar2PC.")
    assignments, found, after = tail.partition(end)
    matches = list(ASSIGNMENT.finditer(assignments))
    if not found or not matches or sum(len(match[0]) for match in matches) != len(assignments):
        raise ValueError("A tabela gerada contém atribuições inesperadas.")

    rows = ((int(match[1]), match[2]) for match in matches)
    runs = []
    for function, group in itertools.groupby(rows, key=lambda row: row[1]):
        first, _ = next(group)
        last = first
        for slot, _ in group:
            if slot != last + 1:
                runs.append((first, last, function))
                first = slot
            last = slot
        runs.append((first, last, function))

    with destination.open("w", encoding="utf-8") as output:
        output.write(before + start)
        output.write("        static constexpr struct Run { uint32_t first, last; "
                     "PS2Runtime::RecompiledFunction function; } runs[] = {\n")
        for first, last, function in runs:
            output.write(f"            {{{first}, {last}, {function}}},\n")
        output.write("        };\n"
                     "        for (const Run &run : runs)\n"
                     "            for (uint32_t slot = run.first; slot <= run.last; ++slot)\n"
                     "                g_ps2RecompiledFunctionTable[slot] = run.function;\n")
        output.write(end + after)


def prepare_sources(generated, output):
    output.mkdir(parents=True, exist_ok=True)
    registration = generated / "register_functions.cpp"
    functions = sorted(path for path in generated.glob("*.cpp") if path != registration)
    if not functions:
        raise ValueError("O PS2Recomp não gerou funções.")
    table = output / registration.name
    compact_registration(registration, table)

    sources = [table]
    batch = []
    size = 0

    def write_batch():
        source = output / f"functions-{len(sources):04}.cpp"
        source.write_text("".join(f'#include "{path.name}"\n' for path in batch), encoding="utf-8")
        sources.append(source)

    for function in functions:
        length = function.stat().st_size
        if batch and (size + length > 600_000 or len(batch) >= 48):
            write_batch()
            batch = []
            size = 0
        batch.append(function)
        size += length
    write_batch()
    return sources
