#!/usr/bin/env python3
"""Tests of lexconv: a LEX document out to each format and back again,
the zip files and their XML checked, Word and OpenDocument files read
compressed as well as stored, and a Markdown file read.
usage: convtest.py ./lexconv"""
import os, shutil, subprocess, sys, tempfile, zipfile
from xml.dom import minidom

prog = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(prefix='lexconv-')
R = 'F' + '.' * 68 + 'M' + '.' * 10
J = 'J' + '.' * 68 + 'M' + '.' * 10
T = '.....L..........R.........L..........####,###.##........####,###.##.............'
DOC = [R, '.T Rapport' + ' ' * 50 + 'Sida (SIDA:', '.F' + ' ' * 30 + 'nylex',
       ' ' * 22 + 'Rubrik som {r centrerad', '', J,
       'Det h{r {r ett stycke med rak h|germarginal.   Texten fylls  ut  till',
       'marginalen  M  av  nylex  n{r  stycket  ombryts  med  ESC J,  och  en',
       'proportionell skrivare f|rdelar d} mellanrummen p} nytt.', '', R,
       'Ett stycke med oj{mn h|germarginal: orden st}r med sina naturliga',
       'bredder, som p} en skrivare med proportionellt typhjul.', '', T,
       '     Artikel        Antal     Pris              Summa',
       '     Pennor            12    10,00                120,00',
       '     Papper (A4)        3    45,50                136,50', '', R,
       'Understruken och fet text samt kursiv.\xff', '____________     AAA           BBBBBB',
       '.P', 'Sista sidan.']
open(os.path.join(d, 'test.brv'), 'w', encoding='latin1').write('\n'.join(DOC) + '\n')
env = dict(os.environ, LEXNAT='1')
bad = 0


def conv(a, b):
    r = subprocess.run([prog, os.path.join(d, a), os.path.join(d, b)], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return r.returncode == 0


def check(what, good):
    global bad
    print('%-28s %s' % (what, 'ok' if good else 'FAILED'))
    bad += not good


for e in ('rtf', 'docx', 'odt', 'md', 'html', 'txt'):
    check('out: ' + e, conv('test.brv', 'test.' + e))
for e in ('docx', 'odt'):
    try:
        z = zipfile.ZipFile(os.path.join(d, 'test.' + e))
        ok = z.testzip() is None
        for n in z.namelist():
            if n.endswith('.xml') or n.endswith('.rels'):
                minidom.parseString(z.read(n))
        if e == 'odt':
            ok = ok and z.namelist()[0] == 'mimetype' and z.read('mimetype') == b'application/vnd.oasis.opendocument.text'
    except Exception as x:
        print('   ', x)
        ok = False
    check('zip and XML: ' + e, ok)
md = open(os.path.join(d, 'test.md'), encoding='utf-8').read()
check('markdown text', 'Rubrik som är centrerad' in md and '**fet**' in md and '*kursiv*' in md and
      '<u>Understruken</u>' in md and '```' in md)
rtf = open(os.path.join(d, 'test.rtf'), encoding='latin1').read()
check('rtf text', rtf.startswith('{\\rtf1') and '\\qj' in rtf and '\\qc' in rtf and "h\\'e4r" in rtf and '\\page' in rtf)
# back again: the same document from each
back = {}
for e in ('rtf', 'docx', 'odt'):
    ok = conv('test.' + e, 'back-%s.brv' % e)
    back[e] = open(os.path.join(d, 'back-%s.brv' % e), 'rb').read() if ok else b''
    check('in: ' + e, ok and b'Understruken' in back[e])
check('rtf, docx, odt agree', back['rtf'] == back['docx'] == back['odt'])
b = back['docx'].decode('latin1').split('\n')
check('header, footer', '.T Rapport' in b[1] and '(SIDA:' in b[1] and b[2].startswith('.F'))
check('justified, centred', any(l.startswith('J....') for l in b) and any(l.strip() == 'Rubrik som {r centrerad' and l.startswith('   ') for l in b))
check('columns', '     Pennor            12    10,00                120,00' in b)
check('attributes', 'Understruken och fet text samt kursiv.\xff' in b and '____________     AAA           BBBBBB' in b)
check('page break', '.P' in b)
# compressed members (as Word and LibreOffice write them)
for e in ('docx', 'odt'):
    src = zipfile.ZipFile(os.path.join(d, 'test.' + e))
    with zipfile.ZipFile(os.path.join(d, 'deflated.' + e), 'w') as z:
        for n in src.namelist():
            z.writestr(n, src.read(n), zipfile.ZIP_STORED if n == 'mimetype' else zipfile.ZIP_DEFLATED)
    ok = conv('deflated.' + e, 'deflated-%s.brv' % e)
    check('in, deflated: ' + e, ok and open(os.path.join(d, 'deflated-%s.brv' % e), 'rb').read() == back[e])
# Markdown in
open(os.path.join(d, 'in.md'), 'w', encoding='utf-8').write(
    '# Rubrik\n\nEtt *stycke* med **fet** text och [en länk](https://example.com).\n\n'
    '- en punkt\n- en till\n\n| A | B |\n|---|--:|\n| x | 12 |\n\n```\nkod  här\n```\n')
ok = conv('in.md', 'in.brv')
m = open(os.path.join(d, 'in.brv'), encoding='latin1').read().split('\n') if ok else []
check('in: md', ok and 'Rubrik\xff' in [l.strip() for l in m] and
      'Ett stycke med fet text och en l{nk (https://example.com).\xff' in m and
      '- en punkt' in m and 'kod  h{r' in m and any(l.startswith('x') and l.endswith('12') for l in m))
shutil.rmtree(d)
print('%d failed' % bad)
sys.exit(1 if bad else 0)
