#!/usr/bin/env python3
"""A quick test of nylex in a pseudo-terminal (Python 3, no other modules).
usage: python3 tests/smoke.py ./nylex ./nylex.vmf [latin1|utf8|swe7]
Makes a document from the main menu, saves it, leaves nylex, and looks
at the file it wrote and at the document register."""
import os, pty, select, shutil, subprocess, sys, tempfile, time

prog, vmf = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
cs = sys.argv[3] if len(sys.argv) > 3 else 'latin1'
d = tempfile.mkdtemp(prefix='nylex-test-')
shutil.copy(vmf, os.path.join(d, 'test.vmf'))
env = dict(os.environ, HOME=d, TERM='vt100', LANG='C', LC_ALL='C', LINES='24', COLUMNS='80')
pid, fd = pty.fork()
if pid == 0:
    os.chdir(d)
    os.execve(prog, [prog, '-f', 'test.vmf', '-c', cs], env)
out = b''


def wait(s, t=10):
    global out
    end = time.time() + t
    while time.time() < end:
        if s.encode() in out:
            out = out[out.index(s.encode()) + len(s):]
            return
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                out += os.read(fd, 65536)
            except OSError:
                break
    sys.exit('FAIL: no "%s" on the screen' % s)


def seen(s):
    """s (on the screen, in the terminal's code) has been written"""
    global out
    if cs == 'swe7':
        s = s.translate(str.maketrans('ÄÖÅäöå', '[\\]{|}'))
    b = s.encode('utf-8' if cs == 'utf8' else 'latin1')
    end = time.time() + 5
    while b not in out and time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            out += os.read(fd, 65536)
    return b in out


def keys(s):
    # the Swedish letters as the terminal sends them
    if cs == 'swe7':
        s = s.translate(str.maketrans('ÄÖÅäöå', '[\\]{|}'))
    os.write(fd, s.encode('utf-8' if cs == 'utf8' else 'latin1'))
    time.sleep(0.3)


wait('Esc E avslutar'); keys('N')
wait('Dokumentnamn'); keys('PROV\r')
wait('Rubrik'); keys('Ett prov\r')
wait('Beskrivning'); keys('Ett prov av nylex\r')
time.sleep(1)
keys('Detta är ett prov av nylex. Raden blir så lång att ordet hamnar på nästa rad av sig själv.')
shown = seen('själv')
keys('\x1bE')
wait('Esc E avslutar'); keys('\x1bE')
_, st = os.waitpid(pid, 0)
txt = open(os.path.join(d, 'prov.brv'), 'rb').read().decode('latin1').split('\n')
ok = shown and st == 0 and txt[0].startswith('F...') and len(txt[0]) == 80 and txt[1].startswith('Detta {r ett prov av nylex.') and txt[2].startswith('n{sta rad') and len(txt) >= 3
lexvmf = os.path.join(os.path.dirname(prog), 'lexvmf')
if ok and os.path.exists(lexvmf):
    ok = 'PROV' in subprocess.run([lexvmf, '-l', os.path.join(d, 'test.vmf')], capture_output=True, text=True).stdout
shutil.rmtree(d)
print('nylex test (%s): ' % cs + ('passed' if ok else 'FAILED'))
for l in txt[:4]:
    print('  |' + l)
sys.exit(0 if ok else 1)
