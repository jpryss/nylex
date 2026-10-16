#!/bin/sh
# Build nylex without make (any Bourne shell):
#   sh build.sh                     with cc
#   CC=gcc CFLAGS=-O2 sh build.sh
#   CC=/usr/modern/bin/mcc CFLAGS="-O -DUSE_TERMIO" LEXLIB=/usr/modern/lib/nylex sh build.sh
#                                   (DIAB D-NIX 5.3 with the port's C compiler)
# LEXLIB is where nylex looks for the system file to copy the first
# time and lexspell its dictionaries (default /usr/local/share/nylex).
# The dictionaries are made with python3 when there is one.  Copy nylex,
# lexvmf, lexspell, lexconv, nylex.vmf, nylex-en.vmf, *.dict and doc/*.1 into
# place yourself, or use the Makefile.
CC=${CC:-cc}
CFLAGS=${CFLAGS:--O}
LEXLIB=${LEXLIB:-/usr/local/share/nylex}
set -x
$CC $CFLAGS "-DLEXLIB=\"$LEXLIB\"" -o nylex src/main.c src/menu.c src/edit.c src/esc.c src/esc2.c src/cut.c src/calc.c src/rec.c src/input.c src/vdu.c src/ui.c src/sys.c src/listp.c src/print.c src/pdf.c src/work.c src/doc.c src/reg.c src/term.c src/vmf.c || exit 1
$CC $CFLAGS -o lexvmf src/lexvmf.c src/vmf.c || exit 1
$CC $CFLAGS "-DLEXLIB=\"$LEXLIB\"" -o lexspell src/lexspell.c || exit 1
$CC $CFLAGS -o lexconv src/lexconv.c || exit 1
if command -v python3 >/dev/null 2>&1; then
	python3 tools/mkdict.py dict/sv_SE.aff dict/sv_SE.dic sv.dict sv || exit 1
	python3 tools/mkdict.py dict/en_GB.aff dict/en_GB.dic en-GB.dict en || exit 1
	python3 tools/mkdict.py dict/en_US.aff dict/en_US.dic en-US.dict en || exit 1
fi
