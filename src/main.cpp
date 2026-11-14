#include "ps2_runtime.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

bool installMovieCompatibility(PS2Runtime &runtime);

namespace
{
void useDefault(const char *name, const char *value)
{
    if (std::getenv(name))
        return;
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
}
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "Uso: Barbie12PC <SLUS_215.79> <imagem.iso>\n";
        return 2;
    }

    try
    {
        const auto elf = std::filesystem::absolute(argv[1]);
        const auto iso = std::filesystem::absolute(argv[2]);
        if (!std::filesystem::is_regular_file(elf) || !std::filesystem::is_regular_file(iso))
        {
            std::cerr << "O executável e a imagem de disco precisam existir.\n";
            return 2;
        }

        useDefault("GOW_WINDOW", "960x720");
        useDefault("PS2X_GS_HW_SCALE", "2");
        auto paths = PS2Runtime::getIoPaths();
        paths.cdImage = iso;
        PS2Runtime::setIoPaths(paths);

        PS2Runtime runtime;
        if (!runtime.initialize("Barbie in the 12 Dancing Princesses") ||
            !runtime.loadELF(elf.string()) || !installMovieCompatibility(runtime))
            return 1;

        runtime.setPrivateGuestHeap(0x000A0000u, 0x000FF000u);
        runtime.run();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
