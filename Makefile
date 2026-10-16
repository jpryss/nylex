# nylex - a word processor in the style of LEX
#
# Plain POSIX make: GNU make, BSD make (FreeBSD, NetBSD, OpenBSD,
# DragonFly, macOS), bmake, Solaris/illumos make, and others.
#
#   make                    nylex, lexvmf, lexspell and its dictionaries, lexconv
#                           (made of dict/ by tools/mkdict.py: python3)
#   make programs           the programs only
#   make check              quick tests in a pseudo-terminal (python3)
#   make install            to $(DESTDIR)$(PREFIX); see INSTALL
#   make uninstall, clean
#   make vmf                nylex.vmf and nylex-en.vmf remade from
#                           lexdef.txt and lexdef-en.txt (python3)
#
# Settings, given on the command line (make PREFIX=/usr CC=clang ...):
#   PREFIX   /usr/local          (Homebrew on Apple silicon: /opt/homebrew)
#   MANDIR   $(PREFIX)/share/man (OpenBSD, NetBSD: $(PREFIX)/man)
#   DOCDIR   $(PREFIX)/share/doc/nylex (the tutorial)
#   LEXLIB   $(PREFIX)/share/nylex (the system files and the dictionaries)
#   CC, CFLAGS, LDFLAGS, DESTDIR as usual.
#   TERMIO   -DUSE_TERMIO on old System V systems without termios.
# INSTALL has the settings for particular systems.

PREFIX	= /usr/local
BINDIR	= $(PREFIX)/bin
MANDIR	= $(PREFIX)/share/man
DOCDIR	= $(PREFIX)/share/doc/nylex
LEXLIB	= $(PREFIX)/share/nylex
CC	= cc
CFLAGS	= -O2
LDFLAGS	=
LIBS	=
TERMIO	=
PYTHON	= python3
DESTDIR	=

ALLCFLAGS = $(CFLAGS) $(TERMIO)
OBJS	= main.o menu.o edit.o esc.o esc2.o cut.o calc.o rec.o input.o \
	  vdu.o ui.o sys.o listp.o print.o pdf.o work.o doc.o reg.o term.o vmf.o
HDRS	= src/lex.h src/vmf.h src/term.h

DICTS	= sv.dict en-GB.dict en-US.dict

all: programs dicts

programs: nylex lexvmf lexspell lexconv

dicts: $(DICTS)

sv.dict: dict/sv_SE.aff dict/sv_SE.dic tools/mkdict.py
	$(PYTHON) tools/mkdict.py dict/sv_SE.aff dict/sv_SE.dic sv.dict sv
en-GB.dict: dict/en_GB.aff dict/en_GB.dic tools/mkdict.py
	$(PYTHON) tools/mkdict.py dict/en_GB.aff dict/en_GB.dic en-GB.dict en
en-US.dict: dict/en_US.aff dict/en_US.dic tools/mkdict.py
	$(PYTHON) tools/mkdict.py dict/en_US.aff dict/en_US.dic en-US.dict en

nylex: $(OBJS)
	$(CC) $(ALLCFLAGS) $(LDFLAGS) -o nylex $(OBJS) $(LIBS)

lexvmf: lexvmf.o vmf.o
	$(CC) $(ALLCFLAGS) $(LDFLAGS) -o lexvmf lexvmf.o vmf.o $(LIBS)

main.o: src/main.c $(HDRS)
	$(CC) $(ALLCFLAGS) -DLEXLIB=\"$(LEXLIB)\" -c src/main.c
menu.o: src/menu.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/menu.c
edit.o: src/edit.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/edit.c
esc.o: src/esc.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/esc.c
esc2.o: src/esc2.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/esc2.c
cut.o: src/cut.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/cut.c
calc.o: src/calc.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/calc.c
rec.o: src/rec.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/rec.c
input.o: src/input.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/input.c
vdu.o: src/vdu.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/vdu.c
ui.o: src/ui.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/ui.c
sys.o: src/sys.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/sys.c
listp.o: src/listp.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/listp.c
print.o: src/print.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/print.c
pdf.o: src/pdf.c src/pdffont.h $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/pdf.c
work.o: src/work.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/work.c
doc.o: src/doc.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/doc.c
reg.o: src/reg.c $(HDRS)
	$(CC) $(ALLCFLAGS) -c src/reg.c
term.o: src/term.c src/term.h
	$(CC) $(ALLCFLAGS) -c src/term.c
vmf.o: src/vmf.c src/vmf.h
	$(CC) $(ALLCFLAGS) -c src/vmf.c
lexvmf.o: src/lexvmf.c src/vmf.h
	$(CC) $(ALLCFLAGS) -c src/lexvmf.c

lexspell: src/lexspell.c
	$(CC) $(ALLCFLAGS) $(LDFLAGS) -DLEXLIB=\"$(LEXLIB)\" -o lexspell src/lexspell.c $(LIBS)

lexconv: src/lexconv.c
	$(CC) $(ALLCFLAGS) $(LDFLAGS) -o lexconv src/lexconv.c $(LIBS)

check: all
	$(PYTHON) tests/smoke.py ./nylex ./nylex.vmf latin1
	$(PYTHON) tests/smoke.py ./nylex ./nylex.vmf utf8
	$(PYTHON) tests/smoke.py ./nylex ./nylex.vmf swe7
	$(PYTHON) tests/edtest.py ./nylex ./nylex.vmf
	$(PYTHON) tests/edtest.py ./nylex ./nylex-en.vmf
	$(PYTHON) tests/spelltest.py ./lexspell .
	$(PYTHON) tests/pdftest.py ./nylex ./nylex.vmf
	$(PYTHON) tests/convtest.py ./lexconv

vmf:
	$(PYTHON) tools/mklexvmf.py lexdef.txt nylex.vmf
	$(PYTHON) tools/mklexvmf.py lexdef-en.txt nylex-en.vmf

install: all
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(LEXLIB) $(DESTDIR)$(MANDIR)/man1 $(DESTDIR)$(DOCDIR)
	cp nylex lexvmf lexspell lexconv $(DESTDIR)$(BINDIR)/
	cd $(DESTDIR)$(BINDIR) && chmod 755 nylex lexvmf lexspell lexconv
	cp nylex.vmf nylex-en.vmf $(DICTS) $(DESTDIR)$(LEXLIB)/
	cd $(DESTDIR)$(LEXLIB) && chmod 644 nylex.vmf nylex-en.vmf $(DICTS)
	cp doc/nylex.1 doc/lexvmf.1 doc/lexspell.1 doc/lexconv.1 $(DESTDIR)$(MANDIR)/man1/
	cd $(DESTDIR)$(MANDIR)/man1 && chmod 644 nylex.1 lexvmf.1 lexspell.1 lexconv.1
	cp doc/TUTORIAL.md $(DESTDIR)$(DOCDIR)/
	chmod 644 $(DESTDIR)$(DOCDIR)/TUTORIAL.md

uninstall:
	cd $(DESTDIR)$(BINDIR) && rm -f nylex lexvmf lexspell lexconv
	cd $(DESTDIR)$(MANDIR)/man1 && rm -f nylex.1 lexvmf.1 lexspell.1 lexconv.1
	cd $(DESTDIR)$(LEXLIB) && rm -f nylex.vmf nylex-en.vmf $(DICTS)
	rm -f $(DESTDIR)$(DOCDIR)/TUTORIAL.md
	-rmdir $(DESTDIR)$(LEXLIB) $(DESTDIR)$(DOCDIR)

clean:
	rm -f nylex lexvmf lexspell lexconv $(DICTS) *.o core
