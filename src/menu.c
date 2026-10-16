/*
 * menu.c - menus (manual chapter 10) and the menu switch options
 * (Appendix A): the documents, printing, list processing, the system
 * file and the programs LEX hands over to.
 *
 * A menu is a record: its first line the heading, then one line per
 * choice: column 1 the switch option, columns 2-11 a record for it,
 * column 12 the character that chooses it, then its text.  "/Smno" at
 * the end of a line asks for highlights.  At a menu "/" and a switch
 * option can be typed too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int	putenv(char *);		/* not in strict POSIX headers; everywhere */
#include "lex.h"

char		menustack[MAXMENU][KEYLEN + 1];
int		menudepth;
char		lastdoc[MAXNAME + 1];
char		curopt[16];
int		quitting;
extern void	(*ui_drawfn)(void);

static const char *progtitle = "NYLEX " LEX_VERSION;

#define MAXITEMS	48

struct menu {
	char	line[MAXITEMS][128];
	int	n;
	char	key[KEYLEN + 1];
};

static struct menu	cur;
static char		heading[128];
static int		heading_col = 20;

/* Fields to fill in: as LEX's, underlines show the room */

/*
 * The cursor at its start; what is typed goes over what is there.
 * Return accepts (Line feed too: *term); ESC E gives up (-1).
 */
static int
field(int row, int col, int width, char *buf, int maxlen, int *term)
{
	char f[256];
	int n, k, i, pos = 0, hw;

	if (width > maxlen)
		width = maxlen;
	if (width > (int)sizeof f - 1)
		width = (int)sizeof f - 1;
	memset(f, ' ', sizeof f);
	n = (int)strlen(buf);
	memcpy(f, buf, n > width ? width : n);
	hw = n > width ? width : n;
	if (term)
		*term = CR;
	for (;;) {
		if (!shutup && in_depth() == 0) {
			for (i = 0; i < width; i++)
				t_put(row, col + i, i < hw ? (unsigned char)f[i] : '_', i < hw ? A_NORM : A_NORM);
			t_move(row, col + (pos < width ? pos : width - 1));
		}
		k = in_key();
		if (k == K_NONE)
			return -1;
		if (!(k & K_LIT)) {
			if (k == CR || k == LF) {
				if (term)
					*term = k;
				break;
			}
			if (k == ESC) {
				k = upcase(KC(in_key()));
				if (k == 'E' || k == 'Z')
					return -1;
				continue;
			}
			if (k == CTL('C') || ((k == M.menuexit || k == CTL('U')) && !vdu_special))
				return -1;
			if (k == DEL) {
				if (pos > 0)
					f[--pos] = ' ';
				continue;
			}
			if (k == CTL('H')) {
				if (pos > 0)
					pos--;
				continue;
			}
			if (k == CTL('R')) {
				if (pos < width - 1)
					pos++;
				continue;
			}
			if (k == CTL('X') || k == CTL('V')) {
				memset(f + pos, ' ', width - pos);
				hw = pos;
				continue;
			}
			if (KC(k) < 32)
				continue;
		}
		if (pos < width) {
			f[pos++] = (char)KC(k);
			if (pos > hw)
				hw = pos;
		}
	}
	for (n = width; n > 0 && f[n - 1] == ' '; n--)
		;
	memcpy(buf, f, n);
	buf[n] = 0;
	return n;
}

static void
cleartop(void)
{
	t_clearrow(0);
	t_clearrow(1);
}

static void
topmsg(const char *a, const char *b)
{
	if (shutup)
		return;
	cleartop();
	t_puts(0, 0, a, A_NORM);
	t_puts(1, 0, b, A_NORM);
}

/* The dialogs of the document options */

static void
dialog_start(void)
{
	if (shutup)
		return;
	t_clear();
	if (heading[0]) {
		t_puts(7, heading_col, heading, A_NORM);
		t_put(7, heading_col, (unsigned char)heading[0], vdu_bgattr);
	}
}

static void
show_reg(int row, const struct regent *r)
{
	char b[160];

	if (shutup || !I.summary)
		return;
	t_clearrow(row);
	t_clearrow(row + 1);
	t_puts(row, 0, text(0, 4), A_NORM);
	if (r) {
		char na[16], ne[16], np[16], tt[40], ln[24], wd[24], ch[24];

		sprintf(ne, r->nedit >= 0 ? "%02d" : "  ", r->nedit);
		sprintf(na, r->nacc >= 0 ? "%02d" : "  ", r->nacc);
		sprintf(np, r->nprint >= 0 ? "%02d" : "  ", r->nprint);
		if (r->tottime >= 0)
			sprintf(tt, "%02ld%02ld%02ld", r->tottime / 3600 % 100, r->tottime / 60 % 60, r->tottime % 60);
		else
			strcpy(tt, "      ");
		sprintf(ln, r->lines >= 0 ? "%06ld" : "      ", r->lines);
		sprintf(wd, r->words >= 0 ? "%08ld" : "        ", r->words);
		sprintf(ch, r->chars >= 0 ? "%08ld" : "        ", r->chars);
		sprintf(b, "%-7s %-7s %s %-7s %s %-7s %s %-6s %s %s %s %s",
		    r->created, r->edited, ne, r->accessed, na, r->printed, np, r->last,
		    tt, ln, wd, ch);
		t_puts(row + 1, 0, b, A_NORM);
	}
}

/* the next document in the index after name (Line feed at the prompt) */
static void
nextdoc(char *name)
{
	char key[12], k[KEYLEN];
	int i;

	reg_key(key, name[0] ? name : " ");
	mkkey(k, key);
	if (sysvmf == NULL)
		return;
	i = vmf_seek(sysvmf, k);
	while (i < sysvmf->nent && (memcmp(sysvmf->ent[i].key, k, KEYLEN) <= 0 ||
	    (sysvmf->ent[i].flags & VMF_DELETED)))
		i++;
	if (i >= sysvmf->nent || sysvmf->ent[i].key[0] != ':') {
		i = vmf_seek(sysvmf, ":         ");
		while (i < sysvmf->nent && (sysvmf->ent[i].flags & VMF_DELETED))
			i++;
		if (i >= sysvmf->nent || sysvmf->ent[i].key[0] != ':')
			return;
	}
	{
		struct regent r;
		char nm[MAXNAME + 1];
		int j, n = 0;

		for (j = 1; j < 7 && sysvmf->ent[i].key[j] != ' '; j++)
			nm[n++] = sysvmf->ent[i].key[j];
		nm[n++] = '.';
		for (j = 7; j < 10 && sysvmf->ent[i].key[j] != ' '; j++)
			nm[n++] = sysvmf->ent[i].key[j];
		nm[n] = 0;
		if (reg_get(nm, &r) == 0 && r.name[0])
			strcpy(name, r.name);
		else
			strcpy(name, nm);
	}
}

/*
 * The name of a document, on row 11.  must: 1 it must exist, 0 it must
 * not, -1 either.  Returns 0, or -1 if given up.
 */
static int
ask_name(char *name, int must, int row)
{
	char path[300], b[120];
	int term;

	for (;;) {
		if (!shutup && in_depth() == 0)
			t_puts(row, 12, text(0, 1), A_NORM);
		if (field(row, 12 + (int)strlen(text(0, 1)), 40, name, MAXNAME, &term) < 0)
			return -1;
		if (term == LF) {
			nextdoc(name);
			continue;
		}
		if (name[0] == 0) {
			topmsg(text(0, 1), text(0, 16));
			bell();
			continue;
		}
		docname_norm(name, I.defext);
		docname_file(path, name);
		if (must == 1 && !fexists(path)) {
			sprintf(b, "%s%s", text(0, 1), name);
			topmsg(b, text(0, 18));
			bell();
			continue;
		}
		if (must == 0 && fexists(path)) {
			sprintf(b, "%s%s", text(0, 1), name);
			topmsg(b, text(0, 17));
			bell();
			continue;
		}
		strcpy(lastdoc, name);
		return 0;
	}
}

/* the index entry of a document, shown; its title (and detail) asked */
static int
reg_and_title(const char *name, struct regent *r, int known, int row)
{
	int term;

	if (reg_get(name, r) < 0) {
		reg_new(r, name);
		known = 0;
	}
	show_reg(row, known ? r : NULL);
	if (I.titling) {
		if (!shutup && in_depth() == 0)
			t_puts(row + 3, 12, text(0, 2), A_NORM);
		if (field(row + 3, 12 + (int)strlen(text(0, 2)), 40, r->title, 40, &term) < 0)
			return -1;
		if (I.detail) {
			if (!shutup && in_depth() == 0)
				t_puts(row + 4, 12, text(0, 3), A_NORM);
			if (field(row + 4, 12 + (int)strlen(text(0, 3)), 40, r->detail, 40, &term) < 0)
				return -1;
		}
	}
	return 0;
}

static int
isbackup(const char *name)
{
	const char *dot = strrchr(name, '.');
	char e[8];
	int i;

	if (dot == NULL)
		return 0;
	for (i = 0; dot[i] && i < 7; i++)
		e[i] = (char)upcase((unsigned char)dot[i]);
	e[i] = 0;
	return I.bakext[0] && strcmp(e, I.bakext) == 0;
}

static void
setext(const char *name)
{
	docname_ext(name, ED.ext);
}

/* /C /E /I /V: a document to edit */
static int
do_edit(int opt, const char *rec)
{
	char name[MAXNAME + 1], path[300];
	struct regent r;
	int mode = opt == 'C' ? ED_NEW : opt == 'V' ? ED_VIEW : ED_EDIT, ex;

	dialog_start();
	if (mode == ED_VIEW && !shutup)
		t_puts(10, 12, text(0, 9), A_NORM);
	strcpy(name, mode == ED_NEW ? "" : lastdoc);
	for (;;) {
		if (ask_name(name, mode == ED_NEW ? 0 : 1, 11) < 0)
			return -1;
		if (mode != ED_VIEW && isbackup(name)) {
			topmsg(name, text(0, 19));
			bell();
			continue;
		}
		break;
	}
	docname_file(path, name);
	if (reg_and_title(name, &r, mode != ED_NEW, 13) < 0)
		return -1;
	setext(name);
	if (mode == ED_NEW) {
		FILE *f = fopen(path, "ab");

		if (f)
			fclose(f);
		if (!fexists(path)) {
			topmsg(name, text(0, 16));
			return -1;
		}
	}
	ex = edit_run(mode, path, name, rec);
	if (ex == EX_ABANDON && mode == ED_NEW) {
		/* nothing kept of a new document given up */
		FILE *f = fopen(path, "rb");

		if (f) {
			int c = getc(f);

			fclose(f);
			if (c == EOF)
				remove(path);
		}
		return 0;
	}
	if (ex == EX_EXIT || ex == EX_ABANDON || ex == EX_MENU) {
		if (mode == ED_VIEW || ex == EX_ABANDON)
			reg_touch(&r, 'A', 0);
		else {
			reg_touch(&r, 'E', (long)time(NULL) - ED.started);
			r.lines = ED_lines;
			r.words = ED_words;
			r.chars = ED_chars;
		}
		if (mode != ED_NEW || fexists(path))
			reg_put(&r);
	}
	return 0;
}

/* /A (and edit) and /J (a copy, block by block) */
static int
do_copy(int andedit, const char *rec, int block)
{
	char from[MAXNAME + 1], to[MAXNAME + 1], pf[300], pt[300];
	struct regent r, rt;

	dialog_start();
	if (!shutup)
		t_puts(10, 12, text(0, 7), A_NORM);
	strcpy(from, lastdoc);
	if (ask_name(from, 1, 11) < 0)
		return -1;
	if (reg_and_title(from, &r, 1, 13) < 0)
		return -1;
	reg_touch(&r, 'A', 0);
	reg_put(&r);
	if (!shutup) {
		int i;

		for (i = 10; i < 18; i++)
			t_clearrow(i);
		t_puts(10, 12, text(0, 8), A_NORM);
	}
	strcpy(to, from);
	if (ask_name(to, 0, 11) < 0)
		return -1;
	rt = r;
	scopy(rt.name, to, 40);
	if (reg_and_title(to, &rt, 0, 13) < 0)
		return -1;
	docname_file(pf, from);
	docname_file(pt, to);
	if (!shutup) {
		t_clearrow(20);
		t_puts(20, 12, text(0, 27), A_NORM);
		t_refresh();
	}
	if (copyfile(pf, pt) < 0) {
		topmsg(to, text(0, 16));
		return -1;
	}
	rt.nedit = 0;
	reg_touch(&rt, 'A', 0);
	reg_put(&rt);
	if (andedit && !block) {
		setext(to);
		if (edit_run(ED_EDIT, pt, to, rec) == EX_EXIT) {
			reg_touch(&rt, 'E', (long)time(NULL) - ED.started);
			rt.lines = ED_lines;
			rt.words = ED_words;
			rt.chars = ED_chars;
			reg_put(&rt);
		}
	}
	return 0;
}

/* /K: one document after another, until ESC E */
static int
do_delete(void)
{
	char name[MAXNAME + 1], path[300];
	struct regent r;

	dialog_start();
	strcpy(name, lastdoc);
	for (;;) {
		if (ask_name(name, 1, 11) < 0)
			return 0;
		if (reg_get(name, &r) < 0)
			reg_new(&r, name);
		show_reg(13, &r);
		docname_file(path, name);
		remove(path);
		reg_delete(name);
		topmsg(name, text(0, 28));
	}
}

/* /R: rename */
static int
do_rename(void)
{
	char from[MAXNAME + 1], to[MAXNAME + 1], pf[300], pt[300];
	struct regent r;

	dialog_start();
	if (!shutup)
		t_puts(10, 12, text(0, 5), A_NORM);
	strcpy(from, lastdoc);
	if (ask_name(from, 1, 11) < 0)
		return -1;
	if (reg_and_title(from, &r, 1, 13) < 0)
		return -1;
	if (!shutup) {
		int i;

		for (i = 10; i < 18; i++)
			t_clearrow(i);
		t_puts(10, 12, text(0, 6), A_NORM);
	}
	strcpy(to, from);
	if (ask_name(to, 0, 11) < 0)
		return -1;
	docname_file(pf, from);
	docname_file(pt, to);
	if (rename(pf, pt) != 0) {
		topmsg(to, text(0, 16));
		return -1;
	}
	reg_delete(from);
	scopy(r.name, to, 40);
	reg_put(&r);
	strcpy(lastdoc, to);
	return 0;
}

/* /P, /Q: print */
static int
do_print(int many)
{
	char name[MAXNAME + 1], path[300], b[8];
	struct regent r;
	int copies = 1, term;

	dialog_start();
	strcpy(name, lastdoc);
	if (ask_name(name, 1, 11) < 0)
		return -1;
	if (reg_and_title(name, &r, 1, 13) < 0)
		return -1;
	if (many) {
		if (!shutup)
			t_puts(19, 12, text(2, 14), A_NORM);
		b[0] = 0;
		if (field(19, 12 + (int)strlen(text(2, 14)), 3, b, 4, &term) < 0)
			return -1;
		copies = atoi(b) > 0 ? atoi(b) : 1;
	}
	docname_file(path, name);
	setext(name);
	ed_load_details(ED.ext);
	if (print_doc(path, copies, 1) == 0) {
		reg_touch(&r, 'P', 0);
		reg_put(&r);
	}
	return 0;
}

/* /T: the statistics of the system file, laid out as LEX-68's */
static void
statline(int row, const char *label, long v, int blank)
{
	char b[16];

	t_puts(row, 17, label, A_NORM);
	if (!blank) {
		sprintf(b, "%5ld", v);
		t_puts(row, 57, b, A_NORM);
	}
}

static void
vmf_statistics(void)
{
	long used, fr, keys, index, ovf, total;
	char b[80];
	int n;

	if (sysvmf == NULL)
		return;
	vmf_stats(sysvmf, &used, &fr, &keys, &index, &ovf, &total);
	t_clear();
	t_puts(3, 32, text(1, 32), A_NORM);
	n = (int)strlen(text(1, 32));
	memset(b, '-', n < 79 ? n : 79);
	b[n < 79 ? n : 79] = 0;
	t_puts(4, 32, b, A_NORM);
	t_puts(6, 17, text(1, 27), A_NORM);
	t_puts(6, 57, vmfname, A_NORM);
	t_puts(7, 57, text(1, I.multi ? 41 : 42), A_NORM);
	statline(9, text(1, 33), total, 0);
	statline(10, text(1, 2), used, 0);
	statline(11, text(1, 3), fr, 0);
	statline(12, text(1, 5), index, 0);
	statline(13, text(1, 4), keys, 0);
	statline(14, text(1, 6), ovf, ovf == 0);
	t_puts(16, 32, text(0, 26), A_NORM);
	n = (int)strlen(text(0, 26));
	while (n > 0 && text(0, 26)[n - 1] == ' ')
		n--;
	t_move(16, 32 + n);
	t_refresh();
	in_key();
}

/* /W: the state of LEX */
static void
lex_status(void)
{
	char b[400], fb[100], list[64];
	int i;

	t_clear();
	ui_center(1, text(1, 26), vdu_bgattr);
	sprintf(b, "%s%s  %s", text(1, 27), vmfname, text(1, I.multi ? 41 : 42));
	t_puts(3, 4, b, A_NORM);
	sprintf(b, "%s)%s", text(0, 23), vdukey);
	t_puts(4, 4, b, A_NORM);
	sprintf(b, "%s%s", text(1, 28), workname);
	t_puts(5, 4, b, A_NORM);
	if (sysvmf) {
		sprintf(b, "%s%ld", text(1, 33), sysvmf->nblocks);
		t_puts(6, 4, b, A_NORM);
	}
	sprintf(b, "%s%s %s %s", text(1, 40), instkey, pnstkey, montkey);
	t_puts(7, 4, b, A_NORM);
	t_puts(9, 4, text(1, 29), A_NORM);
	for (i = 0; i < menudepth; i++) {
		sprintf(b, "%s M%s", i == menudepth - 1 ? "***" : "   ", menustack[i]);
		t_puts(10 + i, 8, b, A_NORM);
	}
	if (foreign_desc(fb)) {
		sprintf(b, "%s%s", text(1, 31), fb);
		t_puts(17, 4, b, A_NORM);
	}
	cut_used(list);
	sprintf(b, "%s%s", text(1, 30), list);
	t_puts(18, 4, b, A_NORM);
	calc_used(list);
	sprintf(b, "%s %s", text(1, 34), list);
	t_puts(19, 4, b, A_NORM);
	ui_pause(text(0, 26));
}

/* a key for an option: the prompt "Key", the first character given */
static int
askkey(char *key, int first)
{
	char b[KEYLEN + 1];
	int row = t_rows - 2, term;

	b[0] = 0;
	if (!shutup && in_depth() == 0) {
		t_clearrow(row);
		t_puts(row, 0, text(0, 24), A_NORM);
		if (first)
			t_put(row, (int)strlen(text(0, 24)), first, A_NORM);
	}
	if (field(row, (int)strlen(text(0, 24)) + (first ? 1 : 0), first ? 9 : 32, b, 33, &term) < 0)
		return -1;
	if (first) {
		key[0] = (char)first;
		strcpy(key + 1, b);
	} else
		strcpy(key, b);
	return 0;
}

static int
askline(const char *prompt, char *b, int width)
{
	int row = t_rows - 2, term;

	if (!shutup && in_depth() == 0) {
		t_clearrow(row);
		t_puts(row, 0, prompt, A_NORM);
	}
	return field(row, (int)strlen(prompt), width, b, width + 1, &term);
}

/* /3 and /4: keys to copy, with ? as a wildcard and /+ //key2 /N /Q */
static int
wildmatch(const char *pat, const char *key)
{
	int i;

	for (i = 0; i < KEYLEN; i++) {
		int p = pat[i] ? (unsigned char)pat[i] : ' ';

		if (p != '?' && p != (unsigned char)key[i])
			return 0;
		if (!pat[i]) {
			/* the rest blank */
			int j;

			for (j = i; j < KEYLEN; j++)
				if (key[j] != ' ')
					return 0;
			return 1;
		}
	}
	return 1;
}

static int
copyrec(struct vmf *from, struct vmf *to, int i, int force, int query, const char *newkey)
{
	long len;
	unsigned char *d = vmf_entdata(from, i, &len);
	char k[KEYLEN];
	char b[100];

	memcpy(k, newkey ? newkey : from->ent[i].key, KEYLEN);
	if (vmf_find(to, k, 0) >= 0 && !force) {
		int r;

		sprintf(b, "%.10s%s", k, text(1, 19));
		t_clearrow(t_rows - 1);
		r = ui_yesno(t_rows - 1, 0, b);
		if (r != 1)
			return 0;
	}
	(void)query;
	if (vmf_put(to, k, d, len) < 0)
		return -1;
	if (!shutup) {
		sprintf(b, "%.10s%s", k, text(1, 17));
		t_clearrow(t_rows - 1);
		t_puts(t_rows - 1, 0, b, A_NORM);
		t_refresh();
	}
	return 1;
}

static void
copykeys(struct vmf *from, struct vmf *to)
{
	char b[40], k[KEYLEN + 1], k2[KEYLEN + 1];

	for (;;) {
		int plus = 0, force = 0, query = 0, i, n;
		char *o;

		b[0] = 0;
		if (askline(text(1, 15), b, 32) < 0)
			break;
		if (strcmp(b, "HELP") == 0 || strcmp(b, M.help) == 0) {
			char *t;
			long len;

			if (rec_text("*HELPV", &t, &len, '_') == 0) {
				int row = 3;
				char *p = t, *e;

				t_clear();
				while (*p && row < t_rows - 3) {
					e = strchr(p, '\n');
					if (e)
						*e = 0;
					t_puts(row++, 0, p, A_NORM);
					if (!e)
						break;
					p = e + 1;
				}
				xfree(t);
			}
			continue;
		}
		k2[0] = 0;
		if ((o = strstr(b, "//")) != NULL) {
			mkkey(k2, o + 2);
			k2[KEYLEN] = 0;
			*o = 0;
		}
		while ((o = strrchr(b, '/')) != NULL) {
			int c = upcase((unsigned char)o[1]);

			if (c == '+')
				plus = 1;
			else if (c == 'N')
				force = 1;
			else if (c == 'Q')
				query = 1;
			*o = 0;
		}
		{
			int j;

			for (j = 0; b[j] && j < KEYLEN; j++)
				b[j] = (char)upcase((unsigned char)b[j]);
			memset(k, ' ', KEYLEN);
			memcpy(k, b, strlen(b) > KEYLEN ? KEYLEN : strlen(b));
			k[KEYLEN] = 0;
		}
		n = 0;
		for (i = 0; i < from->nent; i++) {
			struct vmfent *e = &from->ent[i];

			if (e->flags & VMF_DELETED)
				continue;
			if (k2[0]) {
				if (memcmp(e->key, k, KEYLEN) < 0 || memcmp(e->key, k2, KEYLEN) > 0)
					continue;
			} else if (plus) {
				if (memcmp(e->key, k, KEYLEN) < 0)
					continue;
			} else if (!wildmatch(b, e->key))
				continue;
			if (query) {
				char q[60];

				sprintf(q, "%.10s ? ", e->key);
				t_clearrow(t_rows - 1);
				if (ui_yesno(t_rows - 1, 0, q) != 1)
					continue;
			}
			if (copyrec(from, to, i, force, query, NULL) > 0)
				n++;
		}
		if (n == 0) {
			sprintf(b, "%s%s", k, text(1, 18));
			t_clearrow(t_rows - 1);
			t_puts(t_rows - 1, 0, b, A_NORM);
			bell();
		}
	}
}

static void
withext(char *name, const char *ext)
{
	const char *base = strrchr(name, '/');

	if (strchr(base ? base : name, '.') == NULL)
		strcat(name, ext);
}

/* /4: records from or to another system file */
static void
copy_vmf(void)
{
	char name[200], b[400];
	struct vmf *o;
	int ft;

	t_clear();
	sprintf(b, "%s%s", text(1, 12), vmfname);
	t_puts(2, 0, b, A_NORM);
	name[0] = 0;
	if (askline(text(1, 13), name, 60) <= 0)
		return;
	withext(name, ".vmf");
	if ((o = vmf_open(name)) == NULL) {
		sprintf(b, "%s%s", name, text(1, 20));
		ui_pause(b);
		return;
	}
	t_clearrow(t_rows - 2);
	for (;;) {
		int k;

		t_puts(t_rows - 2, 0, text(1, 14), A_NORM);
		t_move(t_rows - 2, (int)strlen(text(1, 14)));
		k = upcase(KC(in_key()));
		if (k == ESC) {
			vmf_close(o);
			return;
		}
		if (k == M.from || k == M.to) {
			ft = k;
			break;
		}
	}
	if (I.helpcopy) {
		char *t;
		long len;

		if (rec_text("*HELPV", &t, &len, '_') == 0) {
			int row = 4;
			char *p = t, *e;

			while (*p && row < t_rows - 3) {
				e = strchr(p, '\n');
				if (e)
					*e = 0;
				t_puts(row++, 0, p, A_NORM);
				if (!e)
					break;
				p = e + 1;
			}
			xfree(t);
		}
	}
	if (ft == M.from)
		copykeys(sysvmf, o);
	else {
		copykeys(o, sysvmf);
		text_init();
	}
	vmf_close(o);
}

/* /3: the system file made anew */
static void
reorganise(void)
{
	char name[200], b[400], q[40];
	long used, fr, keys, index, ovf, total, nb, nk;
	struct vmf *n;
	int i, aut, all;

	vmf_stats(sysvmf, &used, &fr, &keys, &index, &ovf, &total);
	t_clear();
	sprintf(b, "%s%s", text(1, 1), vmfname);
	t_puts(1, 0, b, A_NORM);
	sprintf(b, "%s %6ld   %s %6ld", text(1, 2), used, text(1, 4), keys);
	t_puts(3, 0, b, A_NORM);
	sprintf(b, "%s %6ld   %s %6ld", text(1, 3), fr, text(1, 5), index);
	t_puts(4, 0, b, A_NORM);
	name[0] = 0;
	if (askline(text(1, 7), name, 60) <= 0)
		return;
	withext(name, ".vmf");
	t_clearrow(t_rows - 2);
	aut = ui_yesno(t_rows - 2, 0, text(1, 8));
	if (aut < 0)
		return;
	nb = total;
	nk = keys * 2 > 400 ? keys * 2 : 400;
	if (!aut) {
		sprintf(q, "%ld", nb);
		if (askline(text(1, 9), q, 8) < 0)
			return;
		if (atol(q) > 0)
			nb = atol(q);
		sprintf(q, "%ld", nk);
		if (askline(text(1, 10), q, 8) < 0)
			return;
		if (atol(q) > 0)
			nk = atol(q);
	}
	n = vmf_new(name);
	memcpy(n->hdr, sysvmf->hdr, 32);
	n->n2li = (int)((nk + 35) / 36);
	t_clearrow(t_rows - 2);
	all = aut ? 1 : ui_yesno(t_rows - 2, 0, text(1, 21));
	if (all < 0) {
		vmf_close(n);
		return;
	}
	for (i = 0; i < sysvmf->nent; i++) {
		struct vmfent *e = &sysvmf->ent[i];
		long len;
		unsigned char *d;

		if (e->flags & VMF_DELETED)
			continue;
		if (!all && !(e->key[0] == '*' && e->key[1] == '*') && e->key[0] != ')')
			continue;
		d = vmf_entdata(sysvmf, i, &len);
		{
			int j = n->nent;

			if (j == n->maxent) {
				n->maxent = n->maxent ? n->maxent * 2 : 512;
				n->ent = (struct vmfent *)xrealloc(n->ent, n->maxent * sizeof(struct vmfent));
			}
			memset(&n->ent[j], 0, sizeof(struct vmfent));
			memcpy(n->ent[j].key, e->key, KEYLEN);
			n->ent[j].data = (unsigned char *)xrealloc(NULL, len + 1);
			memcpy(n->ent[j].data, d, len);
			n->ent[j].len = len;
			n->nent++;
		}
	}
	if (vmf_write(n, name, 1, nb, nk) < 0) {
		vmf_close(n);
		ui_pause(text(1, 22));
		return;
	}
	if (!all) {
		vmf_close(n);
		if ((n = vmf_open(name)) != NULL) {
			copykeys(sysvmf, n);
			vmf_close(n);
		}
	} else
		vmf_close(n);
	t_clearrow(t_rows - 2);
	if (ui_yesno(t_rows - 2, 0, text(1, 35)) == 1) {
		char bak[300], *dot;

		strcpy(bak, vmfname);
		dot = strrchr(bak, '.');
		if (dot == NULL || strchr(dot, '/'))
			dot = bak + strlen(bak);
		strcpy(dot, I.vmfbak[0] ? I.vmfbak : ".bak");
		for (; *dot; dot++)
			*dot = (char)lowcase((unsigned char)*dot);
		remove(bak);
		if (rename(vmfname, bak) != 0 || rename(name, vmfname) != 0) {
			ui_pause(text(1, 39));
			strcpy(name, vmfname);
		}
		vmf_close(sysvmf);
		sysvmf = vmf_open(vmfname);
		sys_load();
	}
}

/* /6: a document through the conversion table *CONV */
static void
convert(void)
{
	char from[MAXNAME + 1], to[MAXNAME + 1], pf[300], pt[300];
	FILE *f, *g;
	int c;

	dialog_start();
	t_puts(10, 12, text(0, 7), A_NORM);
	strcpy(from, lastdoc);
	if (ask_name(from, 1, 11) < 0)
		return;
	t_clearrow(10);
	t_puts(10, 12, text(0, 8), A_NORM);
	strcpy(to, from);
	if (ask_name(to, 0, 11) < 0)
		return;
	docname_file(pf, from);
	docname_file(pt, to);
	if ((f = fopen(pf, "rb")) == NULL)
		return;
	if ((g = fopen(pt, "wb")) == NULL) {
		fclose(f);
		return;
	}
	while ((c = getc(f)) != EOF)
		putc(c == '\n' ? c : convchar(c), g);
	fclose(f);
	fclose(g);
}

/* /0: the locks of a system file cleared */
static void
clearlocks(void)
{
	char name[200];
	struct vmf *v;
	int i;

	strcpy(name, vmfname);
	t_clear();
	if (askline(text(1, 36), name, 60) <= 0)
		return;
	withext(name, ".vmf");
	t_puts(t_rows - 1, 0, text(1, 38), A_NORM);
	t_refresh();
	if (strcmp(name, vmfname) == 0) {
		for (i = 0; i < sysvmf->nent; i++)
			sysvmf->ent[i].lock = 0;
		vmf_save(sysvmf);
		return;
	}
	if ((v = vmf_open(name)) != NULL) {
		for (i = 0; i < v->nent; i++)
			v->ent[i].lock = 0;
		vmf_save(v);
		vmf_close(v);
	}
}

/* a program run from LEX, the screen given back after */
int
fork_prog(const char *cmd, int wait)
{
	int r;

	t_suspend();
	r = system(cmd);
	if (wait) {
		printf("\r\n%s", text(0, 26));
		fflush(stdout);
		t_resume();
		t_getkey();
	} else
		t_resume();
	t_redraw();
	return r;
}

/* /B /D /H /N: the program in the first 32 characters of the record */
static int
do_fork(int opt, const char *rec)
{
	char *t, prog[40], in[MAXNAME + 1], out[MAXNAME + 1], cmd[800];
	long len;

	if (rec_text(rec, &t, &len, '_') < 0) {
		error("NK");
		return -1;
	}
	/* the program in characters 1-32, the rest for the program
	   ($LEXREC), with keystore 0 ($LEXKS0) and the terminal's code
	   ($LEXCS): LEX gave them to its programs in memory */
	{
		static char envrec[400], envks[KSLEN + 10], envcs[20], envnat[12];
		int k = 0, i;

		for (i = 1; i < 33 && i < len; i++)
			if (t[i] != '_' && t[i] != '\n')
				prog[k++] = t[i];
			else if (k > 0 && t[i] == '_')
				break;
		prog[k] = 0;
		while (k > 0 && prog[k - 1] == ' ')
			prog[--k] = 0;
		sprintf(envrec, "LEXREC=%.300s", len > 33 ? t + 33 : "");
		for (i = 7; envrec[i]; i++)
			if (envrec[i] == '\n')
				envrec[i] = ' ';
		sprintf(envks, "LEXKS0=%s", keystore[0]);
		sprintf(envcs, "LEXCS=%s", t_charset == CS_SWE7 ? "swe7" : t_charset == CS_UTF8 ? "utf8" : "latin1");
		putenv(envrec);
		putenv(envks);
		putenv(envcs);
		/* whether the documents are in 7-bit Swedish (lexconv) */
		sprintf(envnat, "LEXNAT=%d", t_national);
		putenv(envnat);
	}
	xfree(t);
	in[0] = out[0] = 0;
	dialog_start();
	if (opt == 'D' || opt == 'H') {
		t_puts(10, 12, text(0, 7), A_NORM);
		strcpy(in, lastdoc);
		if (ask_name(in, -1, 11) < 0)
			return -1;
	}
	if (opt == 'B' || opt == 'H') {
		t_clearrow(10);
		t_puts(10, 12, text(0, 8), A_NORM);
		if (ask_name(out, -1, 11) < 0)
			return -1;
	}
	{
		char pi[300], po[300];

		docname_file(pi, in);
		docname_file(po, out);
		sprintf(cmd, "%s %s %s", prog, pi[0] ? pi : "\"\"", po[0] ? po : "\"\"");
	}
	fork_prog(cmd, 1);
	return 0;
}

/* /F: another system file */
static int
switch_vmf(const char *arg)
{
	char name[260];
	struct vmf *v;

	strncpy(name, arg ? arg : "", 250);
	name[250] = 0;
	if (!name[0]) {
		t_clear();
		if (askline("VMF? ", name, 60) <= 0)
			return -1;
	}
	withext(name, ".vmf");
	if ((v = vmf_open(name)) == NULL) {
		ui_pause(text(0, 16));
		return -1;
	}
	vmf_close(sysvmf);
	sysvmf = v;
	strcpy(vmfname, name);
	sys_load();
	vdu_load(vdukey[0] ? vdukey : I.vdu);
	menudepth = 0;
	return 0;
}

/* /2: the work file started again */
void
work_init(void)
{
	int i;

	foreign_unset();
	shutup = shush = 0;
	cut_clearall();
	calc_clearall();
	for (i = 0; i < NKS; i++)
		keystore[i][0] = 0;
	for (i = 6; i <= 9; i++)
		ED.rulers[i][0] = 0;
	lastdoc[0] = 0;
	esc_clearfuncs();
	menudepth = 0;
}

/*
 * A menu switch option.  arg: what was typed after it (a key, a
 * name), rec: the record from the menu line.  Returns 1 if LEX ends.
 */
int
switch_option(int opt, const char *arg, const char *rec)
{
	char key[40];

	opt = upcase(opt);
	ui_drawfn = NULL;
	sprintf(curopt, "%c%.10s", opt, rec ? rec : "");
	switch (opt) {
	case 'C':
	case 'E':
	case 'I':
	case 'V':
		do_edit(opt == 'I' ? 'E' : opt, rec);
		break;
	case 'K':
		do_delete();
		break;
	case 'R':
		do_rename();
		break;
	case 'A':
		do_copy(1, rec, 0);
		break;
	case 'J':
		do_copy(0, NULL, 1);
		break;
	case 'P':
		do_print(0);
		break;
	case 'Q':
		do_print(1);
		break;
	case 'X':
		ED.ext[0] = 0;
		edit_run(ED_PLAY, NULL, NULL, rec);
		break;
	case 'M':
		if (rec && rec[0])
			strcpy(key, rec);
		else if (arg && arg[0])
			sprintf(key, "*%.9s", arg);
		else if (askkey(key, '*') < 0)
			break;
		menu_goto(key);
		break;
	case 'U':
		if (rec && rec[0])
			strcpy(key, rec);
		else if (arg && arg[0])
			sprintf(key, ")%.9s", arg);
		else if (askkey(key, ')') < 0)
			break;
		if (vdu_load(key + (key[0] == ')')) < 0)
			ui_pause(text(0, 20));
		menudepth = 0;
		break;
	case 'S':
	case 'L':
		if (rec && rec[0])
			strcpy(key, rec);
		else if (arg && arg[0])
			sprintf(key, "*%.9s", arg);
		else if (askkey(key, '*') < 0)
			break;
		if (opt == 'L') {
			char name[MAXNAME + 1];

			dialog_start();
			strcpy(name, lastdoc);
			if (ask_name(name, 1, 11) < 0)
				break;
			listproc(key, 1, name);
		} else
			listproc(key, 0, NULL);
		break;
	case 'F':
		switch_vmf(rec && rec[0] ? rec : arg);
		break;
	case 'Z':
		{
			char name[200];

			strcpy(name, arg ? arg : "");
			if (!name[0]) {
				t_clear();
				if (askline(text(0, 10), name, 60) <= 0)
					break;
			}
			work_save();
			withext(name, ".wrk");
			strcpy(workname, name);
			work_load();
			menudepth = 0;
		}
		break;
	case '1':
		if (arg && arg[0])
			strcpy(key, arg);
		else if (rec && rec[0])
			strcpy(key, rec);
		else if (askkey(key, 0) < 0)
			break;
		if (sys_switch(key) < 0)
			ui_pause(text(0, 20));
		break;
	case '2':
		work_init();
		break;
	case 'T':
		vmf_statistics();
		break;
	case 'W':
		lex_status();
		break;
	case '0':
		clearlocks();
		break;
	case '3':
		reorganise();
		break;
	case '4':
		copy_vmf();
		break;
	case '5':
		{
			char b[100];

			sprintf(b, "%s- nylex patchar inte sig sj{lv", text(1, 23));
			t_clear();
			ui_pause(b);
		}
		break;
	case '6':
		convert();
		break;
	case 'G':
		{
			char name[200];

			strcpy(name, rec && rec[0] ? rec : arg ? arg : "");
			if (!name[0]) {
				t_clear();
				if (askline(text(0, 24), name, 60) <= 0)
					break;
			}
			work_save();
			t_clear();
			t_refresh();
			t_close();
			execl("/bin/sh", "sh", "-c", name, (char *)NULL);
			exit(1);
		}
		break;
	case 'B':
	case 'D':
	case 'H':
	case 'N':
		if (rec && rec[0])
			strcpy(key, rec);
		else if (arg && arg[0])
			sprintf(key, "%s%.9s", arg[0] == '*' ? "" : "*", arg);
		else if (askkey(key, 0) < 0)
			break;
		do_fork(opt, key);
		break;
	case '.':
	case ' ':
		break;
	default:
		bell();
		return 0;
	}
	if (!shutup)
		shush = 0;
	return quitting;
}

/* Menus */

static int
loadmenu(const char *key, struct menu *m)
{
	char *t, *p, *e;
	long len;

	if (rec_text(key, &t, &len, '_') < 0)
		return -1;
	m->n = 0;
	p = t;
	while (*p && m->n < MAXITEMS) {
		e = strchr(p, '\n');
		if (e)
			*e = 0;
		strncpy(m->line[m->n], p, 127);
		m->line[m->n][127] = 0;
		m->n++;
		if (!e)
			break;
		p = e + 1;
	}
	if (m->n == 0) {
		m->line[0][0] = 0;
		m->n = 1;
	}
	xfree(t);
	strncpy(m->key, key, KEYLEN);
	m->key[KEYLEN] = 0;
	return 0;
}

void
menu_goto(const char *key)
{
	char k[KEYLEN + 1];
	int i;

	mkkey(k, key);
	k[KEYLEN] = 0;
	for (i = KEYLEN - 1; i >= 0 && k[i] == ' '; i--)
		k[i] = 0;
	if (strcmp(k, "**MENU") == 0 || strcmp(k, vdu_firstmenu) == 0)
		menudepth = 0;
	if (menudepth >= MAXMENU)
		menudepth = MAXMENU - 1;
	strcpy(menustack[menudepth++], k);
}

/* the highlights of a line: "/Smno" at its end, taken off */
static void
highlights(char *s, int *m, int *n, int *o)
{
	int len = (int)strlen(s), i;

	*m = *n = *o = -1;
	while (len > 0 && s[len - 1] == ' ')
		len--;
	for (i = len - 5; i >= 0; i--)
		if (s[i] == '/' && upcase((unsigned char)s[i + 1]) == 'S' && i + 5 <= len &&
		    s[i + 2] >= '0' && s[i + 2] <= '9' && s[i + 3] >= '0' && s[i + 3] <= '9' &&
		    s[i + 4] >= '0' && s[i + 4] <= '9') {
			*m = s[i + 2] - '0';
			*n = s[i + 3] - '0';
			*o = s[i + 4] - '0';
			s[i] = 0;
			return;
		}
}

static int	menu_dotrow, menu_dotcol;

static void
drawmenu(void)
{
	int i, w = 0, indent, top, m, n, o, a1, a2;
	char title[128];

	if (shutup)
		return;
	for (i = 1; i < cur.n; i++) {
		char s[128];
		int l;

		strcpy(s, cur.line[i]);
		highlights(s, &m, &n, &o);
		l = (int)strlen(s) - 11;
		while (l > 0 && s[10 + l] == ' ')
			l--;
		if (l > 0 && s[11] != '.' && l > w)
			w = l;
	}
	indent = (t_cols - w) / 2 - 1;
	if (strcmp(cur.line[0], ".") == 0 && indent > 20)
		indent = 20;
	if (indent < 0)
		indent = 0;
	top = (t_rows - 4 - (cur.n - 1)) / 2;
	if (top < 0)
		top = 0;
	t_clear();
	strcpy(title, cur.line[0]);
	highlights(title, &m, &n, &o);
	{
		int len = (int)strlen(title);

		while (len > 0 && title[len - 1] == ' ')
			title[--len] = 0;
	}
	if (strcmp(title, ".") == 0 || title[0] == 0)
		strcpy(title, progtitle);
	a1 = vdu_bgattr;
	if (m >= 0)
		vdu_menuattr(m, &a1);
	ui_center(top, title, a1);
	menu_dotrow = -1;
	for (i = 1; i < cur.n; i++) {
		char s[128];
		int r = top + 1 + i;

		strcpy(s, cur.line[i]);
		highlights(s, &m, &n, &o);
		if ((int)strlen(s) <= 11)
			continue;
		a1 = vdu_bgattr;
		a2 = A_NORM;
		if (m >= 0)
			vdu_menuattr(m, &a1);
		if (n >= 0)
			vdu_menuattr(n, &a2);
		if (r < t_rows) {
			t_puts(r, indent + 1, s + 12, a2);
			t_put(r, indent, (unsigned char)s[11], s[11] == '.' ? A_NORM : a1);
		}
		if (s[11] == '.') {
			menu_dotrow = r;
			menu_dotcol = indent;
		}
	}
	if (menu_dotrow >= 0 && menu_dotrow < t_rows)
		t_move(menu_dotrow, menu_dotcol);
	else
		t_move(t_rows - 1, 0);
	{
		long used, fr, keys, index, ovf, total;

		if (sysvmf) {
			vmf_stats(sysvmf, &used, &fr, &keys, &index, &ovf, &total);
			if (fr < I.blockwarn && I.blockwarn > 0) {
				char b[100];

				sprintf(b, "%s %ld", text(0, 11), fr);
				t_puts(t_rows - 1, 0, b, A_NORM);
			}
		}
	}
}

/* "/x" typed at a menu: the option and what goes with it */
static int
typed_option(void)
{
	int k = in_key(), opt, n = 0;
	char arg[64];

	if (k == K_NONE)
		return 0;
	opt = upcase(KC(k));
	if (strchr("MSLU1FZGBDHN", opt) && opt) {
		/* a key or a name follows, ended by Return or a blank */
		for (;;) {
			k = in_key();
			if (k == K_NONE || k == CR || (KC(k) == ' ' && n > 0 && opt != 'G'))
				break;
			if (k == ESC) {
				in_key();
				return 0;
			}
			if (n < 63)
				arg[n++] = (char)KC(k);
		}
	}
	arg[n] = 0;
	return switch_option(opt, n ? arg : NULL, NULL);
}

void
menu_loop(void)
{
	int k, i;

	ui_drawfn = drawmenu;
	if (menudepth == 0) {
		if (vdu_firstmenu[0])
			menu_goto(vdu_firstmenu);
		else
			menu_goto("**MENU");
	}
	while (!quitting) {
		if (menudepth == 0)
			menu_goto(vdu_firstmenu[0] ? vdu_firstmenu : "**MENU");
		ED.protect = 0;
		shush = 0;
		mask_unset();
		ED.sqch = '_';
		if (loadmenu(menustack[menudepth - 1], &cur) < 0) {
			if (strcmp(menustack[menudepth - 1], "**MENU") != 0) {
				ui_pause(text(0, 20));
				menudepth = 0;
				menu_goto("**MENU");
				continue;
			}
			/* a pseudo menu: only switch options */
			cur.n = 1;
			cur.line[0][0] = 0;
		}
		ui_drawfn = drawmenu;
		k = in_key();
		if (k == K_NONE) {
			quitting = 1;
			break;
		}
		if (k == ESC) {
			k = upcase(KC(in_key()));
			if (k == 'E') {
				if (--menudepth <= 0) {
					quitting = 1;
					break;
				}
			}
			continue;
		}
		/* Ctrl U leaves a menu (LEX's **MONT); the up arrow, which nylex
		   makes Ctrl U for the editor, does not */
		if (vdu_special && k == CTL('U')) {
			bell();
			continue;
		}
		if ((k == M.menuexit && !(k & K_LIT)) || k == CTL('C') || k == CTL('U')) {
			if (--menudepth <= 0) {
				quitting = 1;
				break;
			}
			continue;
		}
		if (KC(k) == '/') {
			if (typed_option())
				break;
			continue;
		}
		k = upcase(KC(k));
		for (i = 1; i < cur.n; i++) {
			int c = upcase((unsigned char)cur.line[i][11]);

			if ((int)strlen(cur.line[i]) > 11 && c != '.' && c != ' ' && c == k)
				break;
		}
		if (i >= cur.n) {
			bell();
			continue;
		}
		{
			char rname[KEYLEN + 1], s[128];
			int j, m, n, o;

			memcpy(rname, cur.line[i] + 1, KEYLEN);
			rname[KEYLEN] = 0;
			for (j = KEYLEN - 1; j >= 0 && rname[j] == ' '; j--)
				rname[j] = 0;
			strcpy(s, cur.line[i]);
			highlights(s, &m, &n, &o);
			strncpy(heading, s + 11, sizeof heading - 1);
			heading_col = menu_dotcol;
			if (switch_option(cur.line[i][0], NULL, rname))
				break;
		}
	}
	ui_drawfn = NULL;
}
