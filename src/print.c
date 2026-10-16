/*
 * print.c - printing (manual chapter 9 and Appendix F): the printer
 * chosen from **PNST, its escape sequences (*$ records, *% for
 * combinations of attributes), the petal table (*#) and its functions
 * (*(), the document printing details of the extension (*.EXT), and
 * the print time instructions in the document: .C .F .G .H .I .L .M
 * .N .P .PP .S .T .X.
 *
 * In a title or footer the page number is written as the field PAGE
 * of list processing in visible equivalents, (SIDA: in the Swedish
 * **MONT and **TEXTP; DATE and TIME the same way.
 *
 * A printer whose type is PDF, PDFSANS or PDFMONO is not sent bytes: the
 * characters go to pdf.c with their columns and attributes, and the
 * file (or command) gets a PDF document.
 */
#if !defined(USE_TERMIO) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L	/* popen, pclose in strict C89 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_ISREG
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#endif
#include "lex.h"

#define SEQ	72

/* the printer as the records describe it */
static struct {
	char	set[SEQ], reset[SEQ], ff[SEQ], ff2[SEQ], half[SEQ], quarter[SEQ];
	char	fnon[SEQ], fnoff[SEQ], nolf[SEQ], esc[SEQ], pound[SEQ], ul[SEQ];
	char	sp1[SEQ], sp2[SEQ], alloff[SEQ], aon[6][SEQ], aoff[6][SEQ];
	int	combos;
	char	calloff[SEQ], con[16][SEQ], coff[16][SEQ];
	short	petal[256];		/* the character to send, or -('A'..) */
	char	petfun[52][SEQ];
	int	havepetal;
} R;

/* where it goes */
static FILE	*pf;
static int	pispipe, pvdu, ppause, plastff;
static int	ppdf;			/* the PDF family: 'T', 'H', 'C'; 0 */
static int	pps_out;		/* ... in PostScript */
static int	psup;			/* PDF: a footnote number, raised */
static int	pps;			/* a proportional daisy wheel (";") */
static char	*pstab;			/* its record */
static long	pslen;
static struct pdfhint	phint;
int		print_quiet;		/* list processing on the screen */

static void
out(const char *s, int n)
{
	if (ppdf)
		return;
	if (pvdu)
		t_rawout(s, n);
	else if (pf)
		fwrite(s, 1, n, pf);
}

static void
outs(const char *s)
{
	out(s, (int)strlen(s));
}

static void
outc(int c)
{
	char b = (char)c;

	out(&b, 1);
}

/* The records */

/* the visible equivalents of a printer record: 20 of char + 3 digits */
static int	vis[20][2], nvis;

static void
loadvis(const char *t, long len)
{
	int i;

	nvis = 0;
	for (i = 0; i < 20; i++) {
		int o = 1 + 4 * i, c, v;

		if (o + 3 >= len)
			break;
		c = (unsigned char)t[o];
		if (c == '_' || c == ' ' || t[o + 1] < '0' || t[o + 1] > '9')
			continue;
		v = (t[o + 1] - '0') * 100 + (t[o + 2] - '0') * 10 + (t[o + 3] - '0');
		vis[nvis][0] = c;
		vis[nvis][1] = v & 0xff;
		nvis++;
	}
}

/* a field of a record as the bytes to send: visible equivalents and
   decimal triplets "(155,085,013)" are both understood */
static void
seqfield(char *dst, const char *t, long len, int off, int n)
{
	int i, m = 0, j;

	for (i = 0; i < n && off + i < len && m < SEQ - 1; i++) {
		int c = (unsigned char)t[off + i];

		if (c == '_' || c == '\n')
			break;
		for (j = 0; j < nvis; j++)
			if (vis[j][0] == c)
				break;
		if (j < nvis) {
			dst[m++] = (char)vis[j][1];
			continue;
		}
		if (c == ' ')
			continue;
		dst[m++] = (char)c;
	}
	dst[m] = 0;
}

static void
defseq(char *dst, const char *bytes)
{
	strcpy(dst, bytes);
}

static void	petal_load(const char *pet);

static void
load_printer(const struct printer *p)
{
	char key[12], *t;
	long len;
	int i;

	memset(&R, 0, sizeof R);
	for (i = 0; i < 256; i++)
		R.petal[i] = (short)i;
	ppdf = 0;
	pps = 0;
	pps_out = 0;
	if (strncmp(p->type, "PDF", 3) == 0) {
		ppdf = strncmp(p->type, "PDFSANS", 7) == 0 ? 'H' : strncmp(p->type, "PDFMONO", 7) == 0 ? 'C' : 'T';
		return;
	}
	if (strcmp(p->type, "PS") == 0 || strcmp(p->type, "PSSANS") == 0 || strcmp(p->type, "PSMONO") == 0) {
		ppdf = p->type[2] == 'S' ? 'H' : p->type[2] == 'M' ? 'C' : 'T';
		pps_out = 1;
		return;
	}
	/* the defaults of the form, when no record is named */
	defseq(R.ff, "\f");
	defseq(R.ff2, "\f");
	defseq(R.half, "\233U\r");
	defseq(R.quarter, "\233U\r");
	defseq(R.nolf, "\r");
	defseq(R.esc, "\233");
	defseq(R.pound, "\233 ");
	defseq(R.ul, "\233\005");
	if (p->type[0]) {
		sprintf(key, "*$%.8s", p->type);
		if (rec_text(key, &t, &len, '_') == 0) {
			memset(&R, 0, sizeof R);
			for (i = 0; i < 256; i++)
				R.petal[i] = (short)i;
			loadvis(t, len);
			seqfield(R.set, t, len, 81, 64);
			seqfield(R.reset, t, len, 145, 64);
			seqfield(R.ff, t, len, 209, 32);
			seqfield(R.ff2, t, len, 241, 32);
			seqfield(R.half, t, len, 273, 32);
			seqfield(R.quarter, t, len, 305, 32);
			seqfield(R.fnon, t, len, 337, 16);
			seqfield(R.fnoff, t, len, 353, 16);
			seqfield(R.nolf, t, len, 369, 16);
			seqfield(R.esc, t, len, 401, 16);
			seqfield(R.pound, t, len, 433, 16);
			seqfield(R.ul, t, len, 449, 16);
			seqfield(R.sp1, t, len, 465, 16);
			seqfield(R.sp2, t, len, 481, 16);
			seqfield(R.alloff, t, len, 497, 32);
			for (i = 1; i <= 5; i++) {
				seqfield(R.aon[i], t, len, 529 + 32 * (i - 1), 16);
				seqfield(R.aoff[i], t, len, 545 + 32 * (i - 1), 16);
			}
			if (!R.ff[0])
				strcpy(R.ff, "\f");
			if (!R.ff2[0])
				strcpy(R.ff2, R.ff);
			if (!R.nolf[0])
				strcpy(R.nolf, "\r");
			xfree(t);
		}
		sprintf(key, "*%%%.8s", p->type);
		if (rec_text(key, &t, &len, '_') == 0) {
			loadvis(t, len);
			R.combos = 1;
			seqfield(R.calloff, t, len, 81, 32);
			seqfield(R.con[0], t, len, 113, 16);	/* attribute 1 */
			seqfield(R.coff[0], t, len, 129, 16);
			for (i = 1; i < 16; i++) {
				seqfield(R.con[i], t, len, 145 + 32 * (i - 1), 16);
				seqfield(R.coff[i], t, len, 161 + 32 * (i - 1), 16);
			}
			xfree(t);
		}
	}
	/* a proportional wheel: the record ";NAME", its petal table named at
	   its character 191 */
	pps = 0;
	xfree(pstab);
	pstab = NULL;
	if (p->petal[0]) {
		char pet[10];
		int k;

		strncpy(pet, p->petal, 9);
		pet[9] = 0;
		sprintf(key, ";%.8s", p->petal);
		if (rec_text(key, &pstab, &pslen, '_') == 0) {
			pps = 1;
			for (k = 0; k < 9 && 191 + k < pslen && pstab[191 + k] != '_' && pstab[191 + k] != '\n'; k++)
				pet[k] = pstab[191 + k];
			pet[k] = 0;
		}
		petal_load(pet);
	}
}

/* the petal table *#NAME and its functions */
static void
petal_load(const char *pet)
{
	char key[12], *t;
	long len;
	int i;

	if (pet[0]) {
		char fn[12];
		int k_;

		sprintf(key, "*#%.8s", pet);
		fn[0] = 0;
		if (rec_text(key, &t, &len, '_') == 0) {
			R.havepetal = 1;
			for (i = 0; i < 256; i++) {
				char e[4];
				int o = 1 + 3 * i, k, n = 0;

				for (k = 0; k < 3; k++) {
					e[k] = o + k < len ? t[o + k] : '_';
					if (e[k] != '_' && e[k] != '\n')
						n++;
				}
				e[3] = 0;
				if (n == 0)
					continue;
				if ((e[2] >= 'A' && e[2] <= 'Z') || (e[2] >= 'a' && e[2] <= 'z'))
					R.petal[i] = (short)-e[2];
				else if (e[0] >= '0' && e[0] <= '9' && e[1] >= '0' && e[1] <= '9' && e[2] >= '0' && e[2] <= '9')
					R.petal[i] = (short)(atoi(e) & 0xff);
				else
					R.petal[i] = (short)(unsigned char)(e[0] != '_' ? e[0] : e[1]);
			}
			for (k_ = 0, i = 769; i < 777 && i < len && t[i] != '_' && t[i] != '\n'; i++)
				fn[k_++] = t[i];
			fn[k_] = 0;
			xfree(t);
		}
		if (fn[0]) {
			sprintf(key, "*(%.8s", fn);
			if (rec_text(key, &t, &len, '_') == 0) {
				loadvis(t, len);
				for (i = 0; i < 52; i++) {
					int o = i < 26 ? 81 + 4 * i : 185 + 4 * (i - 26), m = 0, c, j;

					while (o < len && m < SEQ - 1 && (c = (unsigned char)t[o]) != '_' && c != '\n') {
						for (j = 0; j < nvis; j++)
							if (vis[j][0] == c)
								break;
						R.petfun[i][m++] = (char)(j < nvis ? vis[j][1] : c);
						o++;
					}
					R.petfun[i][m] = 0;
				}
				xfree(t);
			}
		}
	}
}

/* a character of text to the printer, through the petal table */
static void
pchar(int c)
{
	int v = R.petal[c & 0xff];

	if (v < 0) {
		int f = -v, i = f >= 'a' ? 26 + f - 'a' : f - 'A';

		if (i >= 0 && i < 52)
			outs(R.petfun[i]);
		return;
	}
	outc(v);
}

/* the printer's sequences from attributes old to a */
static void
attrseq(int old, int a)
{
	int i;

	if (R.combos) {
		int oc = old >> 1, nc = a >> 1;

		if ((old & 1) && !(a & 1))
			outs(R.coff[0]);
		if (oc != nc && oc)
			outs(R.coff[oc]);
		if (!a && R.calloff[0])
			outs(R.calloff);
		if ((a & 1) && !(old & 1))
			outs(R.con[0]);
		if (oc != nc && nc)
			outs(R.con[nc]);
	} else {
		for (i = 1; i <= 5; i++)
			if ((old & (1 << (i - 1))) && !(a & (1 << (i - 1))))
				outs(R.aoff[i]);
		if (!a && R.alloff[0])
			outs(R.alloff);
		for (i = 1; i <= 5; i++)
			if (!(old & (1 << (i - 1))) && (a & (1 << (i - 1))))
				outs(R.aon[i]);
	}
}

/* a proportional wheel's line: bytes, attributes, characters */
static int	psattr;

static void
ps_bytes(const char *b, int n)
{
	out(b, n);
}

static void
ps_attr(int a)
{
	a &= 0x1f;
	if (a != psattr)
		attrseq(psattr, a);
	psattr = a;
}

static void
ps_char(int c)
{
	pchar(c);
}

/* a character of text: to the printer, or to its column of the PDF */
static int	S_attr(void);

static void
tchar(int c)
{
	if (ppdf || pps)
		pdf_put(pdf_col(), c, S_attr(), psup);
	else
		pchar(c);
}

/* The printer chosen */

int
ask_printer(int *pr)
{
	char b[160], *nl;
	int row = t_rows - 3, k;

	for (;;) {
		t_clearrow(row);
		t_clearrow(row + 1);
		strcpy(b, P.prompt);
		nl = strchr(b, '\n');
		if (nl) {
			*nl = 0;
			t_puts(row + 1, 0, nl + 1, A_NORM);
		}
		t_puts(row, 0, b, A_NORM);
		t_move(nl ? row + 1 : row, (int)strlen(nl ? nl + 1 : b) + 1);
		k = in_key();
		if (k == K_NONE || k == ESC || k == CTL('C') || ((k == CTL('U') || (k == M.menuexit && !(k & K_LIT))) && !vdu_special))
			return -1;
		k = KC(k);
		if (k >= '1' && k <= '9')
			k = k - '1';
		else if (k == '0')
			k = 9;
		else {
			bell();
			continue;
		}
		if (!P.p[k].device[0] && !P.p[k].spool && !P.p[k].vdu) {
			bell();
			continue;
		}
		*pr = k;
		return 0;
	}
}

static int
iscommand(const char *dev)
{
	char w[64];
	struct stat st;
	int i;

	if (dev[0] == '|')
		return 1;
	for (i = 0; dev[i] && dev[i] != ' ' && i < 63; i++)
		w[i] = dev[i];
	w[i] = 0;
	if (strncmp(w, "/dev/", 5) == 0)
		return 0;
	if (stat(w, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111))
		return 1;
	return dev[i] == ' ';
}

int
print_open(int *pr, FILE **fp, int *ispipe)
{
	const struct printer *p;
	char dev[200];
	const char *env = getenv("LEXPRINT");

	for (;;) {
		if (ask_printer(pr) < 0)
			return -1;
		p = &P.p[*pr];
		*ispipe = 0;
		*fp = NULL;
		pvdu = 0;
		if (env && *env) {
			t_suspend();
			*fp = popen(env, "w");
			t_resume();
			*ispipe = 1;
		} else if (p->vdu && !p->spool) {
			pvdu = 1;
			*fp = NULL;
			ppause = p->pause;
			plastff = p->lastff;
			load_printer(p);
			{
				const char *s = vdu_pport(1);

				if (s)
					t_rawout(s, (int)strlen(s));
			}
			pf = NULL;
			return 0;
		} else if (p->spool) {
			char name[80];
			int term;

			name[0] = 0;
			t_clearrow(t_rows - 1);
			if (ui_line(t_rows - 1, text(2, 24), name, 60, 60, &term) < 0 || !name[0])
				continue;
			sprintf(dev, "%s%s", P.spooldev, name);
			if (iscommand(dev)) {
				*fp = popen(dev + (dev[0] == '|'), "w");
				*ispipe = 1;
			} else
				*fp = fopen(dev, "w");
		} else if (iscommand(p->device)) {
			*fp = popen(p->device + (p->device[0] == '|'), "w");
			*ispipe = 1;
		} else
			*fp = fopen(p->device, "w");
		if (*fp == NULL) {
			t_clearrow(t_rows - 1);
			t_puts(t_rows - 1, 0, text(2, 23), A_NORM);
			bell();
			continue;
		}
		pf = *fp;
		pispipe = *ispipe;
		ppause = p->pause;
		plastff = p->lastff;
		load_printer(p);
		if (ppdf)
			pdf_begin(pf, ppdf, pps_out);
		if (pps) {
			psattr = 0;
			daisy_begin(pstab, pslen, ps_bytes, ps_attr, ps_char);
		}
		return 0;
	}
}

void
print_close(int pr, FILE *f, int ispipe)
{
	(void)pr;
	if (ppdf) {
		pdf_finish();
		ppdf = 0;
	}
	if (pps) {
		daisy_end();
		pps = 0;
	}
	if (pvdu) {
		const char *s = vdu_pport(0);

		if (s)
			t_rawout(s, (int)strlen(s));
		pvdu = 0;
		t_redraw();
		return;
	}
	if (f) {
		if (ispipe) {
			fflush(f);
			pclose(f);
		} else
			fclose(f);
	}
	pf = NULL;
}

/* The document */

#define MAXT	8		/* lines of titles, of footers */

struct pstate {
	char	pcc[13];
	int	sp;		/* line spacing in quarters */
	int	page;		/* the number on this page */
	int	q;		/* quarters printed of the body */
	int	started;	/* a page is under way */
	int	from, to, oddeven;
	int	printing;	/* this page goes to the printer */
	char	*title[MAXT], *foot[MAXT];
	int	ntitle, nfoot;
	char	*fnote[16];
	int	nfnote, fnum;
	int	para[8];
	int	gwant;		/* .G: lines to keep together */
	int	attr;
	FILE	*ndx;
	int	stop;
	int	physical;	/* something has been printed */
	int	lm;
};

static struct pstate	S;
static void	print_lines(struct doc *d, int depth);

static int
S_attr(void)
{
	return S.attr;
}

/* an empty line: down a line */
static void
blankline(void)
{
	if (ppdf)
		pdf_newline(4, NULL);
	else
		outs("\r\n");
}

static int
isctl(const struct line *l)
{
	return l->len >= 2 && l->s[0] == '.' && l->s[1] >= 'A' && l->s[1] <= 'Z';
}

static void
setpcc(const unsigned char *s, int n)
{
	int i, k = 0;

	if (n > 0 && s[0] == ' ')
		s++, n--;
	if (n <= 0 || (n == 1 && s[0] == '.')) {
		if (n == 1) {
			memset(S.pcc, '.', 12);
			S.pcc[12] = 0;
			return;
		}
	}
	for (i = 0; i < n && s[i] == ' '; i++)
		;
	if (i == n) {
		strncpy(S.pcc, I.pcc, 12);
		S.pcc[12] = 0;
		for (k = (int)strlen(S.pcc); k < 12; k++)
			S.pcc[k] = '.';
		S.pcc[12] = 0;
		return;
	}
	for (i = 0; i < 12; i++)
		S.pcc[i] = (char)(i < n && s[i] != ' ' ? s[i] : '.');
	S.pcc[12] = 0;
}

static int
pccis(int c, int which)
{
	return S.pcc[which] != '.' && (unsigned char)S.pcc[which] == c;
}

/* the attributes of the printer for a cell (for a PDF or a proportional
   wheel they go with the characters, the line laid out) */
static void
setattr(int a)
{
	a &= 0x1f;
	if (a == S.attr)
		return;
	if (!ppdf && !pps)
		attrseq(S.attr, a);
	S.attr = a;
}


/* "(SIDA:" and the others in a title or footer */
static int
special(const char *s, int n, char *out)
{
	char name[16];
	int i;

	if (n < 3 || (unsigned char)s[0] != M.vstart)
		return 0;
	for (i = 1; i < n && i < 14 && (unsigned char)s[i] != M.vend; i++)
		name[i - 1] = s[i];
	if (i >= n || (unsigned char)s[i] != M.vend)
		return 0;
	name[i - 1] = 0;
	{
		const char *ops = text(2, 1);
		int k, L = (int)strlen(ops);

		for (k = 0; k + 10 <= L; k += 10) {
			char nm[9];
			int j;

			memcpy(nm, ops + k, 8);
			nm[8] = 0;
			for (j = 7; j >= 0 && nm[j] == ' '; j--)
				nm[j] = 0;
			if (strcmp(nm, name) != 0)
				continue;
			switch (ops[k + 8]) {
			case 'd':
				sprintf(out, "%d", S.page);
				return i + 1;
			case 'D':
				date_full(out);
				return i + 1;
			case 't':
				time_hms(out, M.timesep);
				return i + 1;
			}
		}
	}
	return 0;
}

static void
newline(void)
{
	int q = S.sp > 0 ? S.sp : 4;

	if (ppdf) {
		pdf_newline(q, &phint);
		return;
	}
	if (pps)
		daisy_flush(&phint);
	outs("\r\n");
	q -= 4;
	while (q >= 4) {
		outs("\n");
		q -= 4;
	}
	if (q >= 2) {
		outs(R.half);
		q -= 2;
	}
	if (q >= 1)
		outs(R.quarter);
}

/*
 * A line of text to the printer: the left margin, then the characters
 * with their attributes and the print time characters of .C.
 */
static void
pline(const unsigned char *s, const unsigned char *a, int n, int nolf)
{
	int i, inpara = 0, inidx = 0, lm = S.lm;
	char num[40], idx[256];
	int nn = 0, ni = 0;

	while (n > 0 && s[n - 1] == ' ' && (!a || !a[n - 1]))
		n--;
	if (n > 0 && pccis(s[n - 1], 1)) {
		nolf = 1;
		n--;
	}
	if (S.printing)
		for (i = 0; i < lm; i++)
			tchar(' ');
	for (i = 0; i < n; i++) {
		int c = s[i], at = a ? a[i] & 0x1f : 0;
		char sub[64];
		int k;

		if (!S.printing)
			goto index_only;
		if (c == M.vstart && (k = special((const char *)s + i, n - i, sub)) > 0) {
			const char *p;

			setattr(at);
			for (p = sub; *p; p++)
				tchar((unsigned char)*p);
			i += k - 1;
			continue;
		}
		if (inpara) {
			if (pccis(c, 6)) {
				int lev = 0, j;
				char b[40];

				num[nn] = 0;
				for (j = 0; j < nn; j++)
					if (num[j] == '.')
						lev++;
				if (lev > 7)
					lev = 7;
				S.para[lev]++;
				for (j = lev + 1; j < 8; j++)
					S.para[j] = 0;
				b[0] = 0;
				for (j = 0; j <= lev; j++)
					sprintf(b + strlen(b), j ? ".%d" : "%d", S.para[j] ? S.para[j] : 1);
				for (j = 0; b[j]; j++)
					tchar((unsigned char)b[j]);
				inpara = 0;
				continue;
			}
			if (nn < 39)
				num[nn++] = (char)c;
			continue;
		}
		if (pccis(c, 6)) {
			inpara = 1;
			nn = 0;
			continue;
		}
		setattr(at);
		if (pccis(c, 0)) {
			outs(R.esc);
			continue;
		}
		if (pccis(c, 2) && i + 1 < n) {
			outc(s[++i] & 0x1f);
			continue;
		}
		if (pccis(c, 3)) {
			if (ppdf)
				tchar(0xa3);
			outs(R.pound);
			continue;
		}
		if (pccis(c, 4)) {
			outs(R.ul);
			continue;
		}
		if (pccis(c, 5)) {
			tchar(' ');
			continue;
		}
		if (pccis(c, 8)) {
			tchar('-');
			continue;
		}
		if (pccis(c, 9)) {
			outs(R.sp1);
			continue;
		}
		if (pccis(c, 10)) {
			outs(R.sp2);
			continue;
		}
		if (pccis(c, 11)) {
			char b[12];
			int j;

			outs(R.fnon);
			psup = ppdf != 0;
			sprintf(b, "%d", ++S.fnum);
			for (j = 0; b[j]; j++)
				tchar((unsigned char)b[j]);
			psup = 0;
			outs(R.fnoff);
			continue;
		}
index_only:
		if (pccis(c, 7)) {
			if (inidx && S.ndx) {
				idx[ni] = 0;
				fprintf(S.ndx, "%s\t%d\n", idx, S.page);
			}
			inidx = !inidx;
			ni = 0;
			continue;
		}
		if (inidx && ni < 255)
			idx[ni++] = (char)c;
		if (S.printing)
			tchar(c);
	}
	if (!S.printing)
		return;
	setattr(0);
	if (nolf && ppdf)
		pdf_overprint(&phint);
	else if (nolf) {
		if (pps)
			daisy_flush(&phint);
		outs(R.nolf);
	}
	else
		newline();
}

static void
pstr(const char *s)
{
	memset(&phint, 0, sizeof phint);
	phint.left = -1;
	pline((const unsigned char *)s, NULL, (int)strlen(s), 0);
}

static void
waitkey(const char *msg)
{
	if (pvdu)
		return;
	t_clearrow(t_rows - 1);
	t_puts(t_rows - 1, 0, msg, A_NORM);
	t_refresh();
	if (in_key() == K_NONE)
		S.stop = 1;
	t_clearrow(t_rows - 1);
}

static void
showpage(void)
{
	char b[60];

	if (pvdu || print_quiet || shutup)
		return;
	sprintf(b, "%s%d", text(2, 18), S.page);
	t_clearrow(t_rows - 1);
	t_puts(t_rows - 1, 0, b, A_NORM);
	t_refresh();
}

static int
wanted(int page)
{
	if (page < S.from || page > S.to)
		return 0;
	if (S.oddeven == M.odd && page % 2 == 0)
		return 0;
	if (S.oddeven == M.even && page % 2 == 1)
		return 0;
	return 1;
}

static void
page_start(void)
{
	int i;

	S.started = 1;
	S.q = 0;
	S.printing = wanted(S.page);
	S.lm = S.oddeven && pd_alt && S.page % 2 == 1 ? pd_alt : pd_lm;
	if (S.printing) {
		if (ppause && S.physical)
			waitkey(text(2, 16));
		S.physical = 1;
		showpage();
		for (i = 0; i < S.ntitle; i++)
			pstr(S.title[i]);
	}
}

static void
page_end(int last)
{
	int i, body = pd_lpp, used;

	if (!S.started)
		return;
	if (S.printing) {
		/* footnotes and footers at the foot of the page */
		used = (S.q + 3) / 4;
		if (S.nfoot || S.nfnote) {
			for (i = used; i < body; i++)
				blankline();
			for (i = 0; i < S.nfnote; i++)
				pstr(S.fnote[i]);
			for (i = 0; i < S.nfoot; i++)
				pstr(S.foot[i]);
			used = body + S.nfnote + S.nfoot + S.ntitle;
		} else
			used += S.ntitle;
		if (ppdf)
			pdf_endpage();
		else if (pd_total > 0) {
			for (i = used; i < pd_total; i++)
				outs("\r\n");
		} else if (!last || plastff)
			outs(last ? R.ff : R.ff2);
	}
	for (i = 0; i < S.nfnote; i++)
		xfree(S.fnote[i]);
	S.nfnote = 0;
	S.started = 0;
	S.page++;
}

static void
settext(char **arr, int *n, const struct line *l)
{
	char *t;

	if (*n >= MAXT)
		return;
	{
		int o = l->len > 2 && l->s[2] == ' ' ? 3 : 2;

		t = (char *)xrealloc(NULL, l->len + 1);
		memcpy(t, l->s + o, l->len - o);
		t[l->len - o] = 0;
	}
	arr[(*n)++] = t;
}

static void
cleartext(char **arr, int *n)
{
	int i;

	for (i = 0; i < *n; i++)
		xfree(arr[i]);
	*n = 0;
}

/* the page breaks of a document without .P or .PP lines */
static int	*breakat;

static void
autobreaks(struct doc *d)
{
	int i, q = 0, sp = S.sp, lim = pd_lpp * 4, start = 0;

	breakat = (int *)xrealloc(NULL, (d->n + 1) * sizeof(int));
	memset(breakat, 0, (d->n + 1) * sizeof(int));
	if (pd_lpp <= 0)
		return;
	for (i = 0; i < d->n; i++) {
		struct line *l = &d->l[i];

		if (isctl(l)) {
			if (l->s[1] == 'S' && l->len > 2)
				sp = (int)(atof((char *)l->s + 2) * 4 + 0.5);
			continue;
		}
		if (l->flags & (LF_RULER | LF_CONT))
			continue;
		q += sp > 0 ? sp : 4;
		if (q > lim) {
			int k, best = -1;

			for (k = i - 1; k > i - 1 - pd_widow && k > start; k--)
				if (d->l[k].len == 0) {
					best = k + 1;
					break;
				}
			if (best < 0)
				for (k = i; k < i + pd_orphan && k < d->n; k++)
					if (d->l[k].len == 0) {
						best = k + 1;
						break;
					}
			if (best < 0)
				best = i;
			breakat[best] = 1;
			start = best;
			i = best - 1;
			q = 0;
		}
	}
}

static void
fork_x(const struct line *l)
{
	char b[MAXW + 1], prog[80], arg[80];
	int n = 0, m = 0, k;
	char *p, *slash;

	memcpy(b, l->s + 2, l->len - 2);
	b[l->len - 2] = 0;
	sscanf(b, "%d %d", &n, &m);
	slash = strchr(b, '/');
	if (slash == NULL)
		return;
	p = slash + 1;
	for (k = 0; *p && *p != '_' && *p != ' ' && k < 79; p++)
		prog[k++] = *p;
	prog[k] = 0;
	if (*p == '_')
		p++;
	for (k = 0; *p && *p != ' ' && k < 79; p++)
		arg[k++] = *p;
	arg[k] = 0;
	if (pf)
		fflush(pf);
	sprintf(b, "%s %s", prog, arg);
	fork_prog(b, 0);
	S.page += n;
	S.q += m * 4;
}

static void
print_lines(struct doc *d, int depth)
{
	int i, auto_pg = 1, intitle = 0, infoot = 0;
	int *brk;
	struct ruler ru;

	memset(&ru, 0, sizeof ru);
	ru.left = ru.right = -1;
	for (i = 0; i < d->n; i++)
		if (isctl(&d->l[i]) && d->l[i].s[1] == 'P')
			auto_pg = 0;
	breakat = NULL;
	if (auto_pg && depth == 0)
		autobreaks(d);
	brk = breakat;
	for (i = 0; i < d->n && !S.stop; i++) {
		struct line *l = &d->l[i];

		if (in_peekterm() == CTL('C')) {
			S.stop = 1;
			break;
		}
		if (l->flags & LF_RULER) {
			ruler_parse(&ru, (const char *)l->s, l->len);
			continue;
		}
		if (brk && brk[i] && S.started)
			page_end(0);
		if (isctl(l)) {
			int c = l->s[1];

			if (c != 'T')
				intitle = 0;
			if (c != 'F')
				infoot = 0;
			switch (c) {
			case 'C':
				setpcc(l->s + 2, l->len - 2);
				break;
			case 'T':
				if (!intitle)
					cleartext(S.title, &S.ntitle);
				intitle = 1;
				settext(S.title, &S.ntitle, l);
				break;
			case 'F':
				if (!infoot)
					cleartext(S.foot, &S.nfoot);
				infoot = 1;
				settext(S.foot, &S.nfoot, l);
				break;
			case 'G':
				if (S.started && S.q / 4 + atoi((char *)l->s + 2) > pd_lpp)
					page_end(0);
				break;
			case 'H':
				{
					char b[MAXW + 1];

					memcpy(b, l->s + 2, l->len - 2);
					b[l->len - 2] = 0;
					if (pf)
						fflush(pf);
					waitkey(b);
				}
				break;
			case 'L':
				{
					int to = atoi((char *)l->s + 2);

					if (!S.started)
						page_start();
					while (S.q / 4 < to - 1 && S.q / 4 < pd_lpp) {
						if (S.printing)
							blankline();
						S.q += 4;
					}
				}
				break;
			case 'M':
				if (depth < 8) {
					char name[MAXNAME + 1], path[300];
					struct doc *m = doc_new();
					int k = 2, n = 0;

					while (k < l->len && l->s[k] == ' ')
						k++;
					while (k < l->len && l->s[k] != ' ' && n < MAXNAME)
						name[n++] = (char)l->s[k++];
					name[n] = 0;
					docname_norm(name, I.defext);
					docname_file(path, name);
					if (doc_load(m, path, MAXW) == 0)
						print_lines(m, depth + 1);
					else
						error("NF");
					doc_free(m);
					breakat = brk;
				}
				break;
			case 'N':
				if (S.nfnote < 16) {
					char *t = (char *)xrealloc(NULL, l->len + 8);

					int L;

					sprintf(t, "%d ", S.fnum ? S.fnum : S.nfnote + 1);
					L = (int)strlen(t);
					memcpy(t + L, l->s + 2, l->len - 2);
					t[L + l->len - 2] = 0;
					S.fnote[S.nfnote++] = t;
				}
				break;
			case 'P':
				{
					int k = l->s[2] == 'P' ? 3 : 2;
					const unsigned char *s = l->s;

					while (k < l->len && (s[k] == '=' || s[k] == ' '))
						k++;
					if (S.started)
						page_end(0);
					if (k < l->len && s[k] == '*' && k + 1 < l->len)
						S.page = atoi((char *)s + k + 1);
				}
				break;
			case 'S':
				if (l->len > 2) {
					double v = atof((char *)l->s + 2);

					S.sp = v > 0 ? (int)(v * 4 + 0.5) : 4;
				}
				break;
			case 'X':
				fork_x(l);
				break;
			default:	/* .I and others: not printed */
				break;
			}
			continue;
		}
		intitle = infoot = 0;
		if (!S.started)
			page_start();
		{
			/* a long line goes on in the rows after it */
			unsigned char *s = l->s, *a = l->a;
			int n = l->len, j = i, own = 0;

			while ((d->l[j].flags & LF_CONT) && j + 1 < d->n) {
				struct line *nx = &d->l[++j];

				if (!own) {
					unsigned char *ns = (unsigned char *)xrealloc(NULL, n + 1), *na = (unsigned char *)xrealloc(NULL, n + 1);

					memcpy(ns, s, n);
					memcpy(na, a, n);
					s = ns;
					a = na;
					own = 1;
				}
				s = (unsigned char *)xrealloc(s, n + nx->len + 1);
				a = (unsigned char *)xrealloc(a, n + nx->len + 1);
				memcpy(s + n, nx->s, nx->len);
				memcpy(a + n, nx->a, nx->len);
				n += nx->len;
			}
			/* for the PDF: the ruler, and whether the line is
			   justified: a J ruler, the text to M, more to come */
			memset(&phint, 0, sizeof phint);
			phint.left = ru.left >= 0 ? ru.left + S.lm : -1;
			phint.right = ru.right >= 0 ? ru.right + S.lm : -1;
			phint.kind = ru.kind;
			if (ru.kind == 'J' && ru.right > 0) {
				int e = n;

				while (e > 0 && s[e - 1] == ' ')
					e--;
				if (e == ru.right && j + 1 < d->n) {
					const struct line *nx = &d->l[j + 1];
					int k;

					for (k = 0; k < nx->len && nx->s[k] == ' '; k++)
						;
					phint.just = k < nx->len && !(nx->flags & LF_RULER) && !isctl(nx);
				}
			}
			pline(s, a, n, 0);
			if (own) {
				xfree(s);
				xfree(a);
			}
			i = j;
		}
		S.q += S.sp > 0 ? S.sp : 4;
		if (!brk && !auto_pg)
			;
		else if (!brk && S.q > pd_lpp * 4 && pd_lpp > 0)
			page_end(0);
	}
	if (depth == 0) {
		xfree(brk);
		breakat = NULL;
	}
}

static int
askpage(const char *prompt, int dflt, int *only)
{
	char b[12];
	int term;

	sprintf(b, "%d", dflt);
	t_clearrow(t_rows - 1);
	if (ui_line(t_rows - 1, prompt, b, 6, 6, &term) < 0)
		return -1;
	if (only)
		*only = term == LF;
	return b[0] ? atoi(b) : dflt;
}

/*
 * Print the document at path.  ask: the printer is asked for (else it
 * is open already: list processing).
 */
int
print_doc(const char *path, int copies, int ask)
{
	struct doc *d = doc_new();
	FILE *f = NULL;
	int pr = 0, ispipe = 0, c, i, hastitle = 0, alt = 0, only = 0;
	int from = 1, to = 9999, start;

	if (doc_load(d, path, MAXW) < 0) {
		doc_free(d);
		error("NF");
		return -1;
	}
	if (ask && print_open(&pr, &f, &ispipe) < 0) {
		doc_free(d);
		return -1;
	}
	for (i = 0; i < d->n; i++)
		if (isctl(&d->l[i]) && (d->l[i].s[1] == 'T' || d->l[i].s[1] == 'F')) {
			hastitle = 1;
			if (d->l[i].len > 2 && (d->l[i].s[2] == M.odd || d->l[i].s[2] == M.even))
				alt = 1;
		}
	if (ask && ppause && !pvdu) {
		t_clearrow(t_rows - 1);
		ppause = ui_yesno(t_rows - 1, 0, text(2, 15)) == 1;
	}
	if (R.havepetal && !pvdu) {
		char b[40];

		sprintf(b, "%s%s", text(2, 17), P.p[pr].petal);
		t_clearrow(t_rows - 2);
		t_puts(t_rows - 2, 0, b, A_NORM);
	}
	for (c = 0; c < copies; c++) {
		memset(&S, 0, sizeof S);
		setpcc((const unsigned char *)"", 0);
		S.sp = 4;
		start = P.pagestart;
		S.from = 0;
		S.to = 99999;
		if (hastitle) {
			if ((start = askpage(text(2, 19), P.pagestart, NULL)) < 0)
				break;
again:
			if ((from = askpage(text(2, 20), start, &only)) < 0)
				break;
			to = only ? from : askpage(text(2, 21), 9999, NULL);
			if (to < 0)
				break;
			S.from = from;
			S.to = to;
			if (alt) {
				char b[4];
				int term;

				b[0] = 0;
				t_clearrow(t_rows - 1);
				ui_line(t_rows - 1, text(2, 22), b, 1, 1, &term);
				S.oddeven = upcase((unsigned char)b[0]);
				if (S.oddeven != M.odd && S.oddeven != M.even)
					S.oddeven = 0;
			}
		}
		S.page = start;
		if (P.ndxext[0]) {
			char np[300], *dot;

			strcpy(np, path);
			dot = strrchr(np, '.');
			if (dot == NULL || strchr(dot, '/'))
				dot = np + strlen(np);
			sprintf(dot, "%s", P.ndxext);
			for (; *dot; dot++)
				*dot = (char)lowcase((unsigned char)*dot);
			S.ndx = fopen(np, "w");
		}
		outs(R.set);
		print_lines(d, 0);
		page_end(1);
		outs(R.reset);
		cleartext(S.title, &S.ntitle);
		cleartext(S.foot, &S.nfoot);
		if (S.ndx)
			fclose(S.ndx);
		if (pf)
			fflush(pf);
		if (only && !S.stop)
			goto again;
		if (S.stop)
			break;
	}
	if (ask)
		print_close(pr, f, ispipe);
	doc_free(d);
	return 0;
}

/* For list processing and mailshot: one document after another */

void
print_begin(void)
{
	memset(&S, 0, sizeof S);
	setpcc((const unsigned char *)"", 0);
	S.sp = 4;
	S.page = P.pagestart;
	S.from = 0;
	S.to = 99999;
	outs(R.set);
}

void
print_pcc(const char *pcc)
{
	setpcc((const unsigned char *)pcc, (int)strlen(pcc));
}

void
print_spacing(int quarters)
{
	S.sp = quarters > 0 ? quarters : 4;
}

void
print_text(const char *s, int lm)
{
	S.printing = 1;
	S.lm = lm;
	memset(&phint, 0, sizeof phint);
	phint.left = -1;
	pline((const unsigned char *)s, NULL, (int)strlen(s), 0);
}

void
print_docpart(struct doc *d)
{
	print_lines(d, 1);
	page_end(1);
	S.started = 0;
}

void
print_ff(int last)
{
	if (ppdf)
		pdf_endpage();
	else
		outs(last ? R.ff : R.ff2);
}

void
print_end(void)
{
	outs(R.reset);
	cleartext(S.title, &S.ntitle);
	cleartext(S.foot, &S.nfoot);
	if (pf)
		fflush(pf);
}

int
print_pausing(void)
{
	return ppause;
}

void
print_setpause(int on)
{
	ppause = on;
}

int
print_stopped(void)
{
	return S.stop;
}
