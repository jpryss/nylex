#!/usr/bin/env python3
"""Editing tests for nylex: each case makes a document from the main
menu with the keys given, saves it (ESC E) and compares the file with
what LEX's manual says it should hold.
usage: edtest.py ./nylex ./nylex.vmf [case ...]
With an English system file (nylex-en.vmf) the English prompts are
waited for and the English cases are run."""
import os, pty, select, shutil, sys, tempfile, time

prog, vmf = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
only = sys.argv[3:]
RULER = 'F' + '.' * 68 + 'M' + '.' * 10

# name, keys typed in the document, the lines expected after the ruler
CASES = [
    ('wrap', 'Detta {r en mening som {r s} l}ng att den inte f}r plats p} en rad utan bryts.',
     ['Detta {r en mening som {r s} l}ng att den inte f}r plats p} en rad', 'utan bryts.']),
    ('insert', 'Ett tv} tre\x08\x08\x08\x1bIfyra \x1b-I',
     ['Ett tv} fyra tre']),
    ('overtype', 'abcdef\x08\x08\x08XY', ['abcXYf']),
    ('delword', 'ett tv} tre\x02\x02\x17', ['ett tre']),
    ('delline', 'rad 1\rrad 2\rrad 3\x15\x18', ['rad 1', 'rad 3']),
    ('undelchar', 'abcd\x08\x08\x04\x1b-P', ['abcd']),
    ('undelline', 'rad 1\rrad 2\x18\x1b-X', ['rad 1', 'rad 2']),
    ('rubout', 'abcd\x7f\x7fX', ['abX']),
    ('ctrly', 'abc\r\x19\x19', ['abc', 'ab']),
    ('case', 'hej svejs\x02\x0b', ['hej SVEJS']),
    ('keystore', '\x1bS1Med v{nlig h{lsning\r\x1b1', ['Med v{nlig h{lsning']),
    ('date', '\x1bK', [time.strftime('%y%m%d')]),
    # between the tab stops around the cursor (C.5, C.6)
    ('center', 'mitt\x1b@', [' ' * 32 + 'mitt']),
    ('right', 'h|ger\x1b#', [' ' * 64 + 'h|ger']),
    ('justify', 'kort rad\rsom ska\rfogas ihop\x11\x0a\x1bJ', ['kort rad som ska fogas ihop']),
    ('cutpaste', 'abc def\x1bQK\x1b(\x06\x08\x1b)L\x07 \x1b*E', ['abc def abc']),
    ('attr', '\x1bA1under\x1bB1 vanlig', ['under vanlig\xff', '_____']),
    ('calc', 'Summa:\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x1b+12+30=\x1bE', ['Summa:      42,00']),
    # no room: the error is shown (**INST), keys are lost until Return
    ('calcroom', 'Summa: \x1b+12\r', ['Summa:']),
    ('numfield', '\x1bM4\t\t\t\t1234\t\x1bE', [None, ' ' * 40 + '1.234,00' + ' ' * 15 + '0,00']),
    ('search', 'alfa beta gamma\x11\x1bGgamma\rX', ['alfa beta Xamma']),
    ('ruler', '\x1bM2text', [RULER.replace('F', 'J', 1), 'text']),
    ('vertical', '\x1b\\abc\x1b-\\', ['a', 'b', 'c']),
]

def longdate():
    d = time.localtime().tm_mday
    suf = 'st' if d in (1, 21, 31) else 'nd' if d in (2, 22) else 'rd' if d in (3, 23) else 'th'
    return '%d%s %s' % (d, suf, time.strftime('%B %Y'))


# the English system file: [ \\ ] { | } are themselves, Latin-1 is kept
EN_CASES = [
    ('wrap', 'This is a sentence that is so long that it does not fit on one line but is broken.',
     ['This is a sentence that is so long that it does not fit on one line', 'but is broken.']),
    ('brackets', 'a [b] {c|d} \\e', ['a [b] {c|d} \\e']),
    ('latin1', 'caf\xe9 na\xefve', ['caf\xe9 na\xefve']),
    ('keystore', '\x1bS1Yours sincerely\r\x1b1', ['Yours sincerely']),
    ('calc', 'Total:\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x1b+12+30=\x1bE', ['Total:      42.00']),
    ('numfield', '\x1bM4\t\t\t\t1234\t\x1bE', [None, ' ' * 40 + '1,234.00' + ' ' * 15 + '0.00']),
    ('date', '\x1bY', [longdate()]),
]
EN = '-en' in os.path.basename(vmf)
P = dict(menu='Esc E to leave', name='Document name', title='Title', descr='Description', ext='ltr') if EN else \
    dict(menu='Esc E avslutar', name='Dokumentnamn', title='Rubrik', descr='Beskrivning', ext='brv')


def run(name, typed, want):
    d = tempfile.mkdtemp(prefix='nylex-ed-')
    shutil.copy(vmf, os.path.join(d, 'test.vmf'))
    env = dict(os.environ, HOME=d, TERM='vt100', LANG='C', LC_ALL='C', LINES='24', COLUMNS='80')
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(d)
        os.execve(prog, [prog, '-f', 'test.vmf', '-c', 'latin1' if EN else 'swe7'], env)
    buf = [b'']

    def wait(s, t=8):
        end = time.time() + t
        while time.time() < end:
            if s.encode() in buf[0]:
                buf[0] = buf[0][buf[0].index(s.encode()) + len(s):]
                return True
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    buf[0] += os.read(fd, 65536)
                except OSError:
                    break
        return False

    def keys(s):
        os.write(fd, s.encode('latin1'))
        time.sleep(0.15)

    ok = wait(P['menu'])
    keys('N')
    ok = ok and wait(P['name'])
    keys('T\r')
    wait(P['title'])
    keys('\r')
    wait(P['descr'])
    keys('\r')
    time.sleep(0.6)
    for c in typed:
        os.write(fd, c.encode('latin1'))
        time.sleep(0.02)
    time.sleep(0.4)
    keys('\x1bE')
    ok = wait(P['menu']) and ok
    keys('\x1bE')
    time.sleep(0.3)
    for i in range(30):
        if os.waitpid(pid, os.WNOHANG)[0]:
            break
        time.sleep(0.1)
    else:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        ok = False
    try:
        got = open(os.path.join(d, 't.' + P['ext']), 'rb').read().decode('latin1').split('\n')
    except OSError:
        got = ['(no file)']
    shutil.rmtree(d)
    while got and got[-1] == '':
        got.pop()
    body = got[1:] if got and got[0].startswith(('F.', 'J.')) and len(got[0]) >= 70 else got
    if name == 'ruler':
        body = got[1:]
    good = ok and len(body) == len(want) and all(w is None or b.rstrip() == w.rstrip() for b, w in zip(body, want))
    print('%-10s %s' % (name, 'ok' if good else 'FAILED'))
    if not good:
        for l in got:
            print('    got  |%s|' % l)
        for l in want:
            print('    want |%s|' % (l if l is not None else '(any)'))
    return good


bad = 0
for c in (EN_CASES if EN else CASES):
    if only and c[0] not in only:
        continue
    if not run(*c):
        bad += 1
print('%d failed' % bad)
sys.exit(1 if bad else 0)
