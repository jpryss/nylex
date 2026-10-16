#!/usr/bin/env python3
"""Make src/pdffont.h: the widths of the PDF standard fonts nylex prints
with (Times, Helvetica; Courier is 600 throughout), for the codes 32-126
and 160-255 of WinAnsiEncoding (ISO 8859-1 there), in 1/1000 em.
usage: mkpdffonts.py GROFF_DEVPS_DIR > src/pdffont.h
The widths are those of Adobe's AFM files for the fonts, as groff's
devps font descriptions (made by afmtodit) carry them."""
import os
import sys

ASCII = ('space exclam quotedbl numbersign dollar percent ampersand quotesingle parenleft parenright '
         'asterisk plus comma hyphen period slash zero one two three four five six seven eight nine '
         'colon semicolon less equal greater question at A B C D E F G H I J K L M N O P Q R S T U V W X Y Z '
         'bracketleft backslash bracketright asciicircum underscore grave a b c d e f g h i j k l m n o p q r '
         's t u v w x y z braceleft bar braceright asciitilde').split()
LATIN1 = ('space exclamdown cent sterling currency yen brokenbar section dieresis copyright ordfeminine '
          'guillemotleft logicalnot hyphen registered macron degree plusminus twosuperior threesuperior '
          'acute mu paragraph periodcentered cedilla onesuperior ordmasculine guillemotright onequarter '
          'onehalf threequarters questiondown Agrave Aacute Acircumflex Atilde Adieresis Aring AE Ccedilla '
          'Egrave Eacute Ecircumflex Edieresis Igrave Iacute Icircumflex Idieresis Eth Ntilde Ograve Oacute '
          'Ocircumflex Otilde Odieresis multiply Oslash Ugrave Uacute Ucircumflex Udieresis Yacute Thorn '
          'germandbls agrave aacute acircumflex atilde adieresis aring ae ccedilla egrave eacute ecircumflex '
          'edieresis igrave iacute icircumflex idieresis eth ntilde ograve oacute ocircumflex otilde '
          'odieresis divide oslash ugrave uacute ucircumflex udieresis yacute thorn ydieresis').split()
assert len(ASCII) == 95 and len(LATIN1) == 96


def widths(path):
    w = {}
    inset = False
    last = None
    for line in open(path, encoding='latin1'):
        line = line.rstrip('\n')
        if line.startswith('charset'):
            inset = True
            continue
        if not inset or not line:
            continue
        f = line.split('\t')
        if len(f) >= 2 and f[1] == '"':
            continue
        if len(f) >= 5:
            w[f[4]] = int(f[1].split(',')[0])
    return w


d = sys.argv[1]
notices = []
for name in ('TR', 'HR'):
    for line in open(os.path.join(d, name), encoding='latin1'):
        if 'Notice Copyright' in line:
            n = line.split('Notice ', 1)[1].strip().replace('Reserved.', 'Reserved.  ', 1)
            if n not in notices:
                notices.append(n)
            break
print('/* pdffont.h - made by tools/mkpdffonts.py of the AFM widths (Adobe) of')
print('   the PDF standard fonts: codes 32-126 and 160-255, in 1/1000 em.')
print('')
print('   The widths come from Adobe\'s AFM files for the fonts:')
for n in notices:
    print('   ' + n)
print('   The AFM files may be used, copied and distributed for any purpose and')
print('   without charge, with or without modification, provided that all')
print('   copyright notices are retained.  This file holds only the widths of')
print('   those files, as numbers. */')
for name, ps in (('TR', 'Times-Roman'), ('TB', 'Times-Bold'), ('TI', 'Times-Italic'),
                 ('TBI', 'Times-BoldItalic'), ('HR', 'Helvetica'), ('HB', 'Helvetica-Bold')):
    w = widths(os.path.join(d, name))
    vals = [w[g] for g in ASCII] + [w[g] for g in LATIN1]
    print('static const short w_%s[191] = {\t/* %s */' % (name, ps))
    for i in range(0, len(vals), 16):
        print('\t' + ', '.join('%d' % v for v in vals[i:i + 16]) + ',')
    print('};')
