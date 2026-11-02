# Architecture and compatibility

The emulator core uses only C++11 and the standard library. SDL2 and Dear ImGui
are limited to the frontend. ROM embedding is a CMake configure step; neither
Python, Java, a ROM path nor an archive library is needed by the binary.

* `include/iq151/cpu.hpp`: translated Java opcode decoder, register/flag logic,
  interrupt entry and single-instruction execution. Java zero initialization is
  explicit; Java increments and shifts are translated without C++ sequence-point
  undefined behavior. Conditional flag behavior intentionally follows upstream.
* `pio.hpp`: original complete 8255 modes and handshake logic, translated to C++.
* `machine.cpp`: page mapping, bootstrap, AMOS/Disc2 ROM overlays, 8259,
  keyboard, frame pacing, video, CPU bus, speaker and autorun entry points.
* `media.cpp`: checked CSW/WAV readers, streaming CSW recording and bounded
  8272 command/execution/result phases. Disk writes require explicit write enable.
* `sdrom.cpp`: deterministic state machine in place of Java's ATmega threads;
  the same 8255 ports and handshake are used by the original embedded loader.
  Filesystem work is local; nothing is sent over the network at runtime.
* `preferences.cpp`: backward-compatible line-based `iq151.ini` parsing;
  `Config::sdRoot` preserves host paths with spaces/backslashes. GUI startup uses
  CLI `--sd-root`, then the saved path, then the default preference/portable
  `SDRoot`. Hardware CLI overrides retain this host setting. Machine reset picks
  up a changed Config path. The folder browser runs in the Settings ImGui context.
* `snapshot.cpp`: Java ISN v1 layout. Length/configuration/enum validation precedes
  state changes. Peripheral transactions, mounted files and the host SD folder
  are not snapshots; the original 10-byte hardware config block is unchanged.
* `frontend.cpp`: SDL window/events/audio, original icons, menus, dialogs,
  debugger, preferences and file browser. Input/focus loss releases held keys.
* `ui_window.cpp`: independent, non-modal SDL Settings/debugger windows with
  one SDL renderer and ImGui context/font atlas each. Events are routed by SDL
  window ID; only the main window sends input to the IQ keyboard. Hidden tools
  retain their geometry/editor state, and are destroyed before the main context
  and SDL_Quit. Windows uses a Win32 owner (GWLP_HWNDPARENT); X11 uses SDL2's
  SetWindowModalFor to set WM_TRANSIENT_FOR without disabling main-window input.
  SDL2 drivers without parent support use application-managed independent windows.
  Local ImGui SDL backend fixes prevent inactive contexts from releasing another
  window's mouse capture or replacing its cursor.

Video uses the original 560 x 272 bitmap, with active pixels at (20,8), displayed
at 560 x 544. Video64 double-width control codes can skip cells. The renderer
retains their previous pixels like Java's BufferedImage, rather than clearing
those cells on every call. The test oracle includes this behavior.

The CSW codec respects run lengths and initial polarity. The machine adapter
retains the Java tape input polarity convention (its CswFile toggles before the
first run), so the default inversion setting loads existing upstream tapes.
New recordings contain a complete last run and correct CSW pulse count.

Linux static distribution means static application libraries and C++ runtime,
with libc/libm, the loader and OS display/audio facilities still external.
SDL may dynamically load X11/ALSA/etc. These are system facilities. It does not
load a separate SDL or ImGui library. A truly libc-free universal Linux binary
is not claimed. Build against an appropriate glibc baseline for distribution.

Validation is deliberately separated:

1. CPU oracle covers 256 x 32 individual instructions, including complete
   post-instruction RAM, port effects and the Java bus T-state count.
2. Boot oracle uses the original Java CPU, memory, 8255, 8259 and renderer;
   RAM/VRAM/PC/image and ISN round trips are checked for eight configurations.
3. Native integration executes BASIC commands from SDL and clipboard-style
   input, CSW tape and SD-ROM. Sector I/O, sound generation, malformed inputs,
   banking, interrupts and protection are exercised directly. The --window-smoke
   test creates/renders/resizes both native tools, verifies input isolation and
   single-step, applies a real Settings button click, closes/reopens both tools
   and checks context/window cleanup without destroying the main renderer.
4. Linux builds and tests run locally, including AddressSanitizer and UBSan.
   Windows is cross-compiled with MinGW; the PE imports contain only Windows
   system DLLs. Native MSYS2 execution is supplied as CI, not claimed as a local
   test. CP/M/Pascal disk applications and host audio hardware were not exhaustively
   exercised; passing sector tests does not establish every disk application's
   behavior.

Intentional UI/format differences: Dear ImGui replaces Swing; screenshots use
portable PPM rather than PNG; the quick slot is retained in the preference or
portable directory. Compressed CSW (compression 2), WAV codecs other than PCM
8/16-bit and arbitrary floppy geometries are rejected with a message. Network,
serial host passthrough and physical floppy access are not implemented, as the
upstream machine's serial callbacks also did not provide host passthrough.
