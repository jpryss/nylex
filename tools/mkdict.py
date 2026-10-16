#!/usr/bin/env python3
"""Make a lexspell dictionary of a Hunspell dictionary (.aff and .dic).
usage: mkdict.py [-t] LANG.aff LANG.dic OUT.dict LANGCODE

Every form the affix rules give is in it, sorted by byte, each with a
flag byte '@' plus the bits
    1  a word of its own
    2  may begin a compound          (COMPOUNDBEGIN)
    4  may be in the middle of one   (COMPOUNDMIDDLE)
    8  may end one                   (COMPOUNDEND)
The text is LEX's 7-bit Swedish: a-ring a-umlaut o-umlaut and their
capitals as } { | ] [ \\, e-acute and E-acute as ` and @ (the Swedish
7-bit code, SEN 850200); words with other letters are left out.
The file (-t: text, "word c" lines, for looking at):

    "LEXDICT1", LANGCODE (2), 0 0, the number of words (4, big-endian),
    words a block (2), 0 0, the number of blocks (4), the offset of the
    index (4): 28 bytes
    blocks: for each word the length it shares with the word before
            (1; 0 at the start of a block), the rest, 0, the flag byte
    index:  for each block its offset (4) and its first word, 0

lexspell keeps the index in memory and reads a block for each word it
looks up."""
import re
import sys

SWE7 = {'å': '}', 'ä': '{', 'ö': '|', 'Å': ']', 'Ä': '[', 'Ö': '\\', 'é': '`', 'É': '@'}


def to7(w):
    out = []
    for ch in w:
        if ch in SWE7:
            out.append(SWE7[ch])
        elif ' ' < ch < '\x7f' and ch not in '[\\]{|}@`':
            out.append(ch)
        else:
            return None
    return ''.join(out)


class Aff:
    def __init__(self, path):
        self.sfx = {}
        self.pfx = {}
        self.opt = {}
        enc = 'latin1'
        raw = open(path, 'rb').read()
        m = re.search(rb'^SET\s+(\S+)', raw, re.M)
        if m:
            enc = m.group(1).decode().replace('ISO8859', 'iso8859').replace('ISO-8859', 'iso8859')
        self.enc = enc
        lines = raw.decode(enc, 'replace').split('\n')
        i = 0
        while i < len(lines):
            w = lines[i].split()
            i += 1
            if not w or w[0].startswith('#'):
                continue
            if w[0] in ('SFX', 'PFX') and len(w) >= 4 and w[2] in 'YN':
                flag, cross, n = w[1], w[2] == 'Y', int(w[3])
                rules = []
                for j in range(n):
                    r = lines[i + j].split()
                    strip = '' if r[2] == '0' else r[2]
                    add = r[3]
                    cont = ''
                    if '/' in add:
                        add, cont = add.split('/', 1)
                    if add == '0':
                        add = ''
                    cond = r[4] if len(r) > 4 else '.'
                    if w[0] == 'SFX':
                        rx = re.compile('(' + cond + ')$') if cond != '.' else None
                    else:
                        rx = re.compile('^(' + cond + ')') if cond != '.' else None
                    rules.append((strip, add, cont, rx))
                i += n
                (self.sfx if w[0] == 'SFX' else self.pfx)[flag] = (cross, rules)
            elif len(w) >= 2:
                self.opt.setdefault(w[0], w[1])

    def flag(self, name):
        return self.opt.get(name)


def suffixed(aff, word, flags, depth=2):
    """the forms of word with the suffixes of flags (and theirs): (form, flags)"""
    for f in flags:
        if f not in aff.sfx:
            continue
        cross, rules = aff.sfx[f]
        for strip, add, cont, rx in rules:
            if strip and not word.endswith(strip):
                continue
            if rx is not None and not rx.search(word):
                continue
            form = word[:len(word) - len(strip)] + add
            yield form, cont, cross
            if depth > 1 and cont:
                for g in suffixed(aff, form, cont, depth - 1):
                    yield g


def prefixed(aff, word, flags):
    for f in flags:
        if f not in aff.pfx:
            continue
        cross, rules = aff.pfx[f]
        for strip, add, cont, rx in rules:
            if strip and not word.startswith(strip):
                continue
            if rx is not None and not rx.search(word):
                continue
            yield add + word[len(strip):], cont, cross


def main():
    args = sys.argv[2:] if sys.argv[1] == '-t' else sys.argv[1:]
    aff = Aff(args[0])
    begin = aff.flag('COMPOUNDBEGIN') or aff.flag('COMPOUNDFLAG')
    middle = aff.flag('COMPOUNDMIDDLE') or aff.flag('COMPOUNDFLAG')
    end = aff.flag('COMPOUNDEND') or aff.flag('COMPOUNDFLAG')
    only = aff.flag('ONLYINCOMPOUND')
    need = aff.flag('NEEDAFFIX')
    forbid = aff.flag('FORBIDDENWORD')
    words = {}

    def put(w, bits):
        w = to7(w)
        if w and len(w) > 1 or (w and w.isalpha()):
            words[w] = words.get(w, 0) | bits

    def bits_of(fl, alone=True):
        b = 0
        if alone and not (only and only in fl) and not (need and need in fl):
            b |= 1
        if begin and begin in fl:
            b |= 2
        if middle and middle in fl:
            b |= 4
        if end and end in fl:
            b |= 8
        return b

    for line in open(args[1], encoding=aff.enc, errors='replace').read().split('\n')[1:]:
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        line = line.split('\t')[0].split(' ')[0]
        if '/' in line and not line.startswith('/'):
            word, fl = line.split('/', 1)
        else:
            word, fl = line, ''
        word = word.replace('\\/', '/')
        if forbid and forbid in fl:
            continue
        put(word, bits_of(fl))
        endstem = bool(end and end in fl)
        for form, cont, cross in suffixed(aff, word, fl):
            b = bits_of(cont)
            if endstem:
                b |= 8		# the end of a compound inflects
            if not cont or not (only and only in cont):
                b |= 1
            put(form, b)
            if cross:
                for pform, pc, pcross in prefixed(aff, form, fl):
                    if pcross:
                        put(pform, 1)
        for form, cont, cross in prefixed(aff, word, fl):
            put(form, 1 | bits_of(cont))
    text = sys.argv[1] == '-t'
    args = sys.argv[2:] if text else sys.argv[1:]
    keys = sorted((w for w in words if words[w]), key=lambda s: s.encode('ascii'))
    if text:
        with open(args[2], 'wb') as out:
            for w in keys:
                out.write(('%s %c\n' % (w, 0x40 | words[w])).encode('ascii'))
    else:
        per = 64
        blocks = []
        body = bytearray()
        base = 28
        for i in range(0, len(keys), per):
            blocks.append((base + len(body), keys[i]))
            prev = b''
            for w in keys[i:i + per]:
                b = w.encode('ascii')
                p = 0
                while p < len(prev) and p < len(b) and p < 255 and prev[p] == b[p]:
                    p += 1
                body += bytes([p]) + b[p:] + b'\0' + bytes([0x40 | words[w]])
                prev = b
        index = bytearray()
        for off, w in blocks:
            index += off.to_bytes(4, 'big') + w.encode('ascii') + b'\0'
        hdr = b'LEXDICT1' + args[3].encode('ascii')[:2].ljust(2) + b'\0\0' + len(keys).to_bytes(4, 'big') + \
            per.to_bytes(2, 'big') + b'\0\0' + len(blocks).to_bytes(4, 'big')
        hdr += (base + len(body)).to_bytes(4, 'big')	# 28 bytes, then the blocks
        with open(args[2], 'wb') as out:
            out.write(hdr + body + index)
    print('mkdict: %s, %d words' % (args[2], len(keys)))


if __name__ == '__main__':
    main()
