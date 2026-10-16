#!/usr/bin/env python3
"""Tests of lexspell: words looked up in the dictionaries (function S).
usage: spelltest.py ./lexspell DICTDIR"""
import os
import shutil
import subprocess
import sys
import tempfile

prog, lib = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])

# dictionary, code, word, right?, a suggestion expected (or None)
CASES = [
    ('sv', 'swe7', 'hus', True, None),
    ('sv', 'swe7', 'husvagnsd{ck', True, None),		# a compound
    ('sv', 'swe7', 'krokodilsko', True, None),
    ('sv', 'latin1', 'h\xe4lsning', True, None),
    ('sv', 'swe7', 'h{lsnig', False, 'h{lsning'),
    ('sv', 'swe7', 'bokstav', True, None),
    ('sv', 'swe7', 'boktsav', False, 'bokstav'),
    ('en', 'latin1', 'colour', True, None),
    ('en', 'latin1', 'recieve', False, 'receive'),
    ('en', 'latin1', 'necessary', True, None),
    ('en', 'latin1', 'neccessary', False, 'necessary'),
    ('en-US', 'latin1', 'color', True, None),
    ('en-US', 'latin1', 'theatre', False, None),
]

home = tempfile.mkdtemp(prefix='lexspell-')
env = dict(os.environ, HOME=home, LEXLIB=lib)
env.pop('LEXREC', None)
bad = 0
for d, cs, w, right, sug in CASES:
    out = subprocess.run([prog, '-d', d, '-k', cs, 'S'], input=(w + '\n').encode('latin1'),
                         env=env, stdout=subprocess.PIPE).stdout.decode('latin1')
    said = out.split('\n')[0]
    isright = (w + ': ') in said
    good = isright == right and (sug is None or sug in said.split())
    print('%-6s %-14s %s' % (d, w, 'ok' if good else 'FAILED: ' + said))
    bad += not good
shutil.rmtree(home)
print('%d failed' % bad)
sys.exit(1 if bad else 0)
