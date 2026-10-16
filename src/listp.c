/*
 * listp.c - the report generator (manual chapter 13): list processing
 * (/S) and mailshot (/L).  A dictionary names the fields of the data
 * records, an output format says which records, how they are chosen
 * and how they are laid out:
 *
 *	*DICT V2123 .05F KEY=? /A:N /T:12
 *	headings (list processing), a blank line, the detail lines
 *
 * The record a dictionary describes is its key (10) and then its data:
 * RECTYPE is the first character of the key, KEY the other nine, the
 * fields of the dictionary start at the second character of the data
 * (the first is the cell after the key on a form).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lex.h"

#define MAXF	120
#define MAXLINES 200

struct dfield {
	int	fc;
	char	name[10];
	int	off, len;
	int	special;	/* the operand of **TEXTP: 3 KEY .. 9 TIME */
};

static struct dfield	F[MAXF];
static int		nf;

/* the operands of **TEXTP: AND OR NOT KEY RECTYPE DATE PAGE SRECS TRECS TIME */
#define OP_AND		0
#define OP_OR		1
#define OP_NOT		2
#define OP_KEY		3
#define OP_RECTYPE	4
#define OP_DATE		5
#define OP_PAGE		6
#define OP_SRECS	7
#define OP_TRECS	8
#define OP_TIME		9
static char	opname[10][10];
static int	opch[10];

/* the record in hand */
static char	*rbuf;
static long	rlen;
static long	nsel, ntest;
static int	pageno;
static int	paint = ' ';
static int	numeric;

static void
load_ops(void)
{
	const char *t = text(2, 1);
	static const char *dname[10] = { "AND", "OR", "NOT", "KEY", "RECTYPE", "DATE", "PAGE", "SRECS", "TRECS", "TIME" };
	static const char dch[10] = { '&', '/', '@', 'K', 'k', 'D', 'd', 'z', 'Z', 't' };
	int i, L = (int)strlen(t);

	for (i = 0; i < 10; i++) {
		int j;

		if (10 * i + 9 <= L) {
			memcpy(opname[i], t + 10 * i, 8);
			opname[i][8] = 0;
			for (j = 7; j >= 0 && opname[i][j] == ' '; j--)
				opname[i][j] = 0;
			opch[i] = (unsigned char)t[10 * i + 8];
		} else {
			strcpy(opname[i], dname[i]);
			opch[i] = dch[i];
		}
	}
}

static int
nameeq(const char *a, const char *b)
{
	while (*a && *b) {
		if (upcase((unsigned char)*a) != upcase((unsigned char)*b))
			return 0;
		a++, b++;
	}
	return *a == *b;
}

static struct dfield *
byname(const char *n)
{
	int i;

	for (i = 0; i < nf; i++)
		if (nameeq(F[i].name, n))
			return &F[i];
	return NULL;
}

static struct dfield *
bychar(int c)
{
	int i;

	for (i = 0; i < nf; i++)
		if (F[i].fc == c)
			return &F[i];
	return NULL;
}

static void
addspecial(int op, int off, int len)
{
	if (nf >= MAXF)
		return;
	F[nf].fc = opch[op];
	strcpy(F[nf].name, opname[op]);
	F[nf].off = off;
	F[nf].len = len;
	F[nf].special = op;
	nf++;
}

static int
load_dict(const char *key)
{
	char *t, *p, *tok[600];
	long len;
	int n = 0, i, off = 11;

	nf = 0;
	addspecial(OP_KEY, 1, 9);
	addspecial(OP_RECTYPE, 0, 1);
	addspecial(OP_DATE, -1, 22);
	addspecial(OP_PAGE, -1, 4);
	addspecial(OP_TIME, -1, 8);
	addspecial(OP_SRECS, -1, 5);
	addspecial(OP_TRECS, -1, 5);
	if (rec_text(key, &t, &len, '_') < 0)
		return -1;
	/* the comments go: from ! to the end of the line */
	for (p = t; *p; p++) {
		if (*p == '!') {
			while (*p && *p != '\n')
				*p++ = ' ';
			if (!*p)
				break;
		}
		if (*p == '\n' || *p == '\r' || *p == '\t')
			*p = ' ';
	}
	for (p = t; *p && n < 600;) {
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		tok[n++] = p;
		while (*p && *p != ' ')
			p++;
		if (*p)
			*p++ = 0;
	}
	for (i = 0; i + 2 < n && nf < MAXF; i += 3) {
		struct dfield *f = &F[nf];
		char *l = tok[i + 2], *q;

		if (strlen(tok[i]) != 1) {
			xfree(t);
			return -2;
		}
		f->fc = (unsigned char)tok[i][0];
		strncpy(f->name, tok[i + 1], 8);
		f->name[8] = 0;
		f->special = -1;
		if (l[0] >= '0' && l[0] <= '9') {
			f->off = off;
			f->len = atoi(l);
			off += f->len;
		} else if ((q = strpbrk(l, "+-#")) != NULL) {
			char base[10];
			struct dfield *b;
			int d, m = (int)(q - l) < 9 ? (int)(q - l) : 9;

			memcpy(base, l, m);
			base[m] = 0;
			b = byname(base);
			if (b == NULL || b->off < 0) {
				xfree(t);
				return -2;
			}
			d = atoi(q + 1);
			if (*q == '-')
				d = -d;
			f->off = b->off + d;
			q = strchr(q, '/');
			f->len = q ? atoi(q + 1) : b->len - d;
		} else {
			xfree(t);
			return -2;
		}
		nf++;
	}
	xfree(t);
	return 0;
}

/* the value of a field in the record in hand (n characters) */
static void
fieldval(const struct dfield *f, char *out, int max)
{
	int i, n = f->len < max - 1 ? f->len : max - 1;

	if (f->special == OP_DATE) {
		date_full(out);
		return;
	}
	if (f->special == OP_PAGE) {
		sprintf(out, "%04d", pageno);
		return;
	}
	if (f->special == OP_TIME) {
		time_hms(out, M.timesep);
		return;
	}
	if (f->special == OP_SRECS) {
		sprintf(out, "%05ld", nsel);
		return;
	}
	if (f->special == OP_TRECS) {
		sprintf(out, "%05ld", ntest);
		return;
	}
	for (i = 0; i < n; i++) {
		long o = f->off + i;
		int c = o >= 0 && o < rlen ? (unsigned char)rbuf[o] : ' ';

		if (c == '\n' || c == paint)
			c = ' ';
		out[i] = (char)c;
	}
	out[n] = 0;
}

static int
fieldchar(const struct dfield *f, int pos)
{
	char b[300];
	long o;
	int c;

	if (f->off < 0) {
		fieldval(f, b, sizeof b);
		return pos < (int)strlen(b) ? b[pos] : ' ';
	}
	o = f->off + pos;
	c = o >= 0 && o < rlen ? (unsigned char)rbuf[o] : ' ';
	return c == '\n' || c == paint ? ' ' : c;
}

static void
rtrim(char *s)
{
	int n = (int)strlen(s);

	while (n > 0 && s[n - 1] == ' ')
		s[--n] = 0;
}

/* Selection */

#define MAXV	16

struct term {
	struct dfield	*f;
	int		not;
	int		eq, gt, lt, has;
	char		*v[MAXV];
	int		nv;
	int		conn;		/* OP_AND or OP_OR before it */
};

static struct term	T[32];
static int		nt;
static char		selbuf[512];

static int
isopc(int c)
{
	return c == M.op_eq || c == M.op_gt || c == M.op_lt || c == M.op_has;
}

static int
word(const char **pp, char *w, int max)
{
	const char *p = *pp;
	int n = 0;

	while (*p == ' ')
		p++;
	while (*p && *p != ' ' && !isopc((unsigned char)*p) && n < max - 1)
		w[n++] = *p++;
	w[n] = 0;
	*pp = p;
	return n;
}

static int
parse_sel(const char *s)
{
	const char *p = s;
	char w[64];
	char *o;
	int conn = OP_AND;

	nt = 0;
	o = selbuf;
	for (;;) {
		struct term *t;
		int not = 0;

		while (*p == ' ')
			p++;
		if (!*p)
			break;
		if (nt >= 32)
			return -1;
		t = &T[nt];
		memset(t, 0, sizeof *t);
		for (;;) {
			const char *save = p;

			if (!word(&p, w, sizeof w))
				return -1;
			if (nameeq(w, opname[OP_NOT])) {
				not = !not;
				continue;
			}
			if (nameeq(w, opname[OP_AND]) || nameeq(w, opname[OP_OR])) {
				if (nt == 0)
					return -1;
				conn = nameeq(w, opname[OP_AND]) ? OP_AND : OP_OR;
				continue;
			}
			(void)save;
			break;
		}
		if ((t->f = byname(w)) == NULL)
			return -1;
		while (*p == ' ')
			p++;
		{
			const char *q = p;

			if (word(&q, w, sizeof w) && nameeq(w, opname[OP_NOT])) {
				not = !not;
				p = q;
				while (*p == ' ')
					p++;
			}
		}
		if (!isopc((unsigned char)*p))
			return -1;
		while (isopc((unsigned char)*p)) {
			if (*p == M.op_eq)
				t->eq = 1;
			else if (*p == M.op_gt)
				t->gt = 1;
			else if (*p == M.op_lt)
				t->lt = 1;
			else
				t->has = 1;
			p++;
		}
		/* the values: a,b,"c d" up to a blank */
		t->v[t->nv++] = o;
		while (*p && *p != ' ') {
			if (*p == '"') {
				p++;
				while (*p && *p != '"' && o < selbuf + sizeof selbuf - 2)
					*o++ = *p++;
				if (*p == '"')
					p++;
				continue;
			}
			if (*p == ',') {
				*o++ = 0;
				p++;
				if (t->nv < MAXV)
					t->v[t->nv++] = o;
				continue;
			}
			if (o < selbuf + sizeof selbuf - 2)
				*o++ = *p;
			p++;
		}
		*o++ = 0;
		t->not = not;
		t->conn = conn;
		conn = OP_AND;
		nt++;
	}
	return 0;
}

static int
isnum(int fc)
{
	return fc >= '0' && fc <= '9';
}

static double
numval(const char *s)
{
	char b[64];
	int n = 0;

	for (; *s && n < 63; s++)
		if (*s == I.decpt && I.decpt)
			b[n++] = '.';
		else if ((*s >= '0' && *s <= '9') || *s == '-' || *s == '.')
			b[n++] = *s;
	b[n] = 0;
	return atof(b);
}

/* "a" contains "b": without case, blanks not counting */
static int
contains(const char *a, const char *b)
{
	char x[300], y[100];
	int n = 0, m = 0;

	for (; *a && n < 299; a++)
		if (*a != ' ')
			x[n++] = (char)upcase((unsigned char)*a);
	x[n] = 0;
	for (; *b && m < 99; b++)
		if (*b != ' ')
			y[m++] = (char)upcase((unsigned char)*b);
	y[m] = 0;
	return strstr(x, y) != NULL;
}

static int
wildeq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if (*b != '?' && *a != *b)
			return 0;
	while (*b == '?')
		b++;
	return *a == 0 && *b == 0;
}

static int
cmpval(const struct term *t, const char *a, const char *b)
{
	if (numeric && isnum(t->f->fc)) {
		double x = numval(a), y = numval(b);

		return x < y ? -1 : x > y;
	}
	{
		int i;

		for (i = 0; a[i] || b[i]; i++) {
			int d = sortval((unsigned char)a[i]) - sortval((unsigned char)b[i]);

			if (d)
				return d;
		}
	}
	return 0;
}

static int
test1(const struct term *t)
{
	char val[300];
	int i;

	fieldval(t->f, val, sizeof val);
	rtrim(val);
	for (i = 0; i < t->nv; i++) {
		const char *v = t->v[i], *dash;
		int c;

		if (t->has && contains(val, v))
			return 1;
		if (t->eq && (dash = strchr(v, '-')) != NULL && dash > v && dash[1]) {
			char lo[100];

			memcpy(lo, v, dash - v);
			lo[dash - v] = 0;
			if (cmpval(t, val, lo) >= 0 &&
			    (strncmp(val, dash + 1, strlen(dash + 1)) <= 0 || cmpval(t, val, dash + 1) <= 0))
				return 1;
			continue;
		}
		if (t->eq && (t->f->special == OP_KEY || t->f->special == OP_RECTYPE)) {
			/* a key matches what it starts with */
			if (strncmp(val, v, strlen(v)) == 0)
				return 1;
		} else if (t->eq && (strchr(v, '?') ? wildeq(val, v) : cmpval(t, val, v) == 0))
			return 1;
		c = cmpval(t, val, v);
		if ((t->gt && c > 0) || (t->lt && c < 0))
			return 1;
	}
	return 0;
}

static int
selected(void)
{
	int i, r = 1, acc;

	if (nt == 0)
		return 1;
	/* AND before OR */
	acc = 1;
	r = 0;
	for (i = 0; i < nt; i++) {
		int v = test1(&T[i]) ^ T[i].not;

		if (i > 0 && T[i].conn == OP_OR) {
			r |= acc;
			acc = 1;
		}
		acc = acc && v;
	}
	return r | acc;
}

/* The output format */

struct fmt {
	char	dict[KEYLEN + 1];
	int	view;
	int	lpp, psize;
	int	rectype, margin, numeric;
	char	criteria[256];
	int	ask;
	int	sortdir;
	char	sortby[32];
	char	help[KEYLEN + 1];
	char	pcc[16];
	int	spacing;
	char	totals[32];
	int	totff;
	char	foreign[80];
	long	upos;
	char	ustr[80];
	char	*head[MAXLINES], *det[MAXLINES];
	int	nhead, ndet;
	char	*text;
};

static void
fmt_free(struct fmt *f)
{
	xfree(f->text);
}

static char *
nextline(char **pp)
{
	char *p = *pp, *e;

	if (p == NULL || !*p)
		return NULL;
	e = strchr(p, '\n');
	if (e) {
		*e = 0;
		*pp = e + 1;
	} else
		*pp = p + strlen(p);
	return p;
}

static int
parse_fmt(const char *key, struct fmt *f, int mailshot)
{
	char *p, *l, top[600], *q;
	long len;
	int n;

	memset(f, 0, sizeof *f);
	f->spacing = 4;
	f->totff = 1;
	if (rec_text(key, &f->text, &len, '_') < 0)
		return -1;
	p = f->text;
	if (*p == ' ' || *p == '_')
		p++;
	/* the top line, with its continuations ("&" in column 80) */
	top[0] = 0;
	while ((l = nextline(&p)) != NULL) {
		int L = (int)strlen(l), cont = L >= 80 && (unsigned char)l[79] == M.cont;

		if (cont)
			l[79] = 0;
		if (strlen(top) + strlen(l) < sizeof top - 1)
			strcat(top, l);
		if (!cont)
			break;
	}
	q = top;
	while (*q == ' ')
		q++;
	for (n = 0; *q && *q != ' ' && n < KEYLEN; q++)
		f->dict[n++] = *q;
	f->dict[n] = 0;
	while (*q && *q != ' ')
		q++;
	while (*q == ' ')
		q++;
	if (upcase((unsigned char)*q) != 'P' && upcase((unsigned char)*q) != 'V')
		return -3;
	f->view = upcase((unsigned char)*q) == 'V';
	q++;
	f->lpp = 60;
	f->psize = 0;
	if (q[0] >= '0' && q[0] <= '9' && q[1] >= '0' && q[1] <= '9') {
		f->lpp = (q[0] - '0') * 10 + q[1] - '0';
		q += 2;
		if (q[0] >= '0' && q[0] <= '9' && q[1] >= '0' && q[1] <= '9')
			f->psize = (q[0] - '0') * 10 + q[1] - '0', q += 2;
		else if (upcase((unsigned char)q[0]) == 'F' && upcase((unsigned char)q[1]) == 'F')
			f->psize = 0, q += 2;
	}
	while (*q && *q != ' ')
		q++;
	while (*q == ' ')
		q++;
	if (!*q)
		return -3;
	f->rectype = (unsigned char)*q++;
	if (q[0] >= '0' && q[0] <= '9' && q[1] >= '0' && q[1] <= '9') {
		f->margin = (q[0] - '0') * 10 + q[1] - '0';
		q += 2;
	}
	if (upcase((unsigned char)*q) == 'F')
		f->numeric = 1, q++;
	while (*q && *q != ' ')
		q++;
	/* the criteria up to the first option */
	{
		char *c = f->criteria;

		while (*q) {
			if (*q == '/' && (q == top || q[-1] == ' ') && strchr("ACDFHPSTU", upcase((unsigned char)q[1])) &&
			    q[1] && (q[2] == ':' || q[2] == ';' || q[2] == '*' || upcase((unsigned char)q[1]) == 'H'))
				break;
			if (*q == '?' && (q[1] == ' ' || q[1] == 0)) {
				f->ask = 1;
				q++;
				continue;
			}
			if (c < f->criteria + sizeof f->criteria - 1)
				*c++ = *q;
			q++;
		}
		*c = 0;
		rtrim(f->criteria);
		{
			char *s = f->criteria;

			while (*s == ' ')
				memmove(s, s + 1, strlen(s));
		}
	}
	/* the options */
	while (*q == '/') {
		int o = upcase((unsigned char)q[1]), sep;
		char v[100];
		int m = 0;

		q += 2;
		sep = *q;
		if (sep == ':' || sep == ';')
			q++;
		if (o == 'U') {
			f->upos = atol(q);
			while (*q >= '0' && *q <= '9')
				q++;
			if (*q == ':')
				q++;
			while (*q && *q != ':' && m < 79)
				f->ustr[m++] = *q++;
			f->ustr[m] = 0;
			if (*q == ':')
				q++;
			else if (m > 1)
				f->ustr[1] = 0;
		} else {
			while (*q && *q != ' ' && m < 99)
				v[m++] = *q++;
			v[m] = 0;
			switch (o) {
			case 'A':
			case 'D':
				f->sortdir = o == 'A' ? 1 : -1;
				scopy(f->sortby, v, 31);
				break;
			case 'C':
				scopy(f->pcc, v, 15);
				break;
			case 'H':
				scopy(f->help, v, KEYLEN);
				break;
			case 'P':
				paint = v[0] ? (unsigned char)v[0] : ' ';
				break;
			case 'S':
				f->spacing = (int)(atof(v) * 4 + 0.5);
				break;
			case 'T':
				scopy(f->totals, v, 31);
				f->totff = sep != ';';
				break;
			case 'F':
				scopy(f->foreign, v, 79);
				break;
			}
		}
		while (*q == ' ')
			q++;
	}
	/* headings up to a blank line, then the detail lines */
	if (!mailshot) {
		int blank = 0;

		while ((l = nextline(&p)) != NULL) {
			char *s = l;

			while (*s == ' ')
				s++;
			if (!*s) {
				blank = 1;
				break;
			}
			if (f->nhead < MAXLINES)
				f->head[f->nhead++] = l;
		}
		if (!blank && f->nhead) {
			/* no blank line: the format is bad */
			return -4;
		}
	}
	while ((l = nextline(&p)) != NULL)
		if (f->ndet < MAXLINES)
			f->det[f->ndet++] = l;
	while (f->ndet > 0 && f->det[f->ndet - 1][0] == 0)
		f->ndet--;
	if (!f->help[0])
		strcpy(f->help, "*HELPS");
	return 0;
}

/* a field written "(NAME:" in visible equivalents at s; its length */
static int
fieldref(const char *s, char *out)
{
	char name[16];
	int i;
	struct dfield *f;

	if ((unsigned char)s[0] != M.vstart)
		return 0;
	for (i = 1; s[i] && i < 12 && (unsigned char)s[i] != M.vend; i++)
		name[i - 1] = s[i];
	if ((unsigned char)s[i] != M.vend)
		return 0;
	name[i - 1] = 0;
	if ((f = byname(name)) == NULL)
		return 0;
	fieldval(f, out, 300);
	rtrim(out);
	return i + 1;
}

/* the positions reached in each field by its formatting characters */
static int	fpos[256];

static void
format_line(const char *src, char *out, int max, int heading, int *lit)
{
	int n = 0;
	const char *s;
	char v[300];

	for (s = src; *s && n < max - 1; s++) {
		int c = (unsigned char)*s, k;
		struct dfield *f;

		if (c == '\'') {
			*lit = !*lit;
			continue;
		}
		if ((k = fieldref(s, v)) > 0) {
			int j;

			for (j = 0; v[j] && n < max - 1; j++)
				out[n++] = v[j];
			s += k - 1;
			continue;
		}
		/* in the detail lines what is outside quotes is formatting,
		   in the headings what is inside */
		if ((heading ? *lit : !*lit) && c != ' ' && (f = bychar(c)) != NULL) {
			out[n++] = (char)fieldchar(f, fpos[c]++);
			continue;
		}
		out[n++] = (char)c;
	}
	out[n] = 0;
	{
		int L = (int)strlen(out);

		while (L > 0 && out[L - 1] == ' ')
			out[--L] = 0;
	}
}

static void
format_lines(char **src, int n, char lines[][MAXW + 1], int *nl, int heading)
{
	int i, lit = 0;

	memset(fpos, 0, sizeof fpos);
	for (i = 0; i < n && *nl < MAXLINES; i++) {
		if (heading)
			lit = 0;	/* in the detail lines a quote goes on */
		format_line(src[i], lines[(*nl)++], MAXW + 1, heading, &lit);
	}
}

/* Totals */

static double	tot[32];
static int	totdec[32];

static void
add_totals(const struct fmt *f)
{
	int i;

	for (i = 0; f->totals[i] && i < 32; i++) {
		struct dfield *d = bychar((unsigned char)f->totals[i]);
		char v[300], *dot;

		if (d == NULL)
			continue;
		fieldval(d, v, sizeof v);
		rtrim(v);
		tot[i] += numval(v);
		dot = strchr(v, I.decpt ? I.decpt : '.');
		if (dot && (int)strlen(dot + 1) > totdec[i])
			totdec[i] = (int)strlen(dot + 1);
	}
}

/* The output: on the screen, a page at a time, or to the printer */

static int	outview, outrow, outlpp, outlines;
static char	headl[MAXLINES][MAXW + 1];
static int	nheadl;
static int	stopped;
static const struct fmt *curfmt;

static void
emit(const char *s);

static void
headings(void)
{
	int i, n = 0;

	pageno++;
	if (curfmt && curfmt->nhead) {
		nheadl = 0;
		format_lines((char **)curfmt->head, curfmt->nhead, headl, &nheadl, 1);
		n = nheadl;
	}
	if (outview) {
		t_clear();
		outrow = 0;
	}
	outlines = 0;
	for (i = 0; i < n; i++) {
		if (outview) {
			if (outrow < t_rows - 1)
				t_puts(outrow++, 0, headl[i], A_NORM);
		} else
			print_text(headl[i], curfmt->margin);
		outlines++;
	}
}

static int
viewpause(void)
{
	int k;

	t_clearrow(t_rows - 1);
	t_puts(t_rows - 1, 0, text(0, 26), vdu_bgattr);
	t_refresh();
	k = in_key();
	if (k == K_NONE || k == ESC || k == CTL('C') || ((k == CTL('U') || (k == M.menuexit && !(k & K_LIT))) && !vdu_special))
		return -1;
	return 0;
}

static void
pageend(void)
{
	if (outview) {
		if (viewpause() < 0)
			stopped = 1;
	} else {
		int i;

		if (curfmt->psize > 0)
			for (i = outlines; i < curfmt->psize; i++)
				print_text("", 0);
		else
			print_ff(0);
	}
}

static void
emit(const char *s)
{
	if (stopped)
		return;
	if (outlines >= outlpp) {
		pageend();
		if (stopped)
			return;
		headings();
	}
	if (outview) {
		if (outrow < t_rows - 1)
			t_puts(outrow++, 0, s, A_NORM);
	} else
		print_text(s, curfmt->margin);
	outlines++;
}

/* Mailshot: the fields put into a copy of the document */

static struct doc *
mail_merge(const struct doc *md, char lines[][MAXW + 1], int nl)
{
	struct doc *d = doc_new();
	struct ruler r;
	int i, haveruler = 0;

	for (i = 0; i < nl; i++)
		doc_insline(d, d->n, (unsigned char *)lines[i], (int)strlen(lines[i]));
	for (i = 0; i < md->n; i++) {
		const struct line *l = &md->l[i];
		unsigned char b[1024], a[1024];
		int n = 0, k, changed = 0;

		if (l->flags & LF_RULER) {
			ruler_parse(&r, (const char *)l->s, l->len);
			haveruler = 1;
		}
		for (k = 0; k < l->len && n < 1000; k++) {
			char v[300];
			int m;

			if (!(l->flags & LF_RULER) && (m = fieldref((const char *)l->s + k, v)) > 0 && k + m <= l->len) {
				int j;

				for (j = 0; v[j] && n < 1000; j++) {
					b[n] = (unsigned char)v[j];
					a[n++] = l->a[k];
				}
				k += m - 1;
				changed = 1;
				continue;
			}
			b[n] = l->s[k];
			a[n++] = l->a[k];
		}
		doc_insline(d, d->n, b, n);
		line_setattr(&d->l[d->n - 1], b, a, n);
		d->l[d->n - 1].flags = l->flags;
		/* what goes past the margin goes on to the next line of the paragraph */
		while (changed) {
			struct line *cl = &d->l[d->n - 1];
			int right = haveruler && r.right > 0 ? r.right : 69, brk, k2;

			if (cl->len <= right + 1)
				break;
			for (brk = right + 1; brk > 0 && cl->s[brk] != ' '; brk--)
				;
			if (brk <= 0)
				break;
			for (k2 = brk; k2 < cl->len && cl->s[k2] == ' '; k2++)
				;
			{
				int rest = cl->len - k2, lm = haveruler && r.left > 0 ? r.left : 0;
				unsigned char rb[1024], ra[1024];
				const struct line *nx;

				nx = NULL;
				if (i + 1 < md->n)
					nx = &md->l[i + 1];

				memcpy(rb, cl->s + k2, rest);
				memcpy(ra, cl->a + k2, rest);
				cl->len = brk;
				if (nx && nx->len > 0 && !(nx->flags & LF_RULER) && nx->s[0] != '.' && nx->s[0] != ' ') {
					/* joined to the front of the next line */
					unsigned char nb[1024], na[1024];
					int nn = 0, j;

					for (j = 0; j < lm; j++)
						nb[nn] = ' ', na[nn++] = 0;
					memcpy(nb + nn, rb, rest);
					memcpy(na + nn, ra, rest);
					nn += rest;
					nb[nn] = ' ';
					na[nn++] = 0;
					for (j = 0; j < nx->len && nn < 1020; j++)
						nb[nn] = nx->s[j], na[nn++] = nx->a[j];
					doc_insline(d, d->n, nb, nn);
					line_setattr(&d->l[d->n - 1], nb, na, nn);
					i++;
				} else {
					unsigned char nb[1024], na[1024];
					int nn = 0, j;

					for (j = 0; j < lm; j++)
						nb[nn] = ' ', na[nn++] = 0;
					memcpy(nb + nn, rb, rest);
					memcpy(na + nn, ra, rest);
					nn += rest;
					doc_insline(d, d->n, nb, nn);
					line_setattr(&d->l[d->n - 1], nb, na, nn);
				}
			}
		}
	}
	return d;
}

/* The records chosen, in the order they come out */

struct pick {
	int	i;
	char	*sk;
};

static struct vmf	*srcv;
static struct pick	*picks;
static int		npick, maxpick;

static void
loadrec(int i)
{
	long len;
	unsigned char *d = vmf_entdata(srcv, i, &len);
	char *u;
	long un;

	/* the runs of the squash character are the paint of the form:
	   blanks here, unless /P says otherwise */
	unsquash(d, len, &u, &un, paint);
	xfree(rbuf);
	rbuf = (char *)xrealloc(NULL, un + KEYLEN + 1);
	memcpy(rbuf, srcv->ent[i].key, KEYLEN);
	memcpy(rbuf + KEYLEN, u, un);
	rlen = un + KEYLEN;
	rbuf[rlen] = 0;
	xfree(u);
}

static int
pickcmp(const void *a, const void *b)
{
	const struct pick *x = (const struct pick *)a, *y = (const struct pick *)b;
	const unsigned char *p = (const unsigned char *)x->sk, *q = (const unsigned char *)y->sk;

	for (; *p || *q; p++, q++)
		if (sortval(*p) != sortval(*q))
			return sortval(*p) - sortval(*q);
	return x->i - y->i;
}

static int	sortsign;

static int
pickcmp_dir(const void *a, const void *b)
{
	return sortsign * pickcmp(a, b);
}

static void
showhelp(const struct fmt *f)
{
	char b[MAXW + 1], *t;
	long len;
	int row = 2, i, col;

	t_clear();
	t_puts(0, 0, text(2, 5), vdu_bgattr);
	b[0] = 0;
	col = 0;
	for (i = 0; i < nf; i++) {
		if (F[i].special >= 0)
			continue;
		if (col + (int)strlen(F[i].name) + 1 > t_cols - 1) {
			t_puts(row++, 0, b, A_NORM);
			b[0] = 0;
			col = 0;
		}
		strcat(b, F[i].name);
		strcat(b, " ");
		col += (int)strlen(F[i].name) + 1;
	}
	if (b[0])
		t_puts(row++, 0, b, A_NORM);
	row++;
	if (rec_text(f->help, &t, &len, '_') == 0) {
		char *p = t, *e;

		if (*p == ' ' || *p == '_')
			p++;
		while (*p && row < t_rows - 3) {
			e = strchr(p, '\n');
			if (e)
				*e = 0;
			t_puts(row++, 0, *p == '!' ? p + 1 : p, A_NORM);
			if (!e)
				break;
			p = e + 1;
		}
		xfree(t);
	}
}

/* the select line: what is typed after the criteria of the format */
static int
ask_select(const struct fmt *f, char *sel, int max, int *kill)
{
	char typed[300], prompt[300];
	int term;

	*kill = 0;
	strcpy(sel, f->criteria);
	if (!f->ask)
		return 0;
	if (I.helpsel)
		showhelp(f);
	for (;;) {
		char *k;

		typed[0] = 0;
		sprintf(prompt, "%s%s", text(2, 7), f->criteria);
		t_clearrow(t_rows - 1);
		if (ui_line(t_rows - 2, prompt, typed, 250, 0, &term) < 0)
			return -1;
		if (nameeq(typed, M.help[0] ? M.help : "HELP") || nameeq(typed, "HELP")) {
			showhelp(f);
			continue;
		}
		/* "[*SEL]": the criteria in a record */
		{
			int L = (int)strlen(typed);

			if (L > 2 && ((unsigned char)typed[0] == M.startkey || (unsigned char)typed[0] == M.vstart) &&
			    ((unsigned char)typed[L - 1] == M.endkey || (unsigned char)typed[L - 1] == M.vend)) {
				char key[12], *t;
				long len;

				memcpy(key, typed + 1, L - 2 > 10 ? 10 : L - 2);
				key[L - 2 > 10 ? 10 : L - 2] = 0;
				if (rec_text(key, &t, &len, '_') == 0) {
					char *p = t;
					int n;

					if (*p == ' ' || *p == '_')
						p++;
					strncpy(typed, p, 290);
					typed[290] = 0;
					for (n = 0; typed[n]; n++)
						if (typed[n] == '\n')
							typed[n] = ' ';
					xfree(t);
				}
			}
		}
		/* "/K/K" or "/K/Q" at the end: the records chosen are deleted */
		if (M.kill && (k = strchr(typed, '/')) != NULL) {
			char a[3];

			a[0] = (char)upcase((unsigned char)k[1]);
			a[1] = k[2] == '/' ? (char)upcase((unsigned char)k[3]) : 0;
			if (a[0] == M.kill && (a[1] == M.kill || a[1] == M.query))
				*kill = 1;
			*k = 0;
		}
		if ((int)(strlen(sel) + strlen(typed)) < max - 1)
			strcat(sel, typed);
		if (parse_sel(sel) < 0) {
			t_clearrow(t_rows - 1);
			t_puts(t_rows - 1, 0, text(2, 8), A_NORM);
			bell();
			strcpy(sel, f->criteria);
			continue;
		}
		return 0;
	}
}

/*
 * List processing (mailshot 0) with the output format fmtkey, or a
 * mailshot of the document docname.
 */
int
listproc(const char *fmtkey, int mailshot, const char *docname)
{
	struct fmt f;
	char sel[600], k0[KEYLEN];
	static char lines[MAXLINES][MAXW + 1];
	struct doc *md = NULL;
	int r, i, kill = 0, pr = 0, ispipe = 0, rows;
	FILE *fp = NULL;

	paint = ' ';
	load_ops();
	r = parse_fmt(fmtkey, &f, mailshot);
	if (r == -1) {
		ui_pause(text(2, 2));
		return -1;
	}
	if (r < 0) {
		fmt_free(&f);
		ui_pause(text(2, 25));
		return -1;
	}
	r = load_dict(f.dict);
	if (r < 0) {
		char b[100];

		sprintf(b, "%s%s", f.dict, r == -1 ? text(2, 3) : text(2, 4));
		fmt_free(&f);
		ui_pause(r == -1 ? text(2, 3) : b);
		return -1;
	}
	numeric = f.numeric || I.freecmp;
	if (f.foreign[0] && foreign_set(f.foreign) < 0) {
		fmt_free(&f);
		error("OF");
		return -1;
	}
	if (mailshot) {
		char path[300];

		docname_file(path, docname);
		md = doc_new();
		if (doc_load(md, path, MAXW) < 0) {
			doc_free(md);
			fmt_free(&f);
			error("NF");
			return -1;
		}
	}
	if (ask_select(&f, sel, sizeof sel, &kill) < 0 || parse_sel(sel) < 0) {
		if (md)
			doc_free(md);
		fmt_free(&f);
		return -1;
	}
	/* the records: of the record type, in key order, chosen */
	memset(k0, ' ', KEYLEN);
	k0[0] = (char)f.rectype;
	srcv = vmf_for(k0);
	if (srcv == NULL) {
		if (md)
			doc_free(md);
		fmt_free(&f);
		return -1;
	}
	npick = 0;
	nsel = ntest = 0;
	t_clearrow(t_rows - 1);
	t_puts(t_rows - 1, 0, text(2, 9), A_NORM);
	t_refresh();
	for (i = vmf_seek(srcv, k0); i < srcv->nent && (unsigned char)srcv->ent[i].key[0] == f.rectype; i++) {
		if (srcv->ent[i].flags & VMF_DELETED)
			continue;
		loadrec(i);
		ntest++;
		if (!selected())
			continue;
		nsel++;
		if (npick == maxpick) {
			maxpick = maxpick ? maxpick * 2 : 64;
			picks = (struct pick *)xrealloc(picks, maxpick * sizeof *picks);
		}
		picks[npick].i = i;
		picks[npick].sk = NULL;
		if (f.sortdir) {
			char sk[600], v[300];
			int j;

			sk[0] = 0;
			for (j = 0; f.sortby[j]; j++) {
				struct dfield *d = bychar((unsigned char)f.sortby[j]);

				if (d && d->off >= 0) {
					fieldval(d, v, sizeof v);
					if (strlen(sk) + strlen(v) < sizeof sk - 1)
						strcat(sk, v);
				}
			}
			picks[npick].sk = xstrdup(sk);
		}
		npick++;
		if (!shutup && (ntest % 16) == 0) {
			char b[60];

			sprintf(b, "%s%.10s", text(2, 11), srcv->ent[i].key);
			t_clearrow(t_rows - 1);
			t_puts(t_rows - 1, 0, b, A_NORM);
			t_refresh();
		}
		if (in_peekterm() == CTL('C'))
			break;
	}
	if (f.sortdir && npick > 1) {
		t_clearrow(t_rows - 1);
		t_puts(t_rows - 1, 0, text(2, 10), A_NORM);
		t_refresh();
		sortsign = f.sortdir;
		qsort(picks, npick, sizeof *picks, pickcmp_dir);
	}
	if (kill) {
		char keys[KEYLEN];

		for (i = 0; i < npick; i++) {
			memcpy(keys, srcv->ent[picks[i].i].key, KEYLEN);
			vmf_delete(srcv, keys);
		}
		goto done;
	}
	/* the output */
	outview = f.view;
	curfmt = &f;
	stopped = 0;
	pageno = P.pagestart - 1;
	memset(tot, 0, sizeof tot);
	memset(totdec, 0, sizeof totdec);
	if (!outview) {
		if (print_open(&pr, &fp, &ispipe) < 0)
			goto done;
		print_begin();
		if (f.pcc[0])
			print_pcc(f.pcc);
		print_spacing(f.spacing);
		pd_lpp = f.lpp;
		pd_total = f.psize;
		pd_lm = f.margin;
		if (print_pausing()) {
			t_clearrow(t_rows - 1);
			print_setpause(ui_yesno(t_rows - 1, 0, text(2, 15)) == 1);
		}
	}
	outlpp = outview ? t_rows - 1 : (f.lpp > 0 ? f.lpp : 60);
	if (!mailshot)
		headings();
	for (i = 0; i < npick && !stopped; i++) {
		int nl = 0, j;

		loadrec(picks[i].i);
		nsel = i + 1;
		format_lines(f.det, f.ndet, lines, &nl, 0);
		if (mailshot) {
			struct doc *d = mail_merge(md, lines, nl);

			if (outview) {
				pageno++;
				t_clear();
				outrow = 0;
				for (j = 0; j < d->n && !stopped; j++) {
					char b[MAXW + 1];
					int n = d->l[j].len < MAXW ? d->l[j].len : MAXW;

					if (d->l[j].flags & LF_RULER)
						continue;
					memcpy(b, d->l[j].s, n);
					b[n] = 0;
					if (outrow >= t_rows - 1) {
						if (viewpause() < 0)
							stopped = 1;
						t_clear();
						outrow = 0;
					}
					t_puts(outrow++, 0, b, A_NORM);
				}
				if (!stopped && viewpause() < 0)
					stopped = 1;
			} else {
				print_docpart(d);
				if (print_stopped())
					stopped = 1;
			}
			doc_free(d);
		} else {
			/* a record's lines stay on one page */
			rows = nl;
			if (outlines + rows > outlpp && outlines > nheadl) {
				pageend();
				if (stopped)
					break;
				headings();
			}
			for (j = 0; j < nl; j++)
				emit(lines[j]);
			add_totals(&f);
		}
		if (f.upos > 0 && f.ustr[0]) {
			/* /U: the record marked */
			char *u = f.ustr, d6[8];
			long o = f.upos, L = (long)strlen(u);
			char *data;
			long dl;
			unsigned char *sq;
			long sn;

			if (u[0] == '#') {
				date_yymmdd(d6);
				u = d6;
				L = 6;
			}
			dl = rlen - KEYLEN;
			if (o + L > dl)
				dl = o + L;
			data = (char *)xrealloc(NULL, dl + 1);
			memset(data, ' ', dl);
			memcpy(data, rbuf + KEYLEN, rlen - KEYLEN);
			memcpy(data + o, u, L);
			squash(data, dl, &sq, &sn, '_');
			{
				char key[KEYLEN + 1];

				memcpy(key, rbuf, KEYLEN);
				key[KEYLEN] = 0;
				vmf_put(srcv, key, sq, sn);
			}
			xfree(sq);
			xfree(data);
		}
		if (in_peekterm() == CTL('C'))
			stopped = 1;
	}
	/* the totals, the count, the criteria */
	if (!mailshot && !stopped) {
		char b[MAXW + 1];

		if (f.totals[0]) {
			if (f.totff && !outview) {
				pageend();
				outlines = 0;
			}
			emit(text(2, 13));
			for (i = 0; f.totals[i] && i < 32; i++) {
				struct dfield *d = bychar((unsigned char)f.totals[i]);

				if (d == NULL)
					continue;
				sprintf(b, "%c %-8s %.*f", f.totals[i], d->name, totdec[i], tot[i]);
				if (I.decpt && I.decpt != '.') {
					char *dot = strrchr(b, '.');

					if (dot)
						*dot = (char)I.decpt;
				}
				emit(b);
			}
		}
	}
	if (!stopped && (I.nsel || I.showsel)) {
		char b[MAXW + 1];

		if (I.nsel) {
			sprintf(b, "%s%04ld / %04ld", text(2, 12), (long)npick, ntest);
			if (outview || mailshot) {
				t_clearrow(t_rows - 2);
				t_puts(t_rows - 2, 0, b, A_NORM);
			} else
				emit(b);
		}
		if (I.showsel && sel[0] && (outview || mailshot)) {
			sprintf(b, "%s%.60s", text(2, 6), sel);
			t_clearrow(t_rows - 1);
			t_puts(t_rows - 1, 0, b, A_NORM);
		} else if (I.showsel && sel[0]) {
			sprintf(b, "%s%.60s", text(2, 6), sel);
			emit(b);
		}
	}
	if (!outview) {
		if (!mailshot && !stopped)
			print_ff(1);
		print_end();
		print_close(pr, fp, ispipe);
	} else if (!stopped && !mailshot)
		ui_pause(text(0, 26));
	else if (!stopped)
		ui_pause(text(0, 26));
done:
	for (i = 0; i < npick; i++)
		xfree(picks[i].sk);
	npick = 0;
	xfree(rbuf);
	rbuf = NULL;
	if (md)
		doc_free(md);
	fmt_free(&f);
	if (srcv && srcv == sysvmf)
		vmf_save(sysvmf);
	return 0;
}
