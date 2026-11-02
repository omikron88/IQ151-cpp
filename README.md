# IQ-151 — C++11 / SDL2

Nativní port [Java emulátoru IQ-151](https://github.com/omikron88/iq-151)
(revize `5bd21af8a332c14483f184bb30f7074c1eea80d8`) pro Linux a Windows/MSYS2.
Nepotřebuje Javu. ROM, znakový generátor, SD zavaděč a původní ikony jsou
součástí spustitelného souboru. Licence GPL-3.0-or-later; původ a licence
převzatých částí jsou v [NOTICE](NOTICE).

## Co je převedeno

- CPU 8080/8085 včetně rozšířených instrukcí, přerušení, 8259 a 8255.
- 32/64 KB RAM, bootstrap ROM, BASIC 6, BASIC G, AMOS s přepínáním
  Assembler/Pascal/Pascal1/RAM, čtyři varianty monitoru a Disk2 ROM.
- Video32, Video64, inverzní a zdvojené znaky, Grafik 512 × 256.
  Původní znakový generátor, okraje a poměr pixelů 1:2; volitelně zelený/jantarový obraz.
- Původní klávesnicová matice, chytrá klávesnice, FA/FB/BR, schránka a výběr textu myší.
- Kazety CSW v2/RLE a PCM WAV (8/16 bitů); záznam do CSW, inverze a převinutí.
- Disk2/8272 se dvěma mechanikami pro původní raw IQD obrazy
  256 256 B (77 stop × 26 sektorů × 128 B), čtení/zápis a ochrana proti zápisu.
- SD-ROM: vestavěný zavaděč, adresáře, načítání BASIC/HEX a ukládání BASIC/HEX
  přes handshake 8255; bez pomocného vlákna a jeho původních závodů.
- Zvuk přes SDL, pauza a rychlosti 0,5× až 40×.
- Debugger: registry, krokování, krok přes CALL/RST, disassembler s Intel/Z80
  mnemonikou, paměť, pět breakpointů a breakpoint při zápisu.
- Binární bloky, Intel HEX s kontrolními součty, kompatibilní snapshoty `.isn` v1,
  rychlý slot, screenshoty PPM, nastavení a přetažení souborů do okna.

Rozhraní používá SDL2 a Dear ImGui, nikoli Swing; zachovává uspořádání menu,
lištu s původními ikonami a vlastní obraz počítače. Časování instrukcí vychází
z Java implementace; nejde o nově prokázanou taktově přesnou simulaci skutečné 8085.

## Linux

GCC nebo Clang s C++11, CMake ≥ 3.16 a SDL2 ≥ 2.0.18. Například na Debianu/Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build libsdl2-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/iq151
```

Pokud CMake nenajde systémové SDL2, standardně stáhne a sestaví připnuté SDL
2.32.10 s ověřením SHA-256. Pro běžný linuxový desktop musí být k dispozici
vývojové hlavičky odpovídajícího backendu, typicky X11 nebo Wayland/ALSA
(`libsdl2-dev` je zajistí). GUI lze sestavit i se softwarovým rendererem.

## Windows / MSYS2 UCRT64

V terminálu **UCRT64** (nikoli v základním MSYS shellu):

```sh
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/iq151.exe
```

Dynamický build používá DLL z UCRT64. Pro distribuci jediného EXE použijte
následující statickou variantu.

## Přenosný build

```sh
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DIQ151_STATIC=ON
cmake --build build-static --parallel 4
ctest --test-dir build-static --output-on-failure
./build-static/iq151 --portable
```

`IQ151_STATIC=ON` sestaví a staticky připojí SDL2, Dear ImGui i runtime C++.
Na Windows nejsou potřeba SDL2, MinGW ani MSYS DLL; zůstávají pouze systémové
DLL Windows. Na Linuxu zůstávají libc, libm, systémový loader a systémové
služby displeje/zvuku; SDL je podle dostupného backendu otevírá za běhu.
Linuxové sestavení není univerzální binárka pro libovolnou verzi glibc:
pro distribuci na starší systémy sestavujte na nejstarší podporované distribuci.

ROM a ostatní vestavěná data jsou v binárce v obou variantách. Pro provoz není
nutné přibalovat adresář `roms` ani zdroje knihoven. Herní soubory a obrazy
kazet/disket jsou uživatelská data, nikoli ROM, a zůstávají externí.

Přepínač `--portable` ukládá nastavení, rychlý slot a výchozí `SDRoot` vedle
binárky; tento adresář musí být zapisovatelný. Bez něj se používá uživatelský
adresář vrácený `SDL_GetPrefPath`. Při zapnutí SD-ROM se do jeho kořene bezpečně
vytvoří chybějící `__BLOADER.BAS`. `--sd-root DIR` umožní zvolit jiný kořen.

```sh
cmake --install build-static --prefix package
# nebo ZIP/TGZ s binárkou a licenčními texty:
cd build-static
cpack -G ZIP
```

K distribuované binárce zajistěte odpovídající zdrojový kód podle GPL a licenční
texty uvedené v NOTICE. Je-li potřeba sestavení bez sítě, rozbalte odpovídající
SDL archiv předem a přidejte `-DFETCHCONTENT_SOURCE_DIR_SDL2=/cesta/k/SDL`.

## Ovládání

| Klávesa / akce | Význam |
| --- | --- |
| F1–F5 | Funkční klávesy IQ |
| Levý / pravý Alt | FA / FB |
| F6 / F7 | BR / reset |
| Ctrl+P | Pauza / pokračování |
| F8 / F9 | Uložit / načíst snapshot |
| Ctrl+F8 / Ctrl+F9 | Rychlý slot |
| F12 | Debugger |
| F10 / F11 v debuggeru | Krok / krok přes podprogram |
| Pravé tlačítko na obrazovce | Vložit ze schránky |
| Výběr myší | Zkopírovat vybraný text |
| Pravé tlačítko na názvu diskety | Povolit zápis / vysunout |

Původní klávesnice odpovídá fyzickému US rozložení IQ; bez Shift píše velká
písmena. Chytrá klávesnice a vložení ze schránky převádějí zadávané znaky
včetně interpunkce. Nastavení hardwaru se aplikuje resetem. Snapshot se načítá
jen při stejné konfiguraci modulů; neplatný nebo krátký soubor stav nezmění.
Stejně jako Java verze `.isn` neukládá rozpracovaný přenos SD-ROM/Disk2,
připojená média, polohu kazety ani zvukovou frontu.

Kazetu otevřete přes **File → Open LOAD tape**, ponechte **Play** a v BASICu
použijte `MLOAD` (ukončení záznamu je ESC). Motor řídí emulovaný počítač.
Pro záznam otevřete nový SAVE soubor, zvolte Record a v počítači `MSAVE`;
**Finish recording** zapíše poslední puls a uzavře soubor.

SD-ROM zapněte spolu s SD autorun v Settings, případně:

```sh
./build/iq151 --sd-autorun --sd-root /cesta/k/SDRoot
```

Po bootu správce vyberte soubor kurzorovými klávesami a Enter. BASIC program
se načte; spustíte jej `RUN`. Diskety se otevírají chráněné proti zápisu.

Příkazové přepínače hardwaru volí konfiguraci z výchozích hodnot a mají přednost
před uloženým nastavením. `--help` vypíše všechny možnosti.

## Headless provoz a testy

```sh
./build/iq151 --headless --frames 120 --expect BASIC
./build/iq151 --headless --module basic6 --video 64 --frames 120 --save-state boot.isn
./build/iq151 --headless --load program.hex --pc 0x1000 --screenshot screen.ppm
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/iq151 --ui-smoke
```

Samostatné jádro bez SDL: `cmake -S . -B build-core -DIQ151_BUILD_GUI=OFF`.
Výchozí headless běh nepoužívá nastavení GUI ani displej.

CTest ověřuje:

- všech 256 opkódů ve 32 počátečních stavech (8 192 případů) proti výsledkům
  původní Javy: registry, příznaky, T-stavy, porty i celou paměť;
- boot osmi konfigurací BASIC/AMOS/monitor × Video32/64: RAM, VRAM, PC a obraz;
- pixelové hashe čtyř vzorů Grafiku/Video32/Video64 včetně zdvojených znaků;
- interoperabilitu `.isn` s Javou, odmítnutí poškozených souborů, bankování,
  přerušení, HEX/bin, kazetové kodeky a čtení/zápis/ochranu sektorů Disk2;
- skutečné `MLOAD`/`RUN` z CSW a SD-ROM boot, výpis adresáře, načtení/spuštění
  BASICu a uložení přes 8255;
- vykreslení SDL okna a vykonání `PRINT` z fyzické i chytré SDL klávesnice.

JDK je potřeba pouze k přegenerování referencí, nikoli pro běžné testy:
[tests/reference](tests/reference). Linuxové testy byly spuštěny včetně
ASan/UBSan. Windows binárka byla zde přeložena cross compilerem MinGW a její
importy zkontrolovány; běh na skutečném Windows/MSYS2 v tomto prostředí nebyl
k dispozici. Workflow GitHub Actions připravuje nativní Linux i MSYS2 testy;
jeho běh je ověřitelný až po uložení projektu na GitHub.
