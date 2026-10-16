# nylex: a first tour

nylex 0.2, 16 October 2026

In about 20 minutes you write a short letter in nylex and then print it to PDF, spell-check it, export it to Word and print address labels. The steps use the English menus; the Swedish menu letters are in brackets.

## Before you start

Install everything, so the spelling and export menus find their programs and dictionaries:

```
make && sudo make install
mkdir ~/lextour && cd ~/lextour
nylex
```

- Documents are saved in the directory you start nylex from.
- The first time, nylex copies its system file to `~/nylex.vmf`: the Swedish one when the locale is Swedish (`LANG=sv_SE...`), the English one otherwise. `nylex -f ~/nylex-en.vmf` (or `nylex.vmf`) picks one yourself; delete `~/nylex.vmf` to choose again, which also empties its document index.
- Make the terminal window at least 81 columns wide. The rulers are 80 characters, so in an 80-column window they take two rows and end in `&`.
- Esc is a key of its own: press Esc, let go, then press the next key. `Esc E` below means exactly that.

## 1. Menus

Press the letter in front of a line to choose it; `Esc E` goes back one menu, and leaves nylex from the main menu.

- `H` opens seven help pages: cursor keys, Esc commands, rulers, cut and paste, print lines, programs, spelling.
- `Ctrl U` also goes back, as in LEX. The arrow keys move the cursor in a document and only beep at a menu.

## 2. Your first letter

Press `N` for a new document, type `LETTER` and Return. nylex adds the type `.LTR` (`.BRV` in Swedish), so the file is `letter.ltr`. Type a title and a description, each followed by Return.

The top line is the ruler, `F....M`: paragraphs start at F and wrap before M. Type these lines exactly; the two that start with a dot are print lines and never print themselves:

```
.T Letter to Anna
.F                                Page (PAGE:
Dear Anna,

This is my first letter written in nylex, a word processor made in the image of LEX from the 1980s.
```

- `.T` is the title at the top of every page and `.F` the footer; `(PAGE:` becomes the page number (`(SIDA:` in Swedish).
- The last line wraps by itself. Press Return only to end a paragraph.
- nylex starts in overwrite mode: `Esc I` turns on insert mode and `Esc - I` turns it off. Rubout deletes to the left, `Ctrl X` deletes a line and `Esc - X` brings it back.
- `Esc E` saves and returns to the menu.

To come back later, press `E` (`Ä`), type `LETTER`, then Return three times to keep the title and description. `Ctrl S` jumps to the end of the document and `Ctrl O` to the start.

## 3. Justify, centre, bold and underline

Open LETTER again, press `Ctrl S` to get to the end, then try each of these in turn:

1. `Esc M 2` puts in ruler 2, a `J` ruler. Type a long paragraph under it: it comes out justified, with a straight right margin, as you type. After changing a paragraph, `Esc J` wraps and justifies it again.
2. `Esc M 1` puts in ruler 1 again, for ordinary ragged text.
3. Type `Our offer`, then press `Esc @`: the line is centred. `Esc #` would push it against the right margin instead.
4. Type *Prices are*, press `Esc A 2`, type *very*, press `Esc B 2`, and go on with *low.* The word **very** is bold (attribute 2). Attribute 1 is underlining and 3 is italic: `Esc A n` turns one on, `Esc B n` turns it off.

Rulers 1 to 5 belong to the document type; `Esc W n` keeps the line the cursor is on as ruler n.

## 4. Keystores and the date

A keystore is a saved piece of text that one key types for you. The letter type comes with nine of them, and you can replace any.

1. Press `Esc 2`: *Thank you for your letter of* is typed at the cursor. `Esc 1` gives *Yours sincerely*.
2. Press `Esc S 1`, type `Kind regards,` and press Return. Nothing appears; the text is stored as keystore 1.
3. Press `Esc 1` again: now it types *Kind regards,*. A keystore you set lasts while you edit; to change the letter type's own for good, use its form (Y F D in the menus).
4. Press `Esc Y` for today's date in full, such as *16th October 2026*.

## 5. Number columns and the calculator

Ruler 4 has tab stops and two number fields, `####,###.##`, for lists of figures:

1. Press `Esc M 4`. Press Tab, type `Pens`, press Tab three times to reach the number field, type `12.50` and press Tab. The number lines up on its decimal point, and the second field shows `0.00`.
2. Press Return and do the same with `Paper` and `7.25`, then `Esc M 1` to go back to ruler 1.
3. Type `Total:` and press the right arrow 12 times. The calculator writes its answer so that it ends at the cursor and needs at least four empty columns to the left.
4. Press `Esc +`, type `12.50+7.25=` and press `Esc E`: *19.75* appears after *Total:*.

## 6. Print to PDF

Save with `Esc E`, then:

1. Press `P` (`U`), type `LETTER` and press Return three times.
2. At *Which printer?* press `7`, type `letter.pdf` and press Return.
3. Press Return at the three page questions (number from, print from page, last page).

Open `letter.pdf`: it is set in Times as a proportional daisy wheel would set it, with the title on top and *Page 1* at the foot. The justified paragraph still meets the margin.

- Printer 9 sends PostScript to `lp`, and printer 8 drives a Diablo-style daisy-wheel printer.
- The printer form (`Y F P`) changes a printer's type to `PDFSANS` (Helvetica) or `PDFMONO` (Courier at LEX's 10 characters to the inch).
- `LEXPAPER=letter` gives US Letter pages instead of A4.

## 7. Check the spelling

Make a deliberate mistake first: open LETTER and change *first* to *frist*. Then:

1. Press `S` (`O`) for the spelling menu, then `C` (`K`), type `LETTER` and press Return.
2. Each unknown word is shown in its line with numbered suggestions. Press `1` and Return to take *first*.
3. *nylex* is unknown too: press `L` and Return to add it to your own word list, so it is never asked about again.

The rest of the answers: Return or `A` accepts a word for this document, `R` lets you type a correction, `F` finishes and `Q` abandons. The old version is kept as `letter.bak`. In the same menu, `S` spells single words and `P` finds words by pattern, such as `c??s*` for a crossword.

## 8. Export to Word or Markdown, and import

1. Press `D` for the document menu, then `X`.
2. At *From* type `LETTER` and press Return; at *To* type `LETTER.DOCX` and press Return. The extension picks the format: `.DOCX`, `.ODT`, `.RTF`, `.MD`, `.HTML` or `.TXT`.
3. Open `letter.docx` in Word or LibreOffice. The paragraphs are whole again, justified where they were, with the bold word, the title and footer with a page number field, and the figures on tab stops.

Import works the same way. Write a small `notes.md` in any editor:

```
# Shopping

Buy **milk** and *bread*.

- apples
- pears
```

Then use `D X`, *From* `NOTES.MD`, *To* `NOTES`, and open NOTES with `E`. The heading is centred and bold, the list keeps its dashes and the words are laid out on a ruler. From a shell, `lexconv letter.ltr letter.rtf` does the same.

## 9. Addresses and labels

The address register keeps one record for each address, filled in on a form.

1. Press `A` (`B`), then `E` (`R`). The form opens with *Key not found*: press Return.
2. Type a key after the dot, such as `SMITH`, and press Tab. Fill in the fields, pressing Tab between them: name, two address lines, town, postcode and telephone.
3. Press `Ctrl L`, then `Esc > P` to save. Make a second record the same way, then press `Esc E`.
4. Press `V` and Return at *Choose*: the list of addresses comes up. Any key goes back.
5. Press `L` for labels, Return at *Choose*, then printer `7` and `labels.pdf`.

At *Choose* you can pick records instead of taking them all: `TOWN=Leeds`, `NAME:smi` (contains) or `KEY=A-M`, joined with AND, OR and NOT. The field names are listed above the question.

## Key card

| Keys | What they do |
| --- | --- |
| `Esc E` | Save and leave the document; back one menu |
| `Esc I` / `Esc - I` | Insert mode on / off |
| `Ctrl O` / `Ctrl S` | Start / end of the document |
| `Ctrl X` / `Esc - X` | Delete a line / bring it back |
| `Esc M n` | Put in ruler n (1 ragged, 2 justified, 4 number columns) |
| `Esc J` | Wrap the paragraph again |
| `Esc @` / `Esc #` | Centre / push against the right margin |
| `Esc A n` / `Esc B n` | Attribute n on / off (1 underline, 2 bold, 3 italic) |
| `Esc S n` | Store the text typed next as keystore n |
| `Esc n` | Type keystore n |
| `Esc Y` | Today's date in full |
| `Esc +` | Calculator; `Esc E` puts the answer in |
| `Esc G` | Find text |
| `.T` `.F` `.P` | Title, footer, new page (print lines) |

Menus (English, Swedish in brackets): `N` new, `E` (`Ä`) edit, `P` (`U`) print, `D` documents, `S` (`O`) spelling, `A` (`B`) addresses, `H` help, `Y` (`S`) system. `man nylex` lists every key.
