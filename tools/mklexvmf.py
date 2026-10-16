#!/usr/bin/env python3
"""Make nylex's system file (a LEX .VMF file) from lexdef.txt.
usage: mklexvmf.py lexdef.txt nylex.vmf

lexdef.txt holds the records, each started by a line

    == KEY                a record as it is written (lines end in NUL)
    == KEY fixed          lines "OFFSET:text": the text put at that offset
                          of the record, the rest the paint character "_"
    == KEY form RECKEY    a form for the records RECKEY... (see below)
    == KEY page           a page of text shown (in background mode) until
                          Return, then back to the menu

A line "#" or starting "# " or "#!" is a comment, "\\" at the end of a
line joins the next.
Text is LEX's 7-bit Swedish (lexdef.txt: [ \\ ] { | } are Ä Ö Å ä ö å) or
ASCII (lexdef-en.txt, whose **MONT makes them brackets and bars again).


A form is a program, as LEX's own (*INSTFORM ...) are: it types the
picture in background mode, sets protect mode and reads the record whose
key is in the top left corner.  Its lines are the picture; <<N:L>> is a
field of L cells for the characters N... of the record.  The fields must
follow each other: the data of a record read on a form is its
unprotected cells in order.  The key cells (after the start character)
come first; RECKEY is typed in them and the rest are left to fill in.

The file is written as LEX writes it (manual chapter 2): block 0 the
header, then the top level index (51 keys a block), the second level
(36 entries of 14 bytes a block) and the data blocks of chained
records of at most 124 bytes; runs of "_" squashed (1-31), NUL for the
ends of lines.
"""
import sys

BLK = 512
ENTLEN = 14
ENTMAX = 36
TLIKEYS = 51
CHUNKMAX = 124
HDR0 = bytes([0x9f, 0x0a, 0x04, 0x5e, 0x04, 0x59, 0x00, 0xf7, 0x00, 0x0d, 0x04, 0x6b,
              0x00, 0x1e, 0x03, 0x30, 0x02, 0x5e]) + bytes(14)
# characters a program would take for something else: typed after "@"
SPECIAL = set('<>():;$\'"!@&?`^%\\/~')


def die(msg):
    sys.stderr.write('mklexvmf: %s\n' % msg)
    sys.exit(1)


def esc(s):
    return ''.join('@' + c if c in SPECIAL else c for c in s)


def form(key, reckey, lines):
    """The program of a form."""
    out = []
    want = 0
    full = False
    for i, line in enumerate(lines):
        o = ''
        p = 0
        width = 11 if i == 0 else 0
        if i == 0:
            if len(reckey) > 10:
                die('%s: key %s too long' % (key, reckey))
            o += '(' + esc(reckey) + '_' * (10 - len(reckey))
        while p < len(line):
            if line.startswith('<<', p):
                e = line.index('>>', p)
                n, l = line[p + 2:e].split(':')
                n, l = int(n), int(l)
                if n != want:
                    die('%s: field at %d, %d expected' % (key, n, want))
                want = n + l
                o += '_' * l
                width += l
                p = e + 2
            else:
                o += esc(line[p])
                width += 1
                p += 1
        if width > 80:
            die('%s: line %d is %d wide' % (key, i + 1, width))
        # a full line has put the cursor on the next one already
        if full:
            out[-1] += o
        else:
            out.append(o)
        full = width == 80
    out[0] = '$SB' + out[0]
    out[-1] += "$UB$SP'T$:R"
    return out


def parse(path):
    recs = {}
    key = None
    kind = None
    arg = None
    lines = []

    def flush():
        if key is None:
            return
        ls = list(lines)
        while ls and ls[-1] == '':
            ls.pop()
        if kind == 'fixed':
            buf = []
            for l in ls:
                off, _, t = l.partition(':')
                off = int(off)
                while len(buf) < off + len(t):
                    buf.append('_')
                buf[off:off + len(t)] = list(t)
            text = ''.join(buf)
        elif kind == 'form':
            text = '\n'.join(form(key, arg, ls))
        elif kind == 'page':
            text = '\n'.join(['$SB' + esc(ls[0])] + [esc(l) for l in ls[1:]]) + '!$E'
        else:
            text = '\n'.join(ls)
        if len(key) > 10:
            die('key %s too long' % key)
        recs[key] = squash(text)

    raw = open(path, encoding='latin1').read().split('\n')
    joined = []
    for l in raw:
        if joined and joined[-1].endswith('\\') and not joined[-1].endswith('\\\\'):
            joined[-1] = joined[-1][:-1] + l
        else:
            joined.append(l)
    for l in joined:
        if l.startswith('== '):
            flush()
            w = l[3:].split()
            key = w[0]
            kind = w[1] if len(w) > 1 else None
            arg = w[2] if len(w) > 2 else None
            lines = []
        elif key is None:
            continue
        elif l == '#' or l.startswith('# ') or l.startswith('#!'):
            continue
        else:
            lines.append(l)
    flush()
    return recs


def squash(text):
    out = bytearray()
    b = text.encode('latin1')
    i = 0
    while i < len(b):
        c = b[i]
        if c == 0x0a:
            out.append(0)
            i += 1
        elif c == 0x5f:
            n = 0
            while i < len(b) and b[i] == 0x5f and n < 31:
                n += 1
                i += 1
            out.append(n)
        else:
            if c < 32:
                die('control character %d in a record' % c)
            out.append(c)
            i += 1
    return bytes(out)


def write(recs, out, minblocks=160, minkeys=360):
    keys = sorted(recs, key=lambda k: k.ljust(10).encode('latin1'))
    n = len(keys)
    n2li = max((minkeys + ENTMAX - 1) // ENTMAX, 1)
    while n2li * (ENTMAX - 1) < n:
        n2li += 1
    per = max((n + n2li - 1) // n2li, 1)
    ntli = max((n2li - 1 + TLIKEYS - 1) // TLIKEYS, 1)
    first = 1 + ntli + n2li
    blocks = {}

    def block(b):
        if b not in blocks:
            x = bytearray(BLK)
            x[0] = 1
            blocks[b] = x
        return blocks[b]

    def setptr(x, o, blk, pos):
        x[o] = ((blk + 1) >> 8) & 0x7f
        x[o + 1] = (blk + 1) & 0xff
        x[o + 2] = pos

    cur = first
    off = 3
    where = {}
    for k in keys:
        data = recs[k]
        done = 0
        fb = fp = pb = po = None
        while True:
            if off + 5 > BLK:
                cur += 1
                off = 3
            x = block(cur)
            room = BLK - off - 4
            l = min(CHUNKMAX, len(data) - done, room)
            if l > 1 and done + l < len(data) and data[done + l - 1] == 0x27:
                l -= 1
            if l <= 0 and len(data) > 0:
                off = BLK
                continue
            x[off + 4:off + 4 + l] = data[done:done + l]
            x[off + 3] = l
            if pb is not None:
                setptr(block(pb), po, cur, (off - 1) // 2)
            else:
                fb, fp = cur, (off - 1) // 2
            pb, po = cur, off
            done += l
            off += 4 + l
            if not off & 1:
                off += 1
            x[0] = min((off - 1) // 2, 255)
            if done >= len(data):
                break
        setptr(block(pb), po, fb, fp)
        where[k] = (pb, (po - 1) // 2)
    total = max(cur + 1, minblocks)
    buf = bytearray()
    for b in range(total):
        buf += block(b) if b >= first else bytearray(BLK)
    buf[0:32] = HDR0
    buf[12] = first >> 8
    buf[13] = first & 0xff
    for i in range(n2li):
        b = 1 + ntli + i
        x = bytearray(b'\xff' * BLK)
        x[1] = 0
        nxt = b + 1 if i + 1 < n2li else 0
        prv = b - 1 if i else 0
        x[2:4] = nxt.to_bytes(2, 'big')
        x[4:6] = prv.to_bytes(2, 'big')
        buf[b * BLK:(b + 1) * BLK] = x
    for j, k in enumerate(keys):
        bi, e = j // per, j % per
        if bi >= n2li:
            bi = n2li - 1
            e = j - bi * per
        o = (1 + ntli + bi) * BLK + 6 + ENTLEN * e
        buf[o:o + 10] = k.ljust(10).encode('latin1')
        pb, pos = where[k]
        buf[o + 10] = ((pb + 1) >> 8) & 0x7f
        buf[o + 11] = (pb + 1) & 0xff
        buf[o + 12] = pos
        buf[o + 13] = 0
    for i in range(ntli):
        o = (1 + i) * BLK
        buf[o:o + BLK] = b'\xff' * BLK
        buf[o + BLK - 1] = 1 if i + 1 < ntli else 0
    for i in range(n2li - 1):
        last = (i + 1) * per - 1
        if last >= n:
            break
        o = (1 + i // TLIKEYS) * BLK + 1 + 10 * (i % TLIKEYS)
        buf[o:o + 10] = keys[last].ljust(10).encode('latin1')
    open(out, 'wb').write(bytes(buf))
    return n


if __name__ == '__main__':
    if len(sys.argv) != 3:
        die('usage: mklexvmf.py lexdef.txt nylex.vmf')
    n = write(parse(sys.argv[1]), sys.argv[2])
    print('mklexvmf: %s, %d records' % (sys.argv[2], n))
