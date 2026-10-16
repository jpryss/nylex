/*
 * ui.c - errors (Appendix E), the quiet modes (shush, shut-up) and the
 * line input that LEX's prompts use.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

int		shush, shutup;
int		errcode[2] = { 'Z', 'Z' };
int		showerr;
void		(*ui_drawfn)(void);

static const struct { const char *code, *text; } errs[] = {
	{ "ZZ", "Inget fel" },
	{ "TB", "Mark|ren st}r redan |verst" },
	{ "EB", "Slutet av dokumentet" },
	{ "MS", "Markerad position syns inte" },
	{ "AX", "F|r l}ngt f|r en snabbtangent" },
	{ "BF", "Klippbufferten full" },
	{ "BR", "Felaktigt klipp-block" },
	{ "CM", "Inget har klippts" },
	{ "CB", "Kalkylatorn: posten finns ej" },
	{ "CC", "Kalkylatorn: ok{nt tecken" },
	{ "CE", "Kalkylatorn: felaktig syntax" },
	{ "CN", "Kalkylatorn beh|ver st|rre utrymme" },
	{ "CO", "Kalkylatorn: talet f|r stort" },
	{ "RI", "Felaktigt tecken i styrlinjen" },
	{ "HC", "Felaktigt avstavningstecken" },
	{ "HF", "N{sta rad {r full" },
	{ "NJ", "Raden kan inte justeras" },
	{ "PM", "Skyddat l{ge" },
	{ "IN", "Kan inte infoga tecken" },
	{ "MC", "ESC M f|ljt av ok{nt tecken" },
	{ "ME", "Slut p} inl{sningsfilen" },
	{ "MF", "Inl{sningsdokumentet finns ej" },
	{ "WF", "ESC W - ingen fil |ppen" },
	{ "MA", "Dokumentet finns redan" },
	{ "OD", "En fil {r redan |ppen" },
	{ "OW", "Kan inte |ppna dokumentet" },
	{ "MW", "Kan inte |ppna dokumentet" },
	{ "SL", "Sorteringskolumner olika breda" },
	{ "SR", "Sorteringsstyrlinjen ska ha L" },
	{ "XR", "ESC X utan str{ng att upprepa" },
	{ "XS", "ESC X utan villkorsstr{ng" },
	{ "NX", "Ingen nyckelb|rjan hittad" },
	{ "BB", "ESC > f|ljt av ok{nt tecken" },
	{ "BC", "Nyckeln finns redan" },
	{ "BK", "Nyckeln finns ej" },
	{ "BS", "Posten finns inte kvar" },
	{ "LK", "Posten {r l}st" },
	{ "NZ", "Nyckeln finns ej" },
	{ "MR", "Inget utrymme f|r mask" },
	{ "NM", "Maskposten finns ej" },
	{ "NP", "Inga ifyllnadstecken" },
	{ "TM", "Posten f|r stor f|r formul{ret" },
	{ "FF", "Kan inte |ppna yttre fil" },
	{ "FR", "Ingen skrivr{tt till yttre fil" },
	{ "NF", "VMF-filen n{stan full" },
	{ "NG", "VMF-filen full" },
	{ "NR", "Ingen skrivr{tt till filen" },
	{ "AC", "Ok{nd ESC-sekvens" },
	{ "DL", "Demoversion: f|r stor fil" },
	{ "EP", "Sidbrytning h{r" },
	{ "EV", "ESC V/QX g}r inte utan dokument" },
	{ "OF", "Filen finns redan" },
	{ "NK", "Felaktig nyckel i ESC QS" },
	{ "AA", "Inget talf{lt" },
	{ "XX", "F|r m}nga niv}er" },
	{ 0, 0 }
};

static const struct { const char *code, *text; } errs_en[] = {
	{ "ZZ", "No error" },
	{ "TB", "Cursor already at the top" },
	{ "EB", "End of document" },
	{ "MS", "Marked position not on screen" },
	{ "AX", "Cut too long for keystore" },
	{ "BF", "Cut and paste buffer full" },
	{ "BR", "Invalid cut and paste block" },
	{ "CM", "You haven't done a cut" },
	{ "CB", "Calculator: invalid use of a record" },
	{ "CC", "Invalid calculator character" },
	{ "CE", "Invalid calculator syntax" },
	{ "CN", "Calculator needs more room" },
	{ "CO", "Calculator number overflow" },
	{ "RI", "Invalid ruler character" },
	{ "HC", "Invalid hyphen character" },
	{ "HF", "Next line is already full" },
	{ "NJ", "Line cannot be justified" },
	{ "PM", "Protect mode" },
	{ "IN", "Cannot insert characters" },
	{ "MC", "ESC M with an invalid character" },
	{ "ME", "End of merge file" },
	{ "MF", "Merge document not found" },
	{ "WF", "ESC W - no file open" },
	{ "MA", "Document already exists" },
	{ "OD", "A file is already open" },
	{ "OW", "Cannot open the document" },
	{ "MW", "Cannot open the document" },
	{ "SL", "Sort columns of different widths" },
	{ "SR", "The sort ruler needs an L" },
	{ "XR", "ESC X without a string to repeat" },
	{ "XS", "ESC X without a condition string" },
	{ "NX", "No start of a key found" },
	{ "BB", "ESC > with an invalid character" },
	{ "BC", "Key already exists" },
	{ "BK", "Key not found" },
	{ "BS", "Saved key no longer on file" },
	{ "LK", "Key is locked" },
	{ "NZ", "Key not found" },
	{ "MR", "No room for mask" },
	{ "NM", "No masking record on file" },
	{ "NP", "No paint characters" },
	{ "TM", "Record too big for form" },
	{ "FF", "Unable to open foreign file" },
	{ "FR", "Foreign file is read only" },
	{ "NF", "VMF nearly full" },
	{ "NG", "VMF full" },
	{ "NR", "No write access to the file" },
	{ "AC", "Invalid escape sequence" },
	{ "DL", "Demonstration version: file too big" },
	{ "EP", "Page break here" },
	{ "EV", "ESC V/QX need a document" },
	{ "OF", "File already exists" },
	{ "NK", "Invalid key in ESC QS" },
	{ "AA", "Not a numeric field" },
	{ "XX", "Too many levels" },
	{ 0, 0 }
};

const char *
errtext(const char *code)
{
	static char b[64];
	char key[11], *t;
	long len;
	int i;

	sprintf(key, "*-ERROR-%.2s", code);
	if (rec_text(key, &t, &len, '_') == 0) {
		/* the text is in the cells after the first (*ERRORFORM) */
		int n = len - 1 > 40 ? 40 : (int)len - 1;

		if (n < 0)
			n = 0;
		memcpy(b, t + (len > 0), n);
		b[n] = 0;
		for (i = 0; b[i]; i++)
			if (b[i] == '\n')
				b[i] = ' ';
		xfree(t);
		while (b[0] == ' ')
			memmove(b, b + 1, strlen(b));
		return b;
	}
	for (i = 0; errs[i].code; i++)
		if (strncmp(errs[i].code, code, 2) == 0) {
			sprintf(b, "**** %s ****", t_national ? errs[i].text : errs_en[i].text);
			return b;
		}
	sprintf(b, "**** ERROR-%.2s ****", code);
	return b;
}

void
bell(void)
{
	if (!shutup)
		t_bell();
}

extern void ed_message(const char *s);

void
error(const char *code)
{
	errcode[0] = code[0];
	errcode[1] = code[1];
	bell();
	if (strcmp(code, "EB") == 0 || strcmp(code, "NZ") == 0 || strcmp(code, "BK") == 0 ||
	    strcmp(code, "TB") == 0 || strcmp(code, "ME") == 0)
		in_stoprepeat();
	if (showerr && !shutup)
		ed_message(errtext(code));
}

int
ui_quiet(void)
{
	return shutup || (shush && ui_drawfn != NULL);
}

void
ui_refresh(void)
{
	if (shutup)
		return;
	if (ui_drawfn)
		ui_drawfn();
	t_refresh();
}

void
ui_text(int row, int col, const char *s, int attr)
{
	if (!shutup)
		t_puts(row, col, s, attr);
}

void
ui_center(int row, const char *s, int attr)
{
	int c = (t_cols - 1 - (int)strlen(s)) / 2;	/* as LEX-68 centres */

	ui_text(row, c > 0 ? c : 0, s, attr);
}

/* a key, waited for (Return and the like at messages) */
int
ui_waitkey(void)
{
	return in_key();
}

void
ui_pause(const char *msg)
{
	int k;

	if (in_depth() > 0)
		return;
	t_clearrow(t_rows - 1);
	ui_text(t_rows - 1, 0, msg, vdu_bgattr);
	t_move(t_rows - 1, (int)strlen(msg) < t_cols ? (int)strlen(msg) : t_cols - 1);
	t_refresh();
	k = in_key();
	(void)k;
}

/*
 * A line typed in background mode after a prompt, as LEX's string
 * prompts work: what buf holds is shown; a character typed over its
 * first character empties it, otherwise it is changed in place.  Ended
 * by Return or Line feed (*term says which); -1 for ESC E (or ESC Z,
 * ^C).  width: the room for the text (0: to the end of the row).
 */
int
ui_line(int row, const char *prompt, char *buf, int max, int width, int *term)
{
	int pos = 0, n = (int)strlen(buf), k, col = (int)strlen(prompt), i, first = 1;
	if (width <= 0)
		width = t_cols - col - 1;
	if (width > max - 1)
		width = max - 1;
	for (;;) {
		if (!shutup && !(in_depth() > 0)) {
			if (ui_drawfn)
				ui_drawfn();
			t_clearrow(row);
			t_puts(row, 0, prompt, vdu_bgattr);
			for (i = 0; i < n && col + i < t_cols; i++)
				t_put(row, col + i, (unsigned char)buf[i], vdu_bgattr);
			t_move(row, col + pos < t_cols ? col + pos : t_cols - 1);
		}
		k = in_key();
		if (k == K_NONE || KC(k) == CTL('C')) {
			if (k == K_NONE || !(k & K_LIT))
				return -1;
		}
		if (!(k & K_LIT)) {
			if (k == CR || k == LF) {
				if (term)
					*term = k;
				break;
			}
			if (k == ESC) {
				int e = upcase(KC(in_key()));

				if (e == 'E' || e == 'Z')
					return -1;
				if (e >= '0' && e <= '9' && keystore[e - '0'][0]) {
					/* a keystore at a prompt: what it holds is typed */
					in_prog(keystore[e - '0'], (long)strlen(keystore[e - '0']), 0);
					first = 0;
				}
				continue;
			}
			if (k == CTL('H')) {
				if (pos > 0)
					pos--;
				first = 0;
				continue;
			}
			if (k == CTL('R')) {
				if (pos < n && pos < width - 1)
					pos++;
				first = 0;
				continue;
			}
			if (k == DEL) {
				if (pos > 0) {
					pos--;
					memmove(buf + pos, buf + pos + 1, n - pos);
					n--;
				}
				first = 0;
				continue;
			}
			if (k == CTL('X') || k == CTL('V')) {
				n = pos;
				buf[n] = 0;
				continue;
			}
			if ((k == CTL('U') || k == M.menuexit) && !vdu_special) {
				if (term)
					*term = k;
				return -1;
			}
			if (KC(k) < 32)
				continue;
		}
		if (first && pos == 0) {
			n = 0;
			buf[0] = 0;
		}
		first = 0;
		if (pos < width) {
			buf[pos] = (char)KC(k);
			pos++;
			if (pos > n)
				n = pos;
			buf[n] = 0;
		}
	}
	buf[n] = 0;
	return n;
}

/* a Y/N question: 1 yes, 0 no, -1 ESC E */
int
ui_yesno(int row, int col, const char *q)
{
	int k;

	for (;;) {
		if (!shutup && !(in_depth() > 0)) {
			t_puts(row, col, q, A_NORM);
			t_move(row, col + (int)strlen(q));
		}
		k = in_key();
		if (k == K_NONE)
			return -1;
		if (k == ESC) {
			k = upcase(KC(in_key()));
			if (k == 'E')
				return 0;
			if (k == 'Z')
				return -1;
			continue;
		}
		if (KC(k) == CTL('C'))
			return 0;
		if (is_yes(KC(k)))
			return 1;
		if (is_no(KC(k)))
			return 0;
	}
}
