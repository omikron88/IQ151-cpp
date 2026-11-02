# Reference data

Upstream: https://github.com/omikron88/iq-151 at
`5bd21af8a332c14483f184bb30f7074c1eea80d8` (GPL-3.0-or-later).

`generate.py /path/to/iq-151 > tests/fixtures/cpu-java.txt` compiles the original
CPU and interfaces with a JDK and executes 32 seeded states for each opcode.
Only the Swing/audio-dependent `execute` loop is omitted; `executeOne`, flags
and instruction decoder are unchanged. C++ tests use the same xorshift32 seed
and FNV-1a hashing of state and complete RAM after each instruction.

To regenerate boot/video fixtures, compile the original Java project's `src`
and `BootReference.java` into a separate directory, then run, with an X display:

```sh
java -cp /compiled/classes:/path/to/iq-151/src BootReference tests/fixtures > reference.log
```

Extract lines starting with `boot ` or `video ` into `fixtures/video-java.txt`.
On Windows the Java classpath separator is `;`. The harness disables only the
wall-clock timer, then calls the original `Iq.ms20()` 120 times for each clean
configuration. Snapshots come from the original `saveSnapshot`; pixel hashes
come from the original `getImage`, including Graphic and double-width cases.
No C++ renderer or CPU was used to produce these oracle values.

`make_tape.py` generates the small `hello.bas.csw` using the modulation algorithm
from upstream `Txt2Csw`. The C++ tests load and run it through the monitor tape
routines. JDK/Python are not runtime or normal build dependencies.
