# LEX-68 notes (reverse engineering for a compatible clone)

Sources: the two diskettes "Lex68(0) - DIAD AB" and "Lex68(1) - DIAD AB"
(LEX-68 version 9A1, 1986), and LEX's technical manual (version 9,
November 1985; its text, read from the scans, is in `manual/` of the
port's source, not in nylex's package).  LEX is Ace Microsystems Ltd's,
written in their Filetab-D language; DIAD AB sold it in Sweden as LEX-68
for DIAB's machines.
Nothing of LEX's code or texts goes into the clone; these notes describe
file formats and behaviour so that the clone can work with LEX's files.

## Programs

* `/usr/lib/slex9a1`: the program, x.out (Xenix-style, magic 0x0206)
  68000 binary, text 0x54d0e at 0x8000, data at 0x60000, with a symbol
  table (422 symbols).  Started by the `lex68` script as
  `slex9a1 /F$HOME/lex9a1.vmf /U$TERM`.  Options seen in the program:
  /T /F /U /P /A /D /H /C /S, /W.  Its symbols (_mov, _cmp, _add, _set,
  _getpag, _findcom, _loadcom, _runcom, _trace, call, callc ...) suggest
  a run-time system for an interpreted program (Filetab-D).
* `lxserl`, `lxinst`, `lxconv` (the same size, 121848): serialisation,
  installation, conversion.
* `fixa.lex`: `stty ixon ixany` 8 s after start.
* `*.lex` scripts called from LEX: `sh.lex` (a shell), `qc.lex`
  (Q-Calc), `posta.lex` (mail), `sendt.lex` (teletex).
* Not serialised, slex9a1 asks "Yes to serialise, No for demo" and the
  demo refuses files that are too big.

## The VMF file (`lex9a1.vmf`, the "system file")

As the manual's chapter 2 has it, and as LEX-68's file is:

* 512-byte blocks.  Block 0 the header: `9f0a` (structure mark) ...;
  bytes 12-13 the first data block.  LEX-68's file: 1 top-level block,
  28 second-level blocks, data from block 30, 521 blocks.
* Top-level index from block 1: byte 0 = 255, then 51 keys of 10 bytes
  (the highest key of each second-level block but the last); the last
  byte of a block is 0 in the last top-level block.
* Second-level blocks: `ff`, a lock byte, next and previous block (16
  bits each), then 36 entries of 14 bytes: key (10, blank padded),
  pointer (block + 1 in 16 bits, the top bit a lock, then a position),
  flags (0x80: deleted; the record stays until a reorganisation).
* Data blocks: byte 0 the next free position (position p is at offset
  2p + 1); a record is a chain of pieces: a forward pointer (3 bytes),
  a length, at most 124 bytes.  The chain is a ring and the index points
  at its last piece.  A piece never ends between `'` and the character
  it makes a control key of.
* Records are squashed: a byte 1-31 is a run of that many squash
  characters (`_`), 0 ends a line.  The record's text starts right after
  its key: on a form the key is the 10 cells after the start character
  and the data the cells after them (the first data cell is often a
  spare one, so most layouts count from 1).

Key prefixes: `**` system records (**MENU, **MONT, **INST, **PNST,
**TEXT, **TEXT2, **TEXTP, **START), `*` menus, programs, forms, help,
dictionaries and output formats, `*.EXT` document types, `*$` printer
sequences, `*%` their combinations of attributes, `*#` printer code
tables, `*(` their functions, `*,` masks, `*-ERROR-xx` error texts,
`)NAME` `))NAME` `)=NAME` `);NAME` terminal records, `:NAME__EXT` the
document index (220 characters: name 1, title 41, description 81,
created 121, changed 128 and times 135, read 137/144, printed 146/153,
last session 155, total time 161, lines 167, words 173, characters 181,
the four dates as yymmdd 189-212).  Data records of list processing
have a record type character of their own (`.` in LEX-68's address
register).

## The system records

* **MONT (fields separated by `\`): field 1 the visible equivalents
  as 3-digit codes; the Swedish one: start key `<`, Return `;`, end key
  `>`, visible start `(`, ESC `$`, visible end `:`, Ctrl `'`, literal
  string `"`, question/pause `!`, next literal `@`, the list processing
  operators `< = > :`, select brackets `( )`, continuation `&`, menu exit
  Ctrl U.  Then months, prefixes, suffixes, the date style, J/N, the
  letters of QFTUJ (query, from, to, odd, even), hyphenation, more
  codes (Australian mode, up, down, paint, ...), the kill character,
  the time separator, the short date, the vowels, the case ranges, the
  status characters and the help word.
* **INST (offsets in the record): VDU 1-9, device 10-41, work file size
  42-44, printer key 45-54, the extensions 77/81/85/89, the print
  control characters 93-104, flags 111-129 (multi-user, keep temporary
  files, versions, free form names, name case L/M/U, titling, counts,
  detail, summary, help on copy and select, running display, are you
  sure, show keys, show page, free form compare, -, show criteria,
  show count), delay 130, sleep 133, block warning 136, automatic
  error show 142, status line 143, page warning 144, decimal point 146,
  thousands 147, negative 148-149, decimals 150.
* **PNST: the printer question 1-156 (two lines of 78), then for
  printers 1-9, 0 at 158 + 64i: type (the *$ record) 8, code table (*#)
  8, flags spool/pause/VDU port/form feed after the last page, device
  or command 40 (LEX-68: `/bin/lpr -h0`, `/dev/tty`); spool directory
  798, first page number 814, index file type 815.
* *.EXT: rulers 1-5 at 0 (80 each), keystores 1-3 at 400 (78), 4-9 at
  634 (38), footnote style 862, left margin 863, alternate margin 865,
  widow 867, orphan 868, lines a page 869, page length or FF 871.
* *$ (the form *PRTFORM): 20 local visible equivalents (character and
  3 digits) at 1, printer set 81 and reset 145 (64), form feeds 209 and
  241, half and quarter line 273 and 305 (32), footnote on/off 337/353,
  no line feed 369, escape 401, pound 433, underline 449, special 1 and
  2 465/481 (16), all off 497 (32), attributes 1-5 on/off from 529 (16
  each).  *% the same equivalents, all off 81, attribute 1 113/129 and
  the combinations of 2-5 (A-O) from 145.  *#: 256 entries of 3 cells
  from 1 (a character, a decimal value, or a letter in the last cell:
  a function of the *( record named at 769).
* **TEXT, **TEXT2, **TEXTP: messages separated by Return (and `;`),
  the first a heading.
* Proportional spacing (no form in the manual; LEX-68's *PPSFORM and
  *PPSDETS show it): a record `;NAME` for each daisy wheel, at 1 two
  digits for each character 32-126, the increments (1/120 inch) to
  move before and after printing it (`;THEME11`: space 4+4, `i` 3+2,
  `m` 7+6 ...), at 191 the wheel's petal table (*#, 9 cells).
  *PSPARS holds the wheel in use (1, 9 cells), a horizontal and a
  vertical pitch (2 digits each: Diablo's HMI and VMI values?) and the
  lines a page.  nylex uses a `;` record when a printer's code table
  field names it, and drives the printer with Diablo 630's ESC US n;
  *PSPARS is not read.

## Programs (visible equivalents)

A record performed or a keystore is typed as keys: `'X` Ctrl X, `$`
ESC, `;` Return, `(` and `:` the start and end keys, `@` the next
character as it is, `"..."` a comment, `!` or `?` a pause until
Return; the ends of the record's lines are Returns.  Typing the end
key after `<KEY` performs the record (`<KEY+n/m>` types part of it).
Menus: column 1 the menu switch option, columns 2-11 a record, column
12 the character that chooses the line, `/Smno` at the end highlights.

Things found by running LEX-68's own programs (*DTIR, *INSTFORM, the
list processing formats) in nylex:

* Up and down arrows keep their column in protect mode; left goes back
  over protected cells to a field; typing on a protected cell goes on to the next
  field.  Ctrl L goes to the last field on the screen.
* ESC N fills a key on a form field by field: a protected cell holding
  the key's character takes it, other protected cells are passed over
  (`<:______.____` shows `:NAME   .EXT`).  The key "last referenced"
  is that of the last record performed or read, even one that does not
  exist.
* In protect mode the protected text is the background and the fields
  the foreground (ESC C F clears the fields).
* Keystores cut from a form keep their blanks; a keystore typed at a
  prompt (ESC R K ...) is part of the answer.
* List processing data: the squashed runs are the form's paint and are
  blanks in the output.
