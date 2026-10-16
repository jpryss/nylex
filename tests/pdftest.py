#!/usr/bin/env python3
"""Printing to PDF: a document of two pages printed on printer 7 of the
system file (PDF), from the menus, and the file looked at; then on
printer 8, a daisy wheel with proportional spacing (;PS10).
usage: pdftest.py ./nylex ./nylex.vmf"""
import os, pty, re, select, shutil, sys, tempfile, time

prog, vmf = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
EN = '-en' in os.path.basename(vmf)
ext, printkey = ('ltr', 'P') if EN else ('brv', 'U')
d = tempfile.mkdtemp(prefix='nylex-pdf-')
shutil.copy(vmf, os.path.join(d, 'test.vmf'))
R = 'J' + '.' * 68 + 'M' + '.' * 10
lines = [R, '.T Title' + ' ' * 50 + ('Page (PAGE:' if EN else 'Sida (SIDA:')]
for i in range(70):
    lines.append('word ' * 13 + 'end%02d' % i)
open(os.path.join(d, 't.' + ext), 'w').write('\n'.join(lines) + '\n')
env = dict(os.environ, HOME=d, TERM='vt100', LANG='C', LC_ALL='C', LINES='24', COLUMNS='80')
env.pop('LEXPRINT', None)


def start(e):
    global pid, fd
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(d)
        os.execve(prog, [prog, '-f', 'test.vmf', '-c', 'latin1'], e)


start(env)


def keys(s, t=0.5):
    os.write(fd, s.encode('latin1'))
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                os.read(fd, 65536)
            except OSError:
                break


keys('', 1.5)
for k in (printkey, 'T\r', '\r', '\r', '7', 'out.pdf\r', '\r', '\r', '\r'):
    keys(k)
keys('\x1bE', 1)
os.kill(pid, 9)
os.waitpid(pid, 0)
ok = True
try:
    pdf = open(os.path.join(d, 'out.pdf'), 'rb').read()
except OSError:
    pdf = b''
for what, good in (('a PDF', pdf.startswith(b'%PDF-1.4')),
                   ('two pages', b'/Count 2' in pdf),
                   ('the page number', b'(Page 2)' in pdf or b'(Sida 2)' in pdf),
                   ('the last word', b'end69' in pdf),
                   ('the end', pdf.rstrip().endswith(b'%%EOF'))):
    print('%-16s %s' % (what, 'ok' if good else 'FAILED'))
    ok = ok and good
m = re.search(rb'startxref\n(\d+)', pdf)
good = bool(m) and pdf[int(m.group(1)):].startswith(b'xref')
print('%-16s %s' % ('the xref', 'ok' if good else 'FAILED'))
ok = ok and good
# the daisy wheel: Diablo 630's motion index (ESC US n), the text whole
start(dict(env, LEXPRINT='cat > ' + os.path.join(d, 'out.bin')))
keys('', 1.5)
for k in (printkey, 'T\r', '\r', '\r', '8', '\r', '\r', '\r'):
    keys(k)
keys('\x1bE', 1)
os.kill(pid, 9)
os.waitpid(pid, 0)
try:
    dw = open(os.path.join(d, 'out.bin'), 'rb').read()
except OSError:
    dw = b''
text = []
i = 0
while i < len(dw):
    if dw[i] == 27:
        i += 3 if i + 1 < len(dw) and dw[i + 1] == 31 else 2
        continue
    if dw[i] > 32:
        text.append(dw[i])
    i += 1
for what, good in (('daisy: HMI', b'\x1b\x1f' in dw), ('daisy: the text', b'end69' in bytes(text))):
    print('%-16s %s' % (what, 'ok' if good else 'FAILED'))
    ok = ok and good
shutil.rmtree(d)
print('0 failed' if ok else 'FAILED')
sys.exit(0 if ok else 1)
