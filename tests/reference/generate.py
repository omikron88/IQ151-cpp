#!/usr/bin/env python3
"""Regenerate CPU oracle from omikron88/iq-151 at 5bd21af8a332c14483f184bb30f7074c1eea80d8.
Usage: python3 tests/reference/generate.py /path/to/iq-151 > tests/fixtures/cpu-java.txt
Only the Swing/audio-coupled execute loop is removed. executeOne and all opcode logic
are compiled unchanged, using the original memory/clock/notification interfaces.
Requires a JDK (only for this optional comparison, never to build or run the port).
"""
import pathlib, shutil, subprocess, sys, tempfile
source=pathlib.Path(sys.argv[1])/'src/machine'
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp);(root/'machine').mkdir()
    for name in ['Clock','ClockTimeoutListener','MemIoOps','NotifyOps']:
        shutil.copyfile(source/(name+'.java'),root/'machine'/(name+'.java'))
    code=(source/'I8085.java').read_text()
    a=code.index('    public final void execute(long statesLimit)')
    b=code.index('    private void decodeOpcode',a)
    (root/'machine/I8085.java').write_text(code[:a]+code[b:])
    shutil.copyfile(pathlib.Path(__file__).with_name('Reference.java'),root/'Reference.java')
    files=[str(p) for p in root.rglob('*.java')]
    subprocess.run(['java','-m','jdk.compiler/com.sun.tools.javac.Main','-d',tmp]+files,check=True)
    subprocess.run(['java','-cp',tmp,'Reference'],check=True)
