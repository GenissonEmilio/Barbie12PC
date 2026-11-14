# Barbie12PC

Port por recompilação estática de *Barbie in the 12 Dancing Princesses* para PCs x86-64. A edição aceita é a USA v1.00 de PlayStation 2, identificada por `SLUS_215.79`. As ferramentas importam a imagem ISO ou o arquivo `.7z` que contém a ISO original e geram o executável no computador do jogador.

O controle usa o mapeamento de gamepads da raylib e GLFW. A janela preserva a proporção 4:3 e usa filtro bilinear, com resolução interna 2× no backend OpenGL. **F11** alterna tela cheia. No teclado, as setas navegam, **X** ou **Espaço** confirma, **C** volta e **Enter** corresponde a Start.

## Linux

Instale Python 3, Git, CMake 3.21+, GCC 13 ou Clang recente e os pacotes de desenvolvimento de OpenGL e X11 usados pela raylib. Para importar `.7z`, instale também o comando `7z`.

```sh
./build-linux.sh '/caminho/Barbie in the 12 Dancing Princesses (USA).7z' ./work
./play-linux.sh ./work
```

A compilação também aceita o kit Linux do GodOfWar2PC em `compiler/`, no diretório irmão do projeto ou no caminho indicado por `BARBIE_LINUX_KIT`.

## Windows

Use PowerShell, Visual Studio 2022 Build Tools com **Desenvolvimento para Desktop com C++**, Windows SDK, CMake 3.21+, Git e Python 3. Para importar `.7z`, coloque o 7-Zip no PATH ou informe o caminho do executável com `-SevenZip`.

```powershell
.\build-windows.ps1 -Disc 'C:\Jogos\Barbie in the 12 Dancing Princesses (USA).7z'
.\play-windows.ps1 -Prepared .\build\disc-work
```

Os dois sistemas precisam de um driver com suporte a OpenGL 4.6 para a renderização em 2×. Mantenha a pasta preparada e a imagem de disco no local usado na compilação para que o lançador encontre o jogo. O runtime guarda os cartões de memória virtuais junto ao ELF extraído em `disc/mc0` e `disc/mc1`.

## Código e dados

Este projeto inclui o runtime e as ferramentas de recompilação sob GPL-3.0, com base no GodOfWar2PC em `395806782f33652838ca26eae3c6ead0e7ab52f3` e no [PS2Recomp](https://github.com/ran-j/PS2Recomp). O disco, o C++ traduzido do jogo e o executável resultante são gerados localmente a partir da cópia do jogador. Barbie e os materiais do jogo pertencem aos respectivos titulares.
