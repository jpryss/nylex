/*
 * lexconv - nylex's documents to and from the formats of other word
 * processors: RTF, Word (.docx), OpenDocument (.odt), Markdown (.md),
 * HTML (export only) and plain text in UTF-8 (.txt).
 *
 *	lexconv [-n | -a] [-w width] [-j] from to
 *
 * The formats go by the files' extensions; a file of none of those is a
 * LEX document (.BRV, .LTR ...).  nylex runs it from its menus with /H
 * (a document in, another out) and the record *=CONV.
 *
 * A LEX document is lines, laid out on rulers.  Out of it come the
 * paragraphs again: lines that were broken by word wrap (the next word
 * did not fit before M) or justified (J ruler, out to M) are joined;
 * a line centred or put against M keeps that; a line of columns (pieces
 * of text two or more spaces apart: a table, tab stops) goes out with
 * tab stops at its columns (in Markdown, HTML and text as it is, in a
 * fixed font).  .T and .F become the page header and footer (the page
 * number of (SIDA: or (PAGE: a page number field), .P a page break, .N
 * a footnote; the other print time lines are left out.  Attributes 1
 * and 4 are underlining, 2 and 5 bold, 3 italic.
 *
 * Into a LEX document the paragraphs are laid out on an F ruler (J for
 * justified ones) of the width given (69 columns, as *.BRV's), with an
 * empty line between paragraphs, headings bold (the first level also
 * centred), lists, quotations and tables as lines of their own.
 *
 * -n: the LEX document is in 7-bit Swedish ([ \ ] { | } are the
 * letters A-umlaut ... a-ring), as nylex's Swedish system file has it;
 * -a: it is ASCII and Latin-1 (the English one).  The default is
 * $LEXNAT (1 or 0, which nylex sets), else -n.
 *
 * Word and OpenDocument files are zip files: they are written stored,
 * and read with the inflate here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long uch;

#define A_UL	(1 | 8)
#define A_B	(2 | 16)
#define A_I	4

static int	national = 1;		/* 7-bit Swedish LEX documents */
static int	width = 69;		/* the ruler's M */
static int	justify;		/* -j: paragraphs justified */

static void
die(const char *a, const char *b)
{
	fprintf(stderr, "lexconv: %s%s\n", a, b ? b : "");
	exit(1);
}

static void *
xrealloc(void *p, size_t n)
{
	/* old C libraries take neither realloc(NULL) nor xfree(NULL) */
	p = p ? realloc(p, n ? n : 1) : malloc(n ? n : 1);
	if (p == NULL)
		die("out of memory", NULL);
	return p;
}

static void
xfree(void *p)
{
	if (p)
		free(p);
}

/* Text: what a document is made of between the formats */

enum { K_PARA = 'P', K_LINE = 'L', K_EMPTY = 'B', K_PAGE = 'E', K_HEAD = 'H', K_NOTE = 'N', K_RULE = 'R' };

struct blk {
	int	kind;		/* K_ */
	int	align;		/* 'L' 'C' 'R' 'J' */
	int	level;		/* a heading's */
	int	indent;		/* columns before the text */
	int	hang;		/* columns of a list item's mark */
	int	n, cap;
	uch	*c;		/* the characters (Unicode) */
	unsigned char *a;	/* their attributes */
};

static struct blk	*B;
static int		nb, capb;
static struct blk	header, footer;		/* .T .F */

static struct blk *
newblk(int kind)
{
	struct blk *b;

	if (nb >= capb) {
		capb = capb * 2 + 64;
		B = (struct blk *)xrealloc(B, capb * sizeof *B);
	}
	b = &B[nb++];
	memset(b, 0, sizeof *b);
	b->kind = kind;
	b->align = 'L';
	return b;
}

static void
addc(struct blk *b, uch c, int a)
{
	if (b->n + 1 >= b->cap) {
		b->cap = b->cap * 2 + 64;
		b->c = (uch *)xrealloc(b->c, b->cap * sizeof(uch));
		b->a = (unsigned char *)xrealloc(b->a, b->cap);
	}
	b->c[b->n] = c;
	b->a[b->n++] = (unsigned char)a;
}

static void
adds(struct blk *b, const char *s, int a)
{
	while (*s)
		addc(b, (unsigned char)*s++, a);
}

/* the last block, if it is a paragraph still open */
static void
trimblk(struct blk *b)
{
	while (b->n > 0 && b->c[b->n - 1] == ' ')
		b->n--;
}

/* Characters */

/* a byte of a LEX document as Unicode */
static uch
fromlex(int c)
{
	static const char s7[] = "[\\]{|}";
	static const unsigned short l1[] = { 0xc4, 0xd6, 0xc5, 0xe4, 0xf6, 0xe5 };
	const char *p;

	if (national && c && (p = strchr(s7, c)) != NULL)
		return l1[p - s7];
	return (uch)(c & 0xff);
}

/* Unicode as a byte of a LEX document */
static int
tolex(uch c)
{
	static const struct { uch u; const char *s; } approx[] = {
		{ 0x2018, "'" }, { 0x2019, "'" }, { 0x201a, "'" }, { 0x201c, "\"" }, { 0x201d, "\"" },
		{ 0x201e, "\"" }, { 0x2013, "-" }, { 0x2014, "-" }, { 0x2212, "-" }, { 0x2022, "*" },
		{ 0x2026, "." }, { 0x00a0, " " }, { 0x20ac, "E" }, { 0x2122, "T" }, { 0x2010, "-" },
		{ 0x2011, "-" }, { 0x2192, ">" }, { 0x2190, "<" }, { 0x00ad, "" }, { 0, NULL }
	};
	int i;

	if (national) {
		switch (c) {
		case 0xc4: return '[';
		case 0xd6: return '\\';
		case 0xc5: return ']';
		case 0xe4: return '{';
		case 0xf6: return '|';
		case 0xe5: return '}';
		case '[': case '{': return '(';
		case ']': case '}': return ')';
		case '\\': return '/';
		case '|': return '!';
		}
	}
	if (c == '\t')
		return ' ';
	if (c >= 32 && c < 127)
		return (int)c;
	if (c >= 0xa0 && c <= 0xff && c != 0xad)
		return (int)c;
	for (i = 0; approx[i].s; i++)
		if (approx[i].u == c)
			return approx[i].s[0] ? approx[i].s[0] : -1;
	if (c < 32)
		return -1;
	return '?';
}

/* the next character of UTF-8 text at *p (Latin-1 where it is not
   UTF-8) */
static uch
getutf8(const unsigned char **p, const unsigned char *end)
{
	const unsigned char *s = *p;
	uch c = *s;
	int n = 0, i;

	if (c >= 0xc2 && c <= 0xdf)
		n = 1;
	else if (c >= 0xe0 && c <= 0xef)
		n = 2;
	else if (c >= 0xf0 && c <= 0xf4)
		n = 3;
	if (s + n >= end && n) {
		*p = s + 1;
		return c;
	}
	for (i = 1; i <= n; i++)
		if ((s[i] & 0xc0) != 0x80) {
			*p = s + 1;
			return c;
		}
	if (n == 1)
		c = ((c & 0x1f) << 6) | (s[1] & 0x3f);
	else if (n == 2)
		c = ((c & 0x0f) << 12) | ((uch)(s[1] & 0x3f) << 6) | (s[2] & 0x3f);
	else if (n == 3)
		c = ((c & 0x07) << 18) | ((uch)(s[1] & 0x3f) << 12) | ((uch)(s[2] & 0x3f) << 6) | (s[3] & 0x3f);
	*p = s + 1 + n;
	return c;
}

/* Windows code page 1252 */
static uch
cp1252(int c)
{
	static const unsigned short hi[32] = {
		0x20ac, 0x81, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8d, 0x017d, 0x8f,
		0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x9d, 0x017e, 0x0178
	};

	c &= 0xff;
	return c >= 0x80 && c < 0xa0 ? hi[c - 0x80] : (uch)c;
}

/* Files */

static unsigned char *
readfile(const char *path, long *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b = NULL;
	long n = 0, cap = 0;
	size_t k;

	if (f == NULL)
		die("cannot read ", path);
	for (;;) {
		if (n + 4096 > cap) {
			cap = cap * 2 + 65536;
			b = (unsigned char *)xrealloc(b, cap + 1);
		}
		k = fread(b + n, 1, 4096, f);
		if (k == 0)
			break;
		n += (long)k;
	}
	fclose(f);
	b[n] = 0;
	*len = n;
	return b;
}

/* A LEX document into blocks */

struct lline {
	unsigned char	*s, *a;
	int		len;
};

static struct lline	*LL;
static int		nll;

static int
lex_attr(int c)
{
	if (c == '_')
		return 1;
	if (c >= 'A' && c <= 'O')
		return (c - 'A' + 1) << 1;
	if (c >= 'a' && c <= 'o')
		return 1 | ((c - 'a' + 1) << 1);
	return 0;
}

static void
lex_load(const char *path)
{
	long len, i = 0;
	unsigned char *b = readfile(path, &len);

	while (i < len) {
		long e = i, k;
		struct lline *l;

		while (e < len && b[e] != '\n')
			e++;
		LL = (struct lline *)xrealloc(LL, (nll + 1) * sizeof *LL);
		l = &LL[nll++];
		l->len = (int)(e - i);
		if (l->len > 0 && b[e - 1] == '\r')
			l->len--;
		l->s = b + i;
		l->a = (unsigned char *)xrealloc(NULL, l->len + 1);
		memset(l->a, 0, l->len + 1);
		i = e + 1;
		if (l->len > 0 && l->s[l->len - 1] == 255) {
			/* the attributes in the next line */
			long f = i;

			l->len--;
			while (f < len && b[f] != '\n')
				f++;
			for (k = 0; k < l->len && i + k < f; k++)
				l->a[k] = (unsigned char)lex_attr(b[i + k]);
			i = f + 1;
		}
		/* tabs, should there be any: to the next eighth column */
		for (k = 0; k < l->len; k++)
			if (l->s[k] == '\t')
				l->s[k] = ' ';
	}
}

static int
isruler(const struct lline *l)
{
	int i, dots = 0, marks = 0;

	if (l->len < 70)
		return 0;
	for (i = 0; i < l->len; i++)
		if (l->s[i] == '.')
			dots++;
		else if (strchr("CFJMLRTH#,", l->s[i]) && l->s[i])
			marks++;
		else
			return 0;
	return marks > 0 && dots >= l->len / 3;
}

static int
isctl(const struct lline *l)
{
	return l->len >= 2 && l->s[0] == '.' && l->s[1] >= 'A' && l->s[1] <= 'Z';
}

static int
lead(const struct lline *l)
{
	int i;

	for (i = 0; i < l->len && l->s[i] == ' '; i++)
		;
	return i;
}

static int
lend(const struct lline *l)
{
	int e = l->len;

	while (e > 0 && l->s[e - 1] == ' ')
		e--;
	return e;
}

/* pieces of text two or more spaces apart */
static int
pieces(const struct lline *l)
{
	int i, n = 0, b = lead(l), e = lend(l);

	for (i = b; i < e; i++)
		if (l->s[i] != ' ' && (i == b || (l->s[i - 1] == ' ' && i >= 2 && l->s[i - 2] == ' ')))
			n++;
	return n;
}

static int
firstword(const struct lline *l)
{
	int i = lead(l), n = 0;

	while (i + n < l->len && l->s[i + n] != ' ')
		n++;
	return n;
}

/* the print characters of .C: 3 pound, 5 a space, 8 a hard hyphen,
   11 a footnote's mark; the others are left out */
static char	pcc[13] = "............";
static int	nmarks;

/* text into a block: from..to of a line, runs of spaces as one */
static void
addlex(struct blk *b, const struct lline *l, int from, int to, int collapse)
{
	int i, k;

	for (i = from; i < to; i++) {
		int c = l->s[i];
		const char *p = c != '.' ? strchr(pcc, c) : NULL;

		if (p && c) {
			char num[24];

			switch (k = (int)(p - pcc)) {
			case 3:
				addc(b, 0xa3, l->a[i]);
				break;
			case 5:
				addc(b, ' ', l->a[i]);
				break;
			case 8:
				addc(b, '-', l->a[i]);
				break;
			case 11:
				sprintf(num, "%d)", ++nmarks);
				adds(b, num, l->a[i]);
				break;
			}
			continue;
		}
		if (collapse && c == ' ' && b->n > 0 && b->c[b->n - 1] == ' ')
			continue;
		addc(b, fromlex(c), l->a[i]);
	}
}

/* .T and .F: the text, with the page number as U+E000 */
static void
setmargin(struct blk *b, const struct lline *l)
{
	int i = 2;

	if (b->n)
		return;
	if (i < l->len && l->s[i] == ' ')
		i++;
	for (; i < l->len; i++) {
		if (l->len - i >= 6 && (memcmp(l->s + i, "(SIDA:", 6) == 0 || memcmp(l->s + i, "(PAGE:", 6) == 0)) {
			addc(b, 0xe000, 0);
			i += 5;
			continue;
		}
		addc(b, fromlex(l->s[i]), l->a[i]);
	}
	trimblk(b);
}

static void
lex_blocks(void)
{
	int i, kind = 'F', left = 0, right = width;

	for (i = 0; i < nll; i++) {
		struct lline *l = &LL[i];
		struct blk *b;
		int ld, e, j, joined;

		if (isruler(l)) {
			int k;

			kind = 0;
			left = 0;
			right = -1;
			for (k = 0; k < l->len; k++) {
				if (!kind && strchr("FJC", l->s[k])) {
					kind = l->s[k];
					left = k;
				}
				if (l->s[k] == 'M' && right < 0)
					right = k;
			}
			if (right < 0)
				right = l->len;
			continue;
		}
		if (isctl(l)) {
			switch (l->s[1]) {
			case 'C':
				{
					int k, o = l->len > 2 && l->s[2] == ' ' ? 3 : 2;

					for (k = 0; k < 12; k++)
						pcc[k] = (char)(o + k < l->len && l->s[o + k] != ' ' ? l->s[o + k] : '.');
				}
				break;
			case 'T':
				setmargin(&header, l);
				break;
			case 'F':
				setmargin(&footer, l);
				break;
			case 'P':
				newblk(K_PAGE);
				break;
			case 'N':
				b = newblk(K_NOTE);
				addlex(b, l, l->len > 2 && l->s[2] == ' ' ? 3 : 2, l->len, 1);
				trimblk(b);
				break;
			}
			continue;
		}
		ld = lead(l);
		e = lend(l);
		if (e == 0) {
			newblk(K_EMPTY);
			continue;
		}
		if (pieces(l) > 1 && !(kind == 'J' && e == right)) {
			/* columns */
			b = newblk(K_LINE);
			addlex(b, l, 0, e, 0);
			continue;
		}
		b = newblk(K_PARA);
		b->indent = ld;
		addlex(b, l, ld, e, 1);
		joined = 0;
		j = i;
		while (kind != 'C' && j + 1 < nll) {
			struct lline *c = &LL[j], *n = &LL[j + 1];
			int ce = lend(c);

			if (isruler(n) || isctl(n) || lend(n) == 0 || (pieces(n) > 1 && !(kind == 'J' && lend(n) == right)))
				break;
			if (lead(n) != left || ce > right || ld - left > 8)
				break;
			if (!((kind == 'J' && ce == right) || (right > 0 && ce + 1 + firstword(n) > right)))
				break;
			addc(b, ' ', 0);
			addlex(b, n, lead(n), lend(n), 1);
			if (!joined && ld > lead(n)) {
				/* the first line indented */
				b->hang = ld - lead(n);
				b->indent = lead(n);
			}
			joined = 1;
			j++;
		}
		trimblk(b);
		if (joined)
			b->align = kind == 'J' ? 'J' : 'L';
		else if (kind == 'C')
			b->align = 'C';
		else if (ld > left + 1 && (ld + e - 1) - (left + right - 1) <= 2 && (left + right - 1) - (ld + e - 1) <= 2)
			b->align = 'C';
		else if (e == right && ld > left + (right - left) / 3)
			b->align = 'R';
		if (b->align == 'C' || b->align == 'R')
			b->indent = 0;
		if (b->indent > 50)
			b->indent = 50;
		i = j;
	}
}

/* Output: into memory, then a file or a member of a zip file */

struct buf {
	char	*s;
	long	n, cap;
};

static void
bput(struct buf *b, const char *s, long n)
{
	if (b->n + n + 1 > b->cap) {
		b->cap = (b->n + n + 1) * 2 + 4096;
		b->s = (char *)xrealloc(b->s, b->cap);
	}
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = 0;
}

static void
bs(struct buf *b, const char *s)
{
	bput(b, s, (long)strlen(s));
}

static void
bc(struct buf *b, int c)
{
	char ch = (char)c;

	bput(b, &ch, 1);
}

static void
butf8(struct buf *b, uch c)
{
	char t[4];
	int n = 0;

	if (c < 0x80)
		t[n++] = (char)c;
	else if (c < 0x800) {
		t[n++] = (char)(0xc0 | (c >> 6));
		t[n++] = (char)(0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		t[n++] = (char)(0xe0 | (c >> 12));
		t[n++] = (char)(0x80 | ((c >> 6) & 0x3f));
		t[n++] = (char)(0x80 | (c & 0x3f));
	} else {
		t[n++] = (char)(0xf0 | (c >> 18));
		t[n++] = (char)(0x80 | ((c >> 12) & 0x3f));
		t[n++] = (char)(0x80 | ((c >> 6) & 0x3f));
		t[n++] = (char)(0x80 | (c & 0x3f));
	}
	bput(b, t, n);
}

static void
bnum(struct buf *b, const char *fmt, long v)
{
	char t[64];

	sprintf(t, fmt, v);
	bs(b, t);
}

/* XML text */
static void
bxml(struct buf *b, uch c)
{
	if (c == '&')
		bs(b, "&amp;");
	else if (c == '<')
		bs(b, "&lt;");
	else if (c == '>')
		bs(b, "&gt;");
	else if (c == '"')
		bs(b, "&quot;");
	else if (c >= 32 || c == '\t' || c == '\n')
		butf8(b, c);
}

static void
writebuf(const char *path, const struct buf *b)
{
	FILE *f = fopen(path, "wb");

	if (f == NULL || fwrite(b->s, 1, b->n, f) != (size_t)b->n || fclose(f) != 0)
		die("cannot write ", path);
}

/* zip, stored */

static unsigned long	crctab[256];

static unsigned long
crc32(const unsigned char *s, long n)
{
	unsigned long c = 0xffffffffUL;
	long i;

	if (!crctab[1]) {
		unsigned long k, v;
		int j;

		for (k = 0; k < 256; k++) {
			v = k;
			for (j = 0; j < 8; j++)
				v = v & 1 ? 0xedb88320UL ^ (v >> 1) : v >> 1;
			crctab[k] = v;
		}
	}
	for (i = 0; i < n; i++)
		c = crctab[(c ^ s[i]) & 0xff] ^ (c >> 8);
	return (c ^ 0xffffffffUL) & 0xffffffffUL;
}

static void
le(struct buf *b, unsigned long v, int n)
{
	int i;

	for (i = 0; i < n; i++)
		bc(b, (int)((v >> (8 * i)) & 0xff));
}

struct zmember {
	const char	*name;
	struct buf	*data;
};

static void
zipwrite(const char *path, struct zmember *m, int n)
{
	struct buf z, cd;
	long *off = (long *)xrealloc(NULL, n * sizeof(long));
	int i;

	memset(&z, 0, sizeof z);
	memset(&cd, 0, sizeof cd);
	for (i = 0; i < n; i++) {
		unsigned long crc = crc32((unsigned char *)m[i].data->s, m[i].data->n);
		long nl = (long)strlen(m[i].name);

		off[i] = z.n;
		le(&z, 0x04034b50UL, 4);
		le(&z, 20, 2);			/* version */
		le(&z, 0x0800, 2);		/* UTF-8 names */
		le(&z, 0, 2);			/* stored */
		le(&z, 0, 2);			/* time */
		le(&z, 0x5021, 2);		/* 2020-01-01 */
		le(&z, crc, 4);
		le(&z, m[i].data->n, 4);
		le(&z, m[i].data->n, 4);
		le(&z, nl, 2);
		le(&z, 0, 2);
		bs(&z, m[i].name);
		bput(&z, m[i].data->s, m[i].data->n);
		le(&cd, 0x02014b50UL, 4);
		le(&cd, 20, 2);
		le(&cd, 20, 2);
		le(&cd, 0x0800, 2);
		le(&cd, 0, 2);
		le(&cd, 0, 2);
		le(&cd, 0x5021, 2);
		le(&cd, crc, 4);
		le(&cd, m[i].data->n, 4);
		le(&cd, m[i].data->n, 4);
		le(&cd, nl, 2);
		le(&cd, 0, 2);
		le(&cd, 0, 2);
		le(&cd, 0, 2);
		le(&cd, 0, 2);
		le(&cd, 0, 4);
		le(&cd, off[i], 4);
		bs(&cd, m[i].name);
	}
	{
		long cdoff = z.n;

		bput(&z, cd.s, cd.n);
		le(&z, 0x06054b50UL, 4);
		le(&z, 0, 2);
		le(&z, 0, 2);
		le(&z, n, 2);
		le(&z, n, 2);
		le(&z, cd.n, 4);
		le(&z, cdoff, 4);
		le(&z, 0, 2);
	}
	writebuf(path, &z);
	xfree(off);
}

/* The pieces of a line of columns */

#define MAXP	40

struct piece {
	int	b, e;		/* columns: from, after */
	int	num;		/* a number: it ends at its column */
};

static int
linepieces(const struct blk *b, struct piece *p)
{
	int i = 0, n = 0;

	while (i < b->n && n < MAXP) {
		int j, digit = 0, num = 1;

		while (i < b->n && b->c[i] == ' ')
			i++;
		if (i >= b->n)
			break;
		j = i;
		while (j < b->n && !(b->c[j] == ' ' && (j + 1 >= b->n || b->c[j + 1] == ' ')))
			j++;
		for (p[n].b = i; i < j; i++) {
			uch c = b->c[i];

			if (c >= '0' && c <= '9')
				digit = 1;
			else if (!strchr(".,-+% ", (int)(c < 128 ? c : 'x')))
				num = 0;
		}
		p[n].e = j;
		p[n].num = digit && num;
		n++;
	}
	return n;
}

/* runs of one attribute: calls f for each */
static int
nextrun(const struct blk *b, int i)
{
	int j = i;

	while (j < b->n && b->a[j] == b->a[i] && b->c[j] != 0xe000 && b->c[j] != 0x2028)
		j++;
	return j == i ? i + 1 : j;
}

/* RTF */

static void
rtfch(struct buf *o, uch c)
{
	if (c == '\\' || c == '{' || c == '}') {
		bc(o, '\\');
		bc(o, (int)c);
	} else if (c == 0xe000)
		bs(o, "{\\field{\\*\\fldinst PAGE}{\\fldrslt 1}}");
	else if (c == 0x2028)
		bs(o, "\\line ");
	else if (c == '\t')
		bs(o, "\\tab ");
	else if (c < 128)
		bc(o, (int)c);
	else if (c < 256)
		bnum(o, "\\'%02lx", (long)c);
	else
		bnum(o, "\\u%ld?", c < 32768 ? (long)c : (long)c - 65536);
}

static void
rtfattr(struct buf *o, int was, int a)
{
	if ((was & A_B) && !(a & A_B))
		bs(o, "\\b0");
	if ((was & A_I) && !(a & A_I))
		bs(o, "\\i0");
	if ((was & A_UL) && !(a & A_UL))
		bs(o, "\\ulnone");
	if (!(was & A_B) && (a & A_B))
		bs(o, "\\b");
	if (!(was & A_I) && (a & A_I))
		bs(o, "\\i");
	if (!(was & A_UL) && (a & A_UL))
		bs(o, "\\ul");
	if (was != a)
		bc(o, ' ');
}

static void
rtftext(struct buf *o, const struct blk *b, int from, int to)
{
	int i, at = 0;

	for (i = from; i < to; i++) {
		if (b->a[i] != at) {
			rtfattr(o, at, b->a[i]);
			at = b->a[i];
		}
		rtfch(o, b->c[i]);
	}
	if (at)
		rtfattr(o, at, 0);
}

#define TW	144		/* a column: a tenth of an inch, in twips */

static void
rtf_line(struct buf *o, const struct blk *b)
{
	struct piece p[MAXP];
	int n = linepieces(b, p), k;

	bs(o, "\\pard");
	for (k = 0; k < n; k++)
		if (p[k].num)
			bnum(o, "\\tqr\\tx%ld", (long)p[k].e * TW);
		else if (p[k].b > 0)
			bnum(o, "\\tx%ld", (long)p[k].b * TW);
	bc(o, ' ');
	for (k = 0; k < n; k++) {
		if (k > 0 || p[k].b > 0 || p[k].num)
			bs(o, "\\tab ");
		rtftext(o, b, p[k].b, p[k].e);
	}
	bs(o, "\\par\n");
}

static void
rtf_write(const char *path)
{
	struct buf o;
	int i, note = 0;

	memset(&o, 0, sizeof o);
	bs(&o, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl{\\f0\\froman\\fcharset0 Times New Roman;}{\\f1\\fmodern\\fcharset0 Courier New;}}\n");
	bs(&o, "\\paperw11906\\paperh16838\\margl1134\\margr1134\\margt1134\\margb1134\\fs24\n");
	if (header.n) {
		bs(&o, "{\\header\\pard\\plain\\fs24 ");
		rtftext(&o, &header, 0, header.n);
		bs(&o, "\\par}\n");
	}
	if (footer.n) {
		bs(&o, "{\\footer\\pard\\plain\\fs24 ");
		rtftext(&o, &footer, 0, footer.n);
		bs(&o, "\\par}\n");
	}
	for (i = 0; i < nb; i++) {
		struct blk *b = &B[i];

		switch (b->kind) {
		case K_EMPTY:
			bs(&o, "\\pard\\par\n");
			break;
		case K_PAGE:
			bs(&o, "\\page\n");
			break;
		case K_RULE:
			bs(&o, "\\pard\\brdrb\\brdrs\\brdrw10\\brsp20\\par\n");
			break;
		case K_LINE:
			rtf_line(&o, b);
			break;
		case K_NOTE:
			bnum(&o, "\\pard\\fs18 %ld) ", (long)++note);
			rtftext(&o, b, 0, b->n);
			bs(&o, "\\fs24\\par\n");
			break;
		case K_HEAD:
			bs(&o, b->level <= 1 ? "\\pard\\qc\\sb240\\sa120\\b\\fs32 " : "\\pard\\sb240\\sa120\\b\\fs28 ");
			rtftext(&o, b, 0, b->n);
			bs(&o, "\\b0\\fs24\\par\n");
			break;
		default:
			bs(&o, "\\pard");
			bs(&o, b->align == 'C' ? "\\qc" : b->align == 'R' ? "\\qr" : b->align == 'J' ? "\\qj" : "\\ql");
			if (b->indent)
				bnum(&o, "\\li%ld", (long)b->indent * TW);
			if (b->hang)
				bnum(&o, "\\fi%ld", (long)b->hang * TW);
			bc(&o, ' ');
			rtftext(&o, b, 0, b->n);
			bs(&o, "\\par\n");
		}
	}
	bs(&o, "}\n");
	writebuf(path, &o);
}

/* Word (Office Open XML) */

static int	runsz;		/* a size for the runs (half points), or 0 */

static void
docx_runs(struct buf *o, const struct blk *b, int from, int to)
{
	int i, j;

	for (i = from; i < to; i = j) {
		int a = b->a[i];

		j = nextrun(b, i);
		if (j > to)
			j = to;
		if (b->c[i] == 0xe000) {
			bs(o, "<w:fldSimple w:instr=\" PAGE \"><w:r><w:t>1</w:t></w:r></w:fldSimple>");
			continue;
		}
		if (b->c[i] == 0x2028) {
			bs(o, "<w:r><w:br/></w:r>");
			continue;
		}
		bs(o, "<w:r>");
		if (a || runsz) {
			bs(o, "<w:rPr>");
			if (a & A_B)
				bs(o, "<w:b/>");
			if (a & A_I)
				bs(o, "<w:i/>");
			if (a & A_UL)
				bs(o, "<w:u w:val=\"single\"/>");
			if (runsz)
				bnum(o, "<w:sz w:val=\"%ld\"/>", (long)runsz);
			bs(o, "</w:rPr>");
		}
		bs(o, "<w:t xml:space=\"preserve\">");
		for (; i < j; i++)
			bxml(o, b->c[i]);
		bs(o, "</w:t></w:r>");
	}
}

static void
docx_para(struct buf *o, const struct blk *b, const char *ppr)
{
	bs(o, "<w:p>");
	if (ppr && *ppr) {
		bs(o, "<w:pPr>");
		bs(o, ppr);
		bs(o, "</w:pPr>");
	}
	docx_runs(o, b, 0, b->n);
	bs(o, "</w:p>\n");
}

static const char docx_ns[] = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
	"xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"";

static void
docx_write(const char *path)
{
	struct buf doc, ct, rels, drels, sty, hdr, ftr;
	struct zmember m[8];
	int i, nm = 0, note = 0;
	char ppr[2000];

	memset(&doc, 0, sizeof doc);
	memset(&ct, 0, sizeof ct);
	memset(&rels, 0, sizeof rels);
	memset(&drels, 0, sizeof drels);
	memset(&sty, 0, sizeof sty);
	memset(&hdr, 0, sizeof hdr);
	memset(&ftr, 0, sizeof ftr);
	bs(&doc, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document ");
	bs(&doc, docx_ns);
	bs(&doc, "><w:body>\n");
	for (i = 0; i < nb; i++) {
		struct blk *b = &B[i];

		ppr[0] = 0;
		switch (b->kind) {
		case K_EMPTY:
			bs(&doc, "<w:p/>\n");
			break;
		case K_PAGE:
			bs(&doc, "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>\n");
			break;
		case K_RULE:
			bs(&doc, "<w:p><w:pPr><w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"auto\"/></w:pBdr></w:pPr></w:p>\n");
			break;
		case K_LINE:
			{
				struct piece p[MAXP];
				int n = linepieces(b, p), k;

				strcpy(ppr, "<w:tabs>");
				for (k = 0; k < n && strlen(ppr) < sizeof ppr - 80; k++)
					if (p[k].num)
						sprintf(ppr + strlen(ppr), "<w:tab w:val=\"right\" w:pos=\"%d\"/>", p[k].e * TW);
					else if (p[k].b > 0)
						sprintf(ppr + strlen(ppr), "<w:tab w:val=\"left\" w:pos=\"%d\"/>", p[k].b * TW);
				strcat(ppr, "</w:tabs>");
				bs(&doc, "<w:p><w:pPr>");
				bs(&doc, ppr);
				bs(&doc, "</w:pPr>");
				for (k = 0; k < n; k++) {
					if (k > 0 || p[k].b > 0 || p[k].num)
						bs(&doc, "<w:r><w:tab/></w:r>");
					docx_runs(&doc, b, p[k].b, p[k].e);
				}
				bs(&doc, "</w:p>\n");
			}
			break;
		case K_NOTE:
			{
				struct blk t;
				int k;

				memset(&t, 0, sizeof t);
				sprintf(ppr, "%d) ", ++note);
				adds(&t, ppr, 0);
				for (k = 0; k < b->n; k++)
					addc(&t, b->c[k], b->a[k]);
				runsz = 18;
				docx_para(&doc, &t, NULL);
				runsz = 0;
				xfree(t.c);
				xfree(t.a);
			}
			break;
		case K_HEAD:
			{
				int k;

				for (k = 0; k < b->n; k++)
					b->a[k] |= 2;
				sprintf(ppr, "<w:keepNext/><w:spacing w:before=\"240\" w:after=\"120\"/>%s",
				    b->level <= 1 ? "<w:jc w:val=\"center\"/>" : "");
				bs(&doc, "<w:p><w:pPr>");
				bs(&doc, ppr);
				bs(&doc, "</w:pPr>");
				runsz = b->level <= 1 ? 32 : b->level == 2 ? 28 : 24;
				docx_runs(&doc, b, 0, b->n);
				runsz = 0;
				bs(&doc, "</w:p>\n");
			}
			break;
		default:
			if (b->align != 'L')
				sprintf(ppr, "<w:jc w:val=\"%s\"/>", b->align == 'C' ? "center" : b->align == 'R' ? "right" : "both");
			if (b->indent || b->hang)
				sprintf(ppr + strlen(ppr), "<w:ind w:left=\"%d\" w:firstLine=\"%d\"/>", b->indent * TW,
				    b->hang > 0 ? b->hang * TW : 0);
			docx_para(&doc, b, ppr);
		}
	}
	bs(&doc, "<w:sectPr>");
	if (header.n)
		bs(&doc, "<w:headerReference w:type=\"default\" r:id=\"rIdH\"/>");
	if (footer.n)
		bs(&doc, "<w:footerReference w:type=\"default\" r:id=\"rIdF\"/>");
	bs(&doc, "<w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" ");
	bs(&doc, "w:left=\"1134\" w:header=\"567\" w:footer=\"567\" w:gutter=\"0\"/></w:sectPr>\n</w:body></w:document>\n");

	bs(&ct, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
	bs(&ct, "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">");
	bs(&ct, "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>");
	bs(&ct, "<Default Extension=\"xml\" ContentType=\"application/xml\"/>");
	bs(&ct, "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>");
	bs(&ct, "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>");
	if (header.n)
		bs(&ct, "<Override PartName=\"/word/header1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>");
	if (footer.n)
		bs(&ct, "<Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>");
	bs(&ct, "</Types>\n");
	bs(&rels, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
	bs(&rels, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
	bs(&rels, "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>");
	bs(&rels, "</Relationships>\n");
	bs(&drels, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
	bs(&drels, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
	bs(&drels, "<Relationship Id=\"rIdS\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");
	if (header.n)
		bs(&drels, "<Relationship Id=\"rIdH\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\" Target=\"header1.xml\"/>");
	if (footer.n)
		bs(&drels, "<Relationship Id=\"rIdF\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer1.xml\"/>");
	bs(&drels, "</Relationships>\n");
	bs(&sty, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:styles ");
	bs(&sty, docx_ns);
	bs(&sty, "><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Times New Roman\" w:hAnsi=\"Times New Roman\" ");
	bs(&sty, "w:cs=\"Times New Roman\"/><w:sz w:val=\"24\"/><w:lang w:val=\"");
	bs(&sty, national ? "sv-SE" : "en-GB");
	bs(&sty, "\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>");
	bs(&sty, "</w:pPr></w:pPrDefault></w:docDefaults></w:styles>\n");
	m[nm].name = "[Content_Types].xml";
	m[nm++].data = &ct;
	m[nm].name = "_rels/.rels";
	m[nm++].data = &rels;
	m[nm].name = "word/document.xml";
	m[nm++].data = &doc;
	m[nm].name = "word/_rels/document.xml.rels";
	m[nm++].data = &drels;
	m[nm].name = "word/styles.xml";
	m[nm++].data = &sty;
	if (header.n) {
		bs(&hdr, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:hdr ");
		bs(&hdr, docx_ns);
		bs(&hdr, ">");
		docx_para(&hdr, &header, NULL);
		bs(&hdr, "</w:hdr>\n");
		m[nm].name = "word/header1.xml";
		m[nm++].data = &hdr;
	}
	if (footer.n) {
		bs(&ftr, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:ftr ");
		bs(&ftr, docx_ns);
		bs(&ftr, ">");
		docx_para(&ftr, &footer, NULL);
		bs(&ftr, "</w:ftr>\n");
		m[nm].name = "word/footer1.xml";
		m[nm++].data = &ftr;
	}
	zipwrite(path, m, nm);
}

/* OpenDocument text */

static const char *
odt_tstyle(int a)
{
	static const char *n[8] = { "", "Tb", "Ti", "Tbi", "Tu", "Tbu", "Tiu", "Tbiu" };

	return n[((a & A_B) ? 1 : 0) | ((a & A_I) ? 2 : 0) | ((a & A_UL) ? 4 : 0)];
}

static void
odt_runs(struct buf *o, const struct blk *b, int from, int to)
{
	int i, j;

	for (i = from; i < to; i = j) {
		const char *t = odt_tstyle(b->a[i]);

		j = nextrun(b, i);
		if (j > to)
			j = to;
		if (b->c[i] == 0xe000) {
			bs(o, "<text:page-number text:select-page=\"current\">1</text:page-number>");
			continue;
		}
		if (b->c[i] == 0x2028) {
			bs(o, "<text:line-break/>");
			continue;
		}
		if (*t) {
			bs(o, "<text:span text:style-name=\"");
			bs(o, t);
			bs(o, "\">");
		}
		for (; i < j; i++) {
			if (b->c[i] == ' ' && (i == from || b->c[i - 1] == ' ')) {
				int k = i;

				while (k < j && b->c[k] == ' ')
					k++;
				bnum(o, "<text:s text:c=\"%ld\"/>", (long)(k - i));
				i = k - 1;
			} else
				bxml(o, b->c[i]);
		}
		if (*t)
			bs(o, "</text:span>");
	}
}

static const char odt_ns[] = "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
	"xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
	"xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
	"xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" office:version=\"1.2\"";

#define CM(cols)	((cols) * 0.254)

static void
odt_write(const char *path)
{
	struct buf mt, man, con, sty, body, auto_;
	struct zmember m[4];
	int i, k, note = 0;
	char t[400];

	memset(&mt, 0, sizeof mt);
	memset(&man, 0, sizeof man);
	memset(&con, 0, sizeof con);
	memset(&sty, 0, sizeof sty);
	memset(&body, 0, sizeof body);
	memset(&auto_, 0, sizeof auto_);
	bs(&mt, "application/vnd.oasis.opendocument.text");
	bs(&man, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">");
	bs(&man, "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>");
	bs(&man, "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>");
	bs(&man, "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>");
	bs(&man, "</manifest:manifest>\n");
	/* the styles of the text and of the kinds of paragraph */
	for (k = 1; k < 8; k++) {
		sprintf(t, "<style:style style:name=\"%s\" style:family=\"text\"><style:text-properties%s%s%s/></style:style>",
		    odt_tstyle((k & 1 ? 2 : 0) | (k & 2 ? 4 : 0) | (k & 4 ? 1 : 0)),
		    k & 1 ? " fo:font-weight=\"bold\"" : "", k & 2 ? " fo:font-style=\"italic\"" : "",
		    k & 4 ? " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"" : "");
		bs(&auto_, t);
	}
	bs(&auto_, "<style:style style:name=\"PL\" style:family=\"paragraph\"/>");
	bs(&auto_, "<style:style style:name=\"PC\" style:family=\"paragraph\"><style:paragraph-properties fo:text-align=\"center\"/></style:style>");
	bs(&auto_, "<style:style style:name=\"PR\" style:family=\"paragraph\"><style:paragraph-properties fo:text-align=\"end\"/></style:style>");
	bs(&auto_, "<style:style style:name=\"PJ\" style:family=\"paragraph\"><style:paragraph-properties fo:text-align=\"justify\"/></style:style>");
	bs(&auto_, "<style:style style:name=\"PE\" style:family=\"paragraph\"><style:paragraph-properties fo:break-before=\"page\"/></style:style>");
	bs(&auto_, "<style:style style:name=\"PN\" style:family=\"paragraph\"><style:text-properties fo:font-size=\"9pt\"/></style:style>");
	bs(&auto_, "<style:style style:name=\"PX\" style:family=\"paragraph\"><style:paragraph-properties fo:border-bottom=\"0.5pt solid #000000\" fo:padding-bottom=\"0.1cm\"/></style:style>");
	bs(&auto_, "<style:style style:name=\"H1\" style:family=\"paragraph\"><style:paragraph-properties fo:text-align=\"center\" fo:margin-top=\"0.4cm\" fo:margin-bottom=\"0.2cm\" fo:keep-with-next=\"always\"/><style:text-properties fo:font-size=\"16pt\" fo:font-weight=\"bold\"/></style:style>");
	bs(&auto_, "<style:style style:name=\"H2\" style:family=\"paragraph\"><style:paragraph-properties fo:margin-top=\"0.4cm\" fo:margin-bottom=\"0.2cm\" fo:keep-with-next=\"always\"/><style:text-properties fo:font-size=\"14pt\" fo:font-weight=\"bold\"/></style:style>");
	bs(&auto_, "<style:style style:name=\"H3\" style:family=\"paragraph\"><style:paragraph-properties fo:margin-top=\"0.3cm\" fo:keep-with-next=\"always\"/><style:text-properties fo:font-weight=\"bold\"/></style:style>");
	for (i = 0; i < nb; i++) {
		struct blk *b = &B[i];
		const char *ps = b->align == 'C' ? "PC" : b->align == 'R' ? "PR" : b->align == 'J' ? "PJ" : "PL";

		switch (b->kind) {
		case K_EMPTY:
			bs(&body, "<text:p text:style-name=\"PL\"/>\n");
			break;
		case K_PAGE:
			bs(&body, "<text:p text:style-name=\"PE\"/>\n");
			break;
		case K_RULE:
			bs(&body, "<text:p text:style-name=\"PX\"/>\n");
			break;
		case K_NOTE:
			bnum(&body, "<text:p text:style-name=\"PN\">%ld) ", (long)++note);
			odt_runs(&body, b, 0, b->n);
			bs(&body, "</text:p>\n");
			break;
		case K_HEAD:
			sprintf(t, "<text:h text:style-name=\"H%d\" text:outline-level=\"%d\">", b->level < 1 ? 1 : b->level > 3 ? 3 : b->level, b->level);
			bs(&body, t);
			odt_runs(&body, b, 0, b->n);
			bs(&body, "</text:h>\n");
			break;
		case K_LINE:
			{
				struct piece p[MAXP];
				int n = linepieces(b, p);

				sprintf(t, "<style:style style:name=\"P%d\" style:family=\"paragraph\"><style:paragraph-properties><style:tab-stops>", i);
				bs(&auto_, t);
				for (k = 0; k < n; k++)
					if (p[k].num || p[k].b > 0) {
						sprintf(t, "<style:tab-stop style:position=\"%.3fcm\"%s/>", CM(p[k].num ? p[k].e : p[k].b),
						    p[k].num ? " style:type=\"right\"" : "");
						bs(&auto_, t);
					}
				bs(&auto_, "</style:tab-stops></style:paragraph-properties></style:style>");
				bnum(&body, "<text:p text:style-name=\"P%ld\">", (long)i);
				for (k = 0; k < n; k++) {
					if (k > 0 || p[k].b > 0 || p[k].num)
						bs(&body, "<text:tab/>");
					odt_runs(&body, b, p[k].b, p[k].e);
				}
				bs(&body, "</text:p>\n");
			}
			break;
		default:
			if (b->indent || b->hang) {
				sprintf(t, "<style:style style:name=\"P%d\" style:family=\"paragraph\" style:parent-style-name=\"Standard\">"
				    "<style:paragraph-properties fo:margin-left=\"%.3fcm\" fo:text-indent=\"%.3fcm\"%s/></style:style>",
				    i, CM(b->indent), CM(b->hang), b->align == 'J' ? " fo:text-align=\"justify\"" : "");
				bs(&auto_, t);
				bnum(&body, "<text:p text:style-name=\"P%ld\">", (long)i);
			} else {
				bs(&body, "<text:p text:style-name=\"");
				bs(&body, ps);
				bs(&body, "\">");
			}
			odt_runs(&body, b, 0, b->n);
			bs(&body, "</text:p>\n");
		}
	}
	bs(&con, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content ");
	bs(&con, odt_ns);
	bs(&con, "><office:automatic-styles>");
	bput(&con, auto_.s ? auto_.s : "", auto_.n);
	bs(&con, "</office:automatic-styles><office:body><office:text>\n");
	bput(&con, body.s ? body.s : "", body.n);
	bs(&con, "</office:text></office:body></office:document-content>\n");

	bs(&sty, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles ");
	bs(&sty, odt_ns);
	bs(&sty, "><office:styles><style:default-style style:family=\"paragraph\"><style:text-properties ");
	bs(&sty, "fo:font-family=\"'Times New Roman'\" style:font-family-generic=\"roman\" fo:font-size=\"12pt\" fo:language=\"");
	bs(&sty, national ? "sv\" fo:country=\"SE\"" : "en\" fo:country=\"GB\"");
	bs(&sty, "/></style:default-style><style:style style:name=\"Standard\" style:family=\"paragraph\"/></office:styles>");
	bs(&sty, "<office:automatic-styles><style:page-layout style:name=\"pm1\"><style:page-layout-properties fo:page-width=\"21cm\" ");
	bs(&sty, "fo:page-height=\"29.7cm\" fo:margin-top=\"1.5cm\" fo:margin-bottom=\"1.5cm\" fo:margin-left=\"2cm\" fo:margin-right=\"2cm\"/>");
	bs(&sty, "</style:page-layout></office:automatic-styles><office:master-styles>");
	bs(&sty, "<style:master-page style:name=\"Standard\" style:page-layout-name=\"pm1\">");
	if (header.n) {
		bs(&sty, "<style:header><text:p>");
		odt_runs(&sty, &header, 0, header.n);
		bs(&sty, "</text:p></style:header>");
	}
	if (footer.n) {
		bs(&sty, "<style:footer><text:p>");
		odt_runs(&sty, &footer, 0, footer.n);
		bs(&sty, "</text:p></style:footer>");
	}
	bs(&sty, "</style:master-page></office:master-styles></office:document-styles>\n");
	m[0].name = "mimetype";
	m[0].data = &mt;
	m[1].name = "META-INF/manifest.xml";
	m[1].data = &man;
	m[2].name = "content.xml";
	m[2].data = &con;
	m[3].name = "styles.xml";
	m[3].data = &sty;
	zipwrite(path, m, 4);
}

/* HTML */

static void
html_runs(struct buf *o, const struct blk *b, int from, int to)
{
	int i, at = 0;

	for (i = from; i <= to; i++) {
		int a = i < to ? b->a[i] : 0;

		if (a != at) {
			if ((at & A_UL) && !(a & A_UL))
				bs(o, "</u>");
			if ((at & A_I) && !(a & A_I))
				bs(o, "</i>");
			if ((at & A_B) && !(a & A_B))
				bs(o, "</b>");
			if (!(at & A_B) && (a & A_B))
				bs(o, "<b>");
			if (!(at & A_I) && (a & A_I))
				bs(o, "<i>");
			if (!(at & A_UL) && (a & A_UL))
				bs(o, "<u>");
			at = a;
		}
		if (i == to)
			break;
		if (b->c[i] == 0x2028)
			bs(o, "<br>\n");
		else
			bxml(o, b->c[i]);
	}
}

static void
html_write(const char *path, const char *title)
{
	struct buf o;
	int i, inpre = 0, note = 0;

	memset(&o, 0, sizeof o);
	bs(&o, "<!DOCTYPE html>\n<html lang=\"");
	bs(&o, national ? "sv" : "en");
	bs(&o, "\">\n<head>\n<meta charset=\"utf-8\">\n<title>");
	while (*title)
		bxml(&o, (unsigned char)*title++);
	bs(&o, "</title>\n<style>\nbody { font-family: Georgia, 'Times New Roman', serif; max-width: 42em; margin: 2em auto; padding: 0 1em; line-height: 1.4; }\n");
	bs(&o, "pre { font-family: 'Courier New', monospace; } p.note { font-size: smaller; } .page { page-break-after: always; border: 0; }\n</style>\n</head>\n<body>\n");
	for (i = 0; i < nb; i++) {
		struct blk *b = &B[i];

		if (inpre && b->kind != K_LINE) {
			bs(&o, "</pre>\n");
			inpre = 0;
		}
		switch (b->kind) {
		case K_EMPTY:
			break;
		case K_PAGE:
			bs(&o, "<hr class=\"page\">\n");
			break;
		case K_RULE:
			bs(&o, "<hr>\n");
			break;
		case K_LINE:
			if (!inpre)
				bs(&o, "<pre>");
			inpre = 1;
			html_runs(&o, b, 0, b->n);
			bc(&o, '\n');
			break;
		case K_NOTE:
			bnum(&o, "<p class=\"note\">%ld) ", (long)++note);
			html_runs(&o, b, 0, b->n);
			bs(&o, "</p>\n");
			break;
		case K_HEAD:
			bnum(&o, "<h%ld>", (long)(b->level < 1 ? 1 : b->level > 6 ? 6 : b->level));
			html_runs(&o, b, 0, b->n);
			bnum(&o, "</h%ld>\n", (long)(b->level < 1 ? 1 : b->level > 6 ? 6 : b->level));
			break;
		default:
			bs(&o, b->align == 'C' ? "<p style=\"text-align: center\">" : b->align == 'R' ? "<p style=\"text-align: right\">" :
			    b->align == 'J' ? "<p style=\"text-align: justify\">" : "<p>");
			html_runs(&o, b, 0, b->n);
			bs(&o, "</p>\n");
		}
	}
	if (inpre)
		bs(&o, "</pre>\n");
	bs(&o, "</body>\n</html>\n");
	writebuf(path, &o);
}

/* Markdown */

static void
md_char(struct buf *o, uch c, int atstart)
{
	if (c == 0x2028) {
		bs(o, "  \n");
		return;
	}
	if (strchr("\\*_`[]<>", (int)(c < 128 ? c : 'x')) && c < 128)
		bc(o, '\\');
	else if (atstart && c < 128 && strchr("#+-|", (int)c))
		bc(o, '\\');
	butf8(o, c);
}

static void
md_runs(struct buf *o, const struct blk *b, int from, int to)
{
	int i, j;

	for (i = from; i < to; i = j) {
		int a = b->a[i] & (A_B | A_I | A_UL), s, e;

		for (j = i; j < to && (b->a[j] & (A_B | A_I | A_UL)) == a && b->c[j] != 0x2028; j++)
			;
		if (j == i) {
			md_char(o, b->c[i], i == from);
			j = i + 1;
			continue;
		}
		/* the marks go round the text, not its spaces */
		for (s = i; s < j && b->c[s] == ' '; s++)
			bc(o, ' ');
		for (e = j; e > s && b->c[e - 1] == ' '; e--)
			;
		if (s < e) {
			if (a & A_UL)
				bs(o, "<u>");
			if (a & A_B)
				bs(o, "**");
			if (a & A_I)
				bc(o, '*');
			for (; s < e; s++)
				md_char(o, b->c[s], s == from);
			if (a & A_I)
				bc(o, '*');
			if (a & A_B)
				bs(o, "**");
			if (a & A_UL)
				bs(o, "</u>");
		}
		for (; e < j; e++)
			bc(o, ' ');
	}
}

static void
md_write(const char *path)
{
	struct buf o;
	int i, inpre = 0, note = 0, blank = 1;

	memset(&o, 0, sizeof o);
	for (i = 0; i < nb; i++) {
		struct blk *b = &B[i];

		if (inpre && b->kind != K_LINE) {
			bs(&o, "```\n");
			inpre = 0;
			blank = 0;
		}
		if (b->kind != K_EMPTY && b->kind != K_LINE && !blank)
			bc(&o, '\n');
		switch (b->kind) {
		case K_EMPTY:
			if (!blank && !inpre)
				bc(&o, '\n');
			if (inpre)
				bc(&o, '\n');
			blank = !inpre;
			continue;
		case K_PAGE:
			bs(&o, "<div style=\"page-break-after: always\"></div>\n");
			break;
		case K_RULE:
			bs(&o, "---\n");
			break;
		case K_LINE:
			if (!inpre) {
				if (!blank)
					bc(&o, '\n');
				bs(&o, "```\n");
			}
			inpre = 1;
			{
				int k;

				for (k = 0; k < b->n; k++)
					butf8(&o, b->c[k]);
			}
			bc(&o, '\n');
			continue;
		case K_NOTE:
			bnum(&o, "*%ld)* ", (long)++note);
			md_runs(&o, b, 0, b->n);
			bc(&o, '\n');
			break;
		case K_HEAD:
			{
				int k;

				for (k = 0; k < b->level && k < 6; k++)
					bc(&o, '#');
				bc(&o, ' ');
				for (k = 0; k < b->n; k++)
					b->a[k] &= ~A_B;
				md_runs(&o, b, 0, b->n);
				bc(&o, '\n');
			}
			break;
		default:
			if (b->align == 'C' && b->n < 70) {
				int k, allb = 1;

				for (k = 0; k < b->n; k++)
					if (b->c[k] != ' ' && !(b->a[k] & A_B))
						allb = 0;
				if (allb) {
					/* a centred heading */
					for (k = 0; k < b->n; k++)
						b->a[k] &= ~A_B;
					bs(&o, "## ");
				}
			}
			md_runs(&o, b, 0, b->n);
			bc(&o, '\n');
		}
		blank = 0;
	}
	if (inpre)
		bs(&o, "```\n");
	writebuf(path, &o);
}

/* Plain text: the document's lines as they are, in UTF-8 */
static void
txt_write(const char *path)
{
	struct buf o;
	int i, k;

	memset(&o, 0, sizeof o);
	for (i = 0; i < nll; i++) {
		struct lline *l = &LL[i];

		if (isruler(l) || isctl(l))
			continue;
		for (k = 0; k < lend(l); k++)
			butf8(&o, fromlex(l->s[k]));
		bc(&o, '\n');
	}
	writebuf(path, &o);
}

/* Reading zip files: the members, inflated (RFC 1951) */

struct inf {
	const unsigned char	*in;
	long			n, pos;
	unsigned long		bitbuf;
	int			bitcnt;
	struct buf		*out;
	int			bad;
};

static int
ibits(struct inf *s, int need)
{
	unsigned long v;

	while (s->bitcnt < need) {
		if (s->pos >= s->n) {
			s->bad = 1;
			return 0;
		}
		s->bitbuf |= (unsigned long)s->in[s->pos++] << s->bitcnt;
		s->bitcnt += 8;
	}
	v = s->bitbuf & ((1UL << need) - 1);
	s->bitbuf >>= need;
	s->bitcnt -= need;
	return (int)v;
}

struct huff {
	short	count[16];
	short	symbol[320];
};

static void
huff_make(struct huff *h, const short *len, int n)
{
	short offs[16];
	int i;

	memset(h->count, 0, sizeof h->count);
	for (i = 0; i < n; i++)
		h->count[len[i]]++;
	h->count[0] = 0;
	offs[1] = 0;
	for (i = 1; i < 15; i++)
		offs[i + 1] = (short)(offs[i] + h->count[i]);
	for (i = 0; i < n; i++)
		if (len[i])
			h->symbol[offs[len[i]]++] = (short)i;
}

static int
huff_decode(struct inf *s, const struct huff *h)
{
	int len, code = 0, first = 0, index = 0;

	for (len = 1; len < 16; len++) {
		int count;

		code |= ibits(s, 1);
		count = h->count[len];
		if (code - count < first)
			return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
		if (s->bad)
			return -1;
	}
	s->bad = 1;
	return -1;
}

static void
inf_codes(struct inf *s, const struct huff *lc, const struct huff *dc)
{
	static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static const unsigned short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
	int sym;

	while (!s->bad) {
		sym = huff_decode(s, lc);
		if (sym < 0)
			return;
		if (sym < 256)
			bc(s->out, sym);
		else if (sym == 256)
			return;
		else {
			long len, dist, k;

			sym -= 257;
			if (sym >= 29) {
				s->bad = 1;
				return;
			}
			len = lbase[sym] + ibits(s, lext[sym]);
			sym = huff_decode(s, dc);
			if (sym < 0 || sym >= 30) {
				s->bad = 1;
				return;
			}
			dist = dbase[sym] + ibits(s, dext[sym]);
			if (dist > s->out->n) {
				s->bad = 1;
				return;
			}
			for (k = 0; k < len; k++)
				bc(s->out, s->out->s[s->out->n - dist]);
		}
	}
}

static int
inflate(const unsigned char *in, long n, struct buf *out)
{
	struct inf s;
	int last;

	memset(&s, 0, sizeof s);
	s.in = in;
	s.n = n;
	s.out = out;
	do {
		int type;

		last = ibits(&s, 1);
		type = ibits(&s, 2);
		if (type == 0) {
			long len;

			s.bitbuf = 0;
			s.bitcnt = 0;
			if (s.pos + 4 > s.n)
				return -1;
			len = s.in[s.pos] | (s.in[s.pos + 1] << 8);
			s.pos += 4;
			if (s.pos + len > s.n)
				return -1;
			bput(out, (const char *)s.in + s.pos, len);
			s.pos += len;
		} else if (type == 1) {
			static struct huff lc, dc;
			static int made;

			if (!made) {
				short l[288];
				int i;

				for (i = 0; i < 288; i++)
					l[i] = (short)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
				huff_make(&lc, l, 288);
				for (i = 0; i < 30; i++)
					l[i] = 5;
				huff_make(&dc, l, 30);
				made = 1;
			}
			inf_codes(&s, &lc, &dc);
		} else if (type == 2) {
			static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
			short l[320];
			struct huff lc, dc;
			int nlen = ibits(&s, 5) + 257, ndist = ibits(&s, 5) + 1, ncode = ibits(&s, 4) + 4, i;

			memset(l, 0, sizeof l);
			for (i = 0; i < ncode; i++)
				l[order[i]] = (short)ibits(&s, 3);
			huff_make(&lc, l, 19);
			for (i = 0; i < nlen + ndist && !s.bad;) {
				int sym = huff_decode(&s, &lc), rep = 0, v = 0;

				if (sym < 0)
					return -1;
				if (sym < 16) {
					l[i++] = (short)sym;
					continue;
				}
				if (sym == 16) {
					if (i == 0)
						return -1;
					v = l[i - 1];
					rep = 3 + ibits(&s, 2);
				} else if (sym == 17)
					rep = 3 + ibits(&s, 3);
				else
					rep = 11 + ibits(&s, 7);
				if (i + rep > nlen + ndist)
					return -1;
				while (rep--)
					l[i++] = (short)v;
			}
			huff_make(&lc, l, nlen);
			huff_make(&dc, l + nlen, ndist);
			inf_codes(&s, &lc, &dc);
		} else
			return -1;
		if (s.bad)
			return -1;
	} while (!last);
	return 0;
}

static unsigned long
rle(const unsigned char *p, int n)
{
	unsigned long v = 0;

	while (n--)
		v = (v << 8) | p[n];
	return v;
}

/* the member name of the zip file in z, or -1 */
static int
unzip(const unsigned char *z, long n, const char *name, struct buf *out)
{
	long e, cd, i;
	int k, cnt;

	for (e = n - 22; e >= 0 && e > n - 65600; e--)
		if (rle(z + e, 4) == 0x06054b50UL)
			break;
	if (e < 0 || e <= n - 65600)
		return -1;
	cnt = (int)rle(z + e + 10, 2);
	cd = (long)rle(z + e + 16, 4);
	for (i = cd, k = 0; k < cnt && i + 46 <= n; k++) {
		long nl = (long)rle(z + i + 28, 2), el = (long)rle(z + i + 30, 2), cl = (long)rle(z + i + 32, 2);
		int method = (int)rle(z + i + 10, 2);
		long csize = (long)rle(z + i + 20, 4), lho = (long)rle(z + i + 42, 4);

		if (rle(z + i, 4) != 0x02014b50UL)
			return -1;
		if ((long)strlen(name) == nl && memcmp(z + i + 46, name, nl) == 0) {
			long d;

			if (lho + 30 > n)
				return -1;
			d = lho + 30 + (long)rle(z + lho + 26, 2) + (long)rle(z + lho + 28, 2);
			if (d + csize > n)
				return -1;
			if (method == 0) {
				bput(out, (const char *)z + d, csize);
				return 0;
			}
			if (method == 8)
				return inflate(z + d, csize, out);
			return -1;
		}
		i += 46 + nl + el + cl;
	}
	return -1;
}

/* XML, as much as the word processors' files need */

struct xtok {
	int		type;		/* 'S' a start tag, 'E' an end tag, 'Z' both, 'T' text */
	char		name[64];
	const char	*at;		/* the attributes, the text */
	long		n;
};

static int
xnext(const char **pp, const char *e, struct xtok *t)
{
	const char *p = *pp;
	int k = 0;

	if (p >= e)
		return 0;
	if (*p != '<') {
		t->type = 'T';
		t->at = p;
		while (p < e && *p != '<')
			p++;
		t->n = p - t->at;
		*pp = p;
		return 1;
	}
	if (p + 4 <= e && memcmp(p, "<!--", 4) == 0) {
		for (p += 4; p + 3 <= e && memcmp(p, "-->", 3) != 0; p++)
			;
		*pp = p + 3 <= e ? p + 3 : e;
		return xnext(pp, e, t);
	}
	if (p + 9 <= e && memcmp(p, "<![CDATA[", 9) == 0) {
		t->type = 'T';
		t->at = p + 9;
		for (p += 9; p + 3 <= e && memcmp(p, "]]>", 3) != 0; p++)
			;
		t->n = p - t->at;
		*pp = p + 3 <= e ? p + 3 : e;
		return 1;
	}
	if (p[1] == '?' || p[1] == '!') {
		while (p < e && *p != '>')
			p++;
		*pp = p < e ? p + 1 : e;
		return xnext(pp, e, t);
	}
	p++;
	t->type = 'S';
	if (*p == '/') {
		t->type = 'E';
		p++;
	}
	while (p < e && *p != ' ' && *p != '>' && *p != '/' && *p != '\t' && *p != '\n' && *p != '\r') {
		if (k < 63)
			t->name[k++] = *p;
		p++;
	}
	t->name[k] = 0;
	t->at = p;
	while (p < e && *p != '>') {
		if (*p == '"' || *p == '\'') {
			char q = *p++;

			while (p < e && *p != q)
				p++;
		}
		if (p < e)
			p++;
	}
	t->n = p - t->at;
	if (t->n > 0 && p[-1] == '/' && t->type == 'S') {
		t->type = 'Z';
		t->n--;
	}
	*pp = p < e ? p + 1 : e;
	return 1;
}

/* the value of an attribute (raw), or NULL */
static const char *
xattr(const struct xtok *t, const char *name, char *v, int max)
{
	const char *p = t->at, *e = t->at + t->n;
	int nl = (int)strlen(name);

	while (p < e) {
		while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;
		if (e - p > nl && memcmp(p, name, nl) == 0 && p[nl] == '=') {
			char q;
			int k = 0;

			p += nl + 1;
			q = *p++;
			while (p < e && *p != q) {
				if (k < max - 1)
					v[k++] = *p;
				p++;
			}
			v[k] = 0;
			return v;
		}
		while (p < e && *p != '=' && *p != ' ')
			p++;
		if (p < e && *p == '=') {
			char q;

			p++;
			q = *p;
			if (q == '"' || q == '\'') {
				p++;
				while (p < e && *p != q)
					p++;
				p++;
			}
		}
	}
	return NULL;
}

static int
xis(const struct xtok *t, const char *name)
{
	return strcmp(t->name, name) == 0;
}

/* text with its entities, into a block */
static void
xtext(struct blk *b, const char *s, long n, int a)
{
	const unsigned char *p = (const unsigned char *)s, *e = p + n;

	while (p < e) {
		uch c;

		if (*p == '&') {
			const unsigned char *q = p + 1;

			while (q < e && *q != ';' && q - p < 12)
				q++;
			if (q < e && *q == ';') {
				long l = (long)(q - p - 1);

				c = 0;
				if (l > 1 && p[1] == '#')
					c = p[2] == 'x' || p[2] == 'X' ? strtoul((const char *)p + 3, NULL, 16) : strtoul((const char *)p + 2, NULL, 10);
				else if (l == 3 && memcmp(p + 1, "amp", 3) == 0)
					c = '&';
				else if (l == 2 && memcmp(p + 1, "lt", 2) == 0)
					c = '<';
				else if (l == 2 && memcmp(p + 1, "gt", 2) == 0)
					c = '>';
				else if (l == 4 && memcmp(p + 1, "quot", 4) == 0)
					c = '"';
				else if (l == 4 && memcmp(p + 1, "apos", 4) == 0)
					c = '\'';
				else if (l == 4 && memcmp(p + 1, "nbsp", 4) == 0)
					c = ' ';
				if (c) {
					addc(b, c, a);
					p = q + 1;
					continue;
				}
			}
		}
		c = getutf8(&p, e);
		if (c == '\n' || c == '\r' || c == '\t')
			c = ' ';
		addc(b, c, a);
	}
}

/* Tables read: the cells of each row, then lines of columns */

#define MAXCOL	16

struct trow {
	struct blk	cell[MAXCOL];
	int		ncell;
};

static struct trow	*TR;
static int		ntr;

static void
table_row(void)
{
	TR = (struct trow *)xrealloc(TR, (ntr + 1) * sizeof *TR);
	memset(&TR[ntr], 0, sizeof TR[ntr]);
	ntr++;
}

static struct blk *
table_cell(void)
{
	struct trow *r;

	if (ntr == 0)
		table_row();
	r = &TR[ntr - 1];
	if (r->ncell >= MAXCOL)
		return &r->cell[MAXCOL - 1];
	return &r->cell[r->ncell++];
}

static void
table_end(void)
{
	int w[MAXCOL], i, k, j, ncol = 0;

	memset(w, 0, sizeof w);
	for (i = 0; i < ntr; i++) {
		for (k = 0; k < TR[i].ncell; k++) {
			struct blk *c = &TR[i].cell[k];

			for (j = 0; j < c->n; j++)
				if (c->c[j] == 0x2028 || c->c[j] == '\t')
					c->c[j] = ' ';
			trimblk(c);
			while (c->n > 0 && c->c[0] == ' ') {
				memmove(c->c, c->c + 1, (c->n - 1) * sizeof(uch));
				memmove(c->a, c->a + 1, c->n - 1);
				c->n--;
			}
			if (c->n > 40)
				c->n = 40;
			if (c->n > w[k])
				w[k] = c->n;
		}
		if (TR[i].ncell > ncol)
			ncol = TR[i].ncell;
	}
	for (i = 0; i < ntr; i++) {
		struct blk *b = newblk(K_LINE);

		for (k = 0; k < TR[i].ncell; k++) {
			struct blk *c = &TR[i].cell[k];
			int num = c->n > 0, pad;

			for (j = 0; j < c->n; j++)
				if (!((c->c[j] >= '0' && c->c[j] <= '9') || (c->c[j] < 128 && strchr(".,-+% ", (int)c->c[j]))))
					num = 0;
			pad = w[k] - c->n;
			if (k > 0)
				adds(b, "  ", 0);
			if (num)
				while (pad-- > 0)
					addc(b, ' ', 0);
			for (j = 0; j < c->n; j++)
				addc(b, c->c[j], c->a[j]);
			if (!num && k + 1 < TR[i].ncell)
				while (pad-- > 0)
					addc(b, ' ', 0);
			xfree(c->c);
			xfree(c->a);
		}
		trimblk(b);
		if (b->n == 0)
			b->kind = K_EMPTY;
	}
	xfree(TR);
	TR = NULL;
	ntr = 0;
	(void)ncol;
}

/* a paragraph read: kept, or into the table's cell */
static struct blk	para;		/* the paragraph being read */
static int		pkind, palign, plevel, pindent, phang, ptable;
static int		ntabs, tabpos[32], tabright[32];	/* tab stops (columns) */
/* the paragraphs of a header or footer go into it rather than the text */
static struct blk	*pmargin;
static int		afterpage;	/* the paragraph that ends at a page break */

static void
tab_add(int col, int right)
{
	int i, k;

	if (ntabs >= 32 || col <= 0)
		return;
	for (i = 0; i < ntabs && tabpos[i] < col; i++)
		;
	if (i < ntabs && tabpos[i] == col)
		return;
	for (k = ntabs; k > i; k--) {
		tabpos[k] = tabpos[k - 1];
		tabright[k] = tabright[k - 1];
	}
	tabpos[i] = col;
	tabright[i] = right;
	ntabs++;
}

static void	para_end(void);

/* a page break in the text: the paragraph so far, then the break */
static void
page_break(void)
{
	if (para.n)
		para_end();
	if (!ptable && !pmargin)
		newblk(K_PAGE);
	afterpage = 1;
	xfree(para.c);
	xfree(para.a);
	memset(&para, 0, sizeof para);
}

static void
para_start(void)
{
	xfree(para.c);
	xfree(para.a);
	memset(&para, 0, sizeof para);
	pkind = K_PARA;
	palign = 'L';
	plevel = 0;
	pindent = phang = 0;
}

static void
para_end(void)
{
	struct blk *b;
	int i, tabs = 0;

	if (pmargin) {
		trimblk(&para);
		if (pmargin->n && para.n)
			adds(pmargin, "   ", 0);
		for (i = 0; i < para.n; i++)
			addc(pmargin, para.c[i] == '\t' ? ' ' : para.c[i], para.a[i]);
		return;
	}
	if (ptable) {
		struct blk *c;

		if (ntr == 0 || TR[ntr - 1].ncell == 0)
			c = table_cell();
		else
			c = &TR[ntr - 1].cell[TR[ntr - 1].ncell - 1];
		if (c->n && para.n)
			addc(c, ' ', 0);
		for (i = 0; i < para.n; i++)
			addc(c, para.c[i], para.a[i]);
		return;
	}
	trimblk(&para);
	for (i = 0; i < para.n; i++)
		if (para.c[i] == '\t')
			tabs++;
	if (tabs == 1 && pkind == K_PARA) {
		/* a mark and a tab: an item of a list */
		for (i = 0; i < para.n && i < 6 && para.c[i] != '\t'; i++)
			;
		if (i < 6 && i < para.n && para.c[i] == '\t') {
			int k;

			for (k = 0; k < i && para.c[k] == ' '; k++)
				;
			para.c[i] = ' ';
			memmove(para.c, para.c + k, (para.n - k) * sizeof(uch));
			memmove(para.a, para.a + k, para.n - k);
			para.n -= k;
			pindent = i - k + 1;
			phang = -(i - k + 1);
			tabs = 0;
		}
	}
	if (para.n == 0) {
		if (!afterpage)
			newblk(K_EMPTY);
		afterpage = 0;
		return;
	}
	afterpage = 0;
	if (pkind == K_LINE) {
		/* preformatted: a line for each line */
		int from = 0;

		for (i = 0; i <= para.n; i++)
			if (i == para.n || para.c[i] == 0x2028) {
				int k;

				b = newblk(K_LINE);
				for (k = from; k < i; k++)
					if (para.c[k] == '\t') {
						do
							addc(b, ' ', 0);
						while (b->n % 8);
					} else
						addc(b, para.c[k], para.a[k]);
				trimblk(b);
				if (b->n == 0)
					b->kind = K_EMPTY;
				from = i + 1;
			}
		return;
	}
	b = newblk(tabs ? K_LINE : pkind);
	b->align = palign;
	b->level = plevel;
	b->indent = pindent;
	b->hang = phang;
	if (tabs) {
		/* the text between the tabs at the tab stops (those not set:
		   every half inch) */
		int seg = 0;

		while (b->n < pindent)
			addc(b, ' ', 0);
		for (i = 0; i <= para.n; i++) {
			if (i < para.n && para.c[i] != '\t')
				continue;
			if (seg > 0 || i > seg) {
				int k, j, at, len = i - seg, right = 0;

				if (seg > 0) {
					/* the stop after where the text is */
					for (k = 0; k < ntabs && tabpos[k] <= b->n; k++)
						;
					if (k < ntabs) {
						at = tabpos[k];
						right = tabright[k];
					} else
						at = (b->n / 5 + 1) * 5;
					if (right)
						at = at - len > b->n ? at - len : b->n + 1;
					while (b->n < at)
						addc(b, ' ', 0);
				}
				for (j = seg; j < i; j++)
					addc(b, para.c[j], para.a[j]);
			}
			seg = i + 1;
		}
		return;
	}
	b->n = b->cap = para.n;
	b->c = para.c;
	b->a = para.a;
	memset(&para, 0, sizeof para);
}

/* an empty line between paragraphs, unless the document has its own */
static void
space_paragraphs(void)
{
	int i, adj = 0, gap = 0;
	struct blk *old;
	int nold;

	/* text after text, and text after an empty paragraph after text */
	for (i = 1; i < nb; i++)
		if (B[i].kind == K_PARA || B[i].kind == K_HEAD) {
			if (B[i - 1].kind == K_PARA || B[i - 1].kind == K_HEAD)
				adj++;
			else if (B[i - 1].kind == K_EMPTY && i >= 2 && B[i - 2].kind != K_EMPTY)
				gap++;
		}
	if (gap * 2 >= adj)
		return;
	old = B;
	nold = nb;
	B = NULL;
	nb = capb = 0;
	for (i = 0; i < nold; i++) {
		if (i > 0 && old[i].kind != K_PAGE && old[i - 1].kind != K_PAGE &&
		    !(old[i].kind == K_LINE && old[i - 1].kind == K_LINE) &&
		    !(old[i].hang < 0 && old[i - 1].hang < 0) && old[i].kind != K_EMPTY && old[i - 1].kind != K_EMPTY)
			newblk(K_EMPTY);
		*newblk(0) = old[i];
	}
	xfree(old);
}

/* Word */

static int
xbool(const struct xtok *t)
{
	char v[16];

	if (xattr(t, "w:val", v, sizeof v) == NULL)
		return 1;
	return !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "none") == 0 || strcmp(v, "off") == 0);
}

static void
docx_parse(const char *p, const char *e)
{
	struct xtok t;
	int inrun = 0, ra = 0, inp = 0, intext = 0, skip = 0, numbered = 0, intabs = 0;
	int fld = 0, fldpage = 0;	/* a field: 1 its instruction, 2 its result */
	char v[64];

	para_start();
	while (xnext(&p, e, &t)) {
		if (t.type == 'T') {
			if (intext && !skip && fld != 2)
				xtext(&para, t.at, t.n, ra);
			if (fld == 1 && strstr(t.at, "PAGE") && strstr(t.at, "PAGE") < t.at + t.n)
				fldpage = 1;
			continue;
		}
		if (skip) {
			if (t.type == 'E' && (xis(&t, "w:del") || xis(&t, "mc:Fallback") || xis(&t, "w:fldSimple")))
				skip = 0;
			continue;
		}
		if (t.type == 'S' && (xis(&t, "w:del") || xis(&t, "mc:Fallback"))) {
			skip = 1;
			continue;
		}
		if (xis(&t, "w:fldSimple") && (t.type == 'S' || t.type == 'Z')) {
			if (xattr(&t, "w:instr", v, sizeof v) && strstr(v, "PAGE")) {
				addc(&para, 0xe000, ra);
				skip = t.type == 'S';
			}
			continue;
		}
		if (xis(&t, "w:fldChar") && xattr(&t, "w:fldCharType", v, sizeof v)) {
			if (strcmp(v, "begin") == 0) {
				fld = 1;
				fldpage = 0;
			} else if (strcmp(v, "separate") == 0) {
				fld = 2;
				if (fldpage)
					addc(&para, 0xe000, ra);
				else
					fld = 0;	/* the result is the text */
			} else
				fld = 0;
			continue;
		}
		if (xis(&t, "w:p")) {
			if (t.type == 'S' || t.type == 'Z') {
				para_start();
				inp = 1;
				numbered = 0;
				ntabs = 0;
			}
			if (t.type == 'E' || t.type == 'Z') {
				if (numbered && para.n) {
					struct blk tmp = para;
					int k;

					memset(&para, 0, sizeof para);
					adds(&para, "- ", 0);
					for (k = 0; k < tmp.n; k++)
						addc(&para, tmp.c[k], tmp.a[k]);
					xfree(tmp.c);
					xfree(tmp.a);
					pindent = 2;
					phang = -2;
				}
				para_end();
				inp = 0;
			}
		} else if (xis(&t, "w:r")) {
			inrun = t.type == 'S';
			ra = 0;
		} else if (xis(&t, "w:t") || xis(&t, "w:instrText")) {
			intext = t.type == 'S' && xis(&t, "w:t");
			if (xis(&t, "w:instrText"))
				fld = t.type == 'S' && fld == 1 ? 1 : fld;
		} else if (xis(&t, "w:tabs")) {
			intabs = t.type == 'S';
		} else if (intabs && xis(&t, "w:tab")) {
			if (xattr(&t, "w:pos", v, sizeof v)) {
				int pos = (atoi(v) + TW / 2) / TW;
				char k[16];

				if (!xattr(&t, "w:val", k, sizeof k) || strcmp(k, "clear") != 0)
					tab_add(pos, strcmp(k, "right") == 0 || strcmp(k, "decimal") == 0 || strcmp(k, "end") == 0);
			}
		} else if (inrun && (t.type == 'S' || t.type == 'Z')) {
			if (xis(&t, "w:b"))
				ra = xbool(&t) ? ra | 2 : ra & ~A_B;
			else if (xis(&t, "w:i"))
				ra = xbool(&t) ? ra | 4 : ra & ~A_I;
			else if (xis(&t, "w:u"))
				ra = xbool(&t) ? ra | 1 : ra & ~A_UL;
			else if (xis(&t, "w:tab"))
				addc(&para, '\t', ra);
			else if (xis(&t, "w:br") || xis(&t, "w:cr")) {
				if (xattr(&t, "w:type", v, sizeof v) && strcmp(v, "page") == 0)
					page_break();
				else
					addc(&para, 0x2028, ra);
			} else if (xis(&t, "w:noBreakHyphen"))
				addc(&para, '-', ra);
		} else if (inp && (t.type == 'S' || t.type == 'Z')) {
			if (xis(&t, "w:jc") && xattr(&t, "w:val", v, sizeof v))
				palign = strcmp(v, "center") == 0 ? 'C' : strcmp(v, "right") == 0 || strcmp(v, "end") == 0 ? 'R' :
				    strcmp(v, "both") == 0 || strcmp(v, "distribute") == 0 ? 'J' : 'L';
			else if (xis(&t, "w:pStyle") && xattr(&t, "w:val", v, sizeof v)) {
				int lv = 0;

				if (strncmp(v, "Heading", 7) == 0 || strncmp(v, "Rubrik", 6) == 0 || strncmp(v, "heading", 7) == 0)
					lv = atoi(v + strcspn(v, "0123456789"));
				if (strcmp(v, "Title") == 0 || strcmp(v, "Rubrik") == 0)
					lv = 1;
				if (lv > 0) {
					pkind = K_HEAD;
					plevel = lv;
				} else if (strstr(v, "Code") || strstr(v, "Preformatted") || strstr(v, "Source"))
					pkind = K_LINE;
			} else if (xis(&t, "w:numPr"))
				numbered = 1;
			else if (xis(&t, "w:ind")) {
				if (xattr(&t, "w:left", v, sizeof v) || xattr(&t, "w:start", v, sizeof v))
					pindent = atoi(v) / TW;
				if (xattr(&t, "w:hanging", v, sizeof v))
					phang = -(atoi(v) / TW);
				else if (xattr(&t, "w:firstLine", v, sizeof v))
					phang = atoi(v) / TW;
			}
		}
		if (xis(&t, "w:tbl")) {
			if (t.type == 'S')
				ptable = 1;
			else if (t.type == 'E') {
				ptable = 0;
				table_end();
			}
		} else if (xis(&t, "w:tr") && t.type == 'S')
			table_row();
		else if (xis(&t, "w:tc") && t.type == 'S')
			table_cell();
	}
}

static void
docx_read(const char *path)
{
	long n;
	unsigned char *z = readfile(path, &n);
	struct buf x;
	int k;

	memset(&x, 0, sizeof x);
	if (unzip(z, n, "word/document.xml", &x) < 0)
		die("not a Word file (.docx): ", path);
	docx_parse(x.s, x.s + x.n);
	space_paragraphs();
	/* the first header and footer there are */
	for (k = 1; k <= 6; k++) {
		char name[40];
		struct blk *m = k <= 3 ? &header : &footer;

		if (m->n)
			continue;
		sprintf(name, k <= 3 ? "word/header%d.xml" : "word/footer%d.xml", k <= 3 ? k : k - 3);
		x.n = 0;
		if (unzip(z, n, name, &x) == 0) {
			pmargin = m;
			docx_parse(x.s, x.s + x.n);
			pmargin = NULL;
			trimblk(m);
		}
	}
	xfree(x.s);
	xfree(z);
}

/* OpenDocument */

struct ostyle {
	char	name[64];
	int	attr, align, page, head;
	int	ntab, tab[16], tabr[16];
	int	indent, hang;
};

/* a length of OpenDocument in columns (tenths of an inch) */
static int
ocols(const char *v)
{
	double d = atof(v);
	const char *u = v + strspn(v, "-0123456789.");

	if (strncmp(u, "cm", 2) == 0)
		d /= 0.254;
	else if (strncmp(u, "mm", 2) == 0)
		d /= 2.54;
	else if (strncmp(u, "in", 2) == 0)
		d *= 10;
	else if (strncmp(u, "pt", 2) == 0)
		d /= 7.2;
	return (int)(d < 0 ? d - 0.5 : d + 0.5);
}

static struct ostyle	*OS;
static int		nos;

static struct ostyle *
ostyle(const char *name)
{
	int i;

	for (i = nos - 1; i >= 0; i--)
		if (strcmp(OS[i].name, name) == 0)
			return &OS[i];
	return NULL;
}

static void
odt_styles(const char *p, const char *e)
{
	struct xtok t;
	struct ostyle *cur = NULL;
	char v[64];

	while (xnext(&p, e, &t)) {
		if (t.type == 'E') {
			if (xis(&t, "style:style"))
				cur = NULL;
			continue;
		}
		if (t.type == 'T')
			continue;
		if (xis(&t, "style:style") && xattr(&t, "style:name", v, sizeof v)) {
			OS = (struct ostyle *)xrealloc(OS, (nos + 1) * sizeof *OS);
			cur = &OS[nos++];
			memset(cur, 0, sizeof *cur);
			strcpy(cur->name, v);
			if (xattr(&t, "style:parent-style-name", v, sizeof v)) {
				struct ostyle *par = ostyle(v);

				if (par) {
					cur->attr = par->attr;
					cur->align = par->align;
					cur->head = par->head;
					cur->ntab = par->ntab;
					cur->indent = par->indent;
					cur->hang = par->hang;
					memcpy(cur->tab, par->tab, sizeof cur->tab);
					memcpy(cur->tabr, par->tabr, sizeof cur->tabr);
				}
				if (strncmp(v, "Heading_20_", 11) == 0)
					cur->head = atoi(v + 11);
				else if (strcmp(v, "Title") == 0)
					cur->head = 1;
			}
			if (t.type == 'Z')
				cur = NULL;
		} else if (cur && xis(&t, "style:text-properties")) {
			if (xattr(&t, "fo:font-weight", v, sizeof v))
				cur->attr = strcmp(v, "bold") == 0 || atoi(v) >= 600 ? cur->attr | 2 : cur->attr & ~A_B;
			if (xattr(&t, "fo:font-style", v, sizeof v))
				cur->attr = strcmp(v, "italic") == 0 || strcmp(v, "oblique") == 0 ? cur->attr | 4 : cur->attr & ~A_I;
			if (xattr(&t, "style:text-underline-style", v, sizeof v))
				cur->attr = strcmp(v, "none") != 0 ? cur->attr | 1 : cur->attr & ~A_UL;
		} else if (cur && xis(&t, "style:paragraph-properties")) {
			if (xattr(&t, "fo:text-align", v, sizeof v))
				cur->align = strcmp(v, "center") == 0 ? 'C' : strcmp(v, "end") == 0 || strcmp(v, "right") == 0 ? 'R' :
				    strcmp(v, "justify") == 0 ? 'J' : 'L';
			if (xattr(&t, "fo:break-before", v, sizeof v) && strcmp(v, "page") == 0)
				cur->page = 1;
			if (xattr(&t, "fo:margin-left", v, sizeof v))
				cur->indent = ocols(v);
			if (xattr(&t, "fo:text-indent", v, sizeof v))
				cur->hang = ocols(v);
		} else if (cur && xis(&t, "style:tab-stop") && cur->ntab < 16 && xattr(&t, "style:position", v, sizeof v)) {
			cur->tab[cur->ntab] = ocols(v);
			cur->tabr[cur->ntab] = xattr(&t, "style:type", v, sizeof v) && (strcmp(v, "right") == 0 || strcmp(v, "char") == 0);
			cur->ntab++;
		}
	}
}

/* the body of content.xml, or (margins) the header and footer of styles.xml */
static void
odt_parse(const char *p, const char *e, int margins)
{
	struct xtok t;
	int stack[64], sp = 0, inp = 0, skip = 0, item = 0, pageno = 0;
	char v[64];

	para_start();
	stack[0] = 0;
	while (xnext(&p, e, &t)) {
		if (margins) {
			if ((xis(&t, "style:header") || xis(&t, "style:footer")) && t.type == 'S') {
				struct blk *m = xis(&t, "style:header") ? &header : &footer;

				pmargin = m->n ? NULL : m;
				continue;
			}
			if ((xis(&t, "style:header") || xis(&t, "style:footer")) && t.type == 'E') {
				if (pmargin)
					trimblk(pmargin);
				pmargin = NULL;
				continue;
			}
			if (!pmargin)
				continue;
		}
		if (skip) {
			if (t.type == 'S' && (xis(&t, "text:note") || xis(&t, "office:annotation") || xis(&t, "draw:frame")))
				skip++;
			else if (t.type == 'E' && (xis(&t, "text:note") || xis(&t, "office:annotation") || xis(&t, "draw:frame")))
				skip--;
			continue;
		}
		if (t.type == 'T') {
			if (inp && !pageno)
				xtext(&para, t.at, t.n, stack[sp]);
			continue;
		}
		if (t.type == 'S' && (xis(&t, "text:note") || xis(&t, "office:annotation") || xis(&t, "draw:frame"))) {
			skip = 1;
			continue;
		}
		if (xis(&t, "text:page-number")) {
			if (t.type == 'S' || t.type == 'Z')
				addc(&para, 0xe000, stack[sp]);
			pageno = t.type == 'S';
			continue;
		}
		if (xis(&t, "text:p") || xis(&t, "text:h")) {
			if (t.type == 'S' || t.type == 'Z') {
				struct ostyle *st = xattr(&t, "text:style-name", v, sizeof v) ? ostyle(v) : NULL;

				para_start();
				inp = 1;
				sp = 0;
				stack[0] = st ? st->attr : 0;
				ntabs = 0;
				if (st) {
					int k;

					palign = st->align ? st->align : 'L';
					pindent = st->indent > 0 ? st->indent : 0;
					phang = st->hang;
					if (st->page && !ptable && !pmargin) {
						newblk(K_PAGE);
						afterpage = 1;
					}
					if (st->head) {
						pkind = K_HEAD;
						plevel = st->head;
					}
					for (k = 0; k < st->ntab; k++)
						tab_add(st->tab[k], st->tabr[k]);
				}
				if (xis(&t, "text:h")) {
					pkind = K_HEAD;
					plevel = xattr(&t, "text:outline-level", v, sizeof v) ? atoi(v) : 1;
				}
				if (item) {
					adds(&para, "- ", 0);
					pindent = 2;
					phang = -2;
					item = 0;
				}
			}
			if (t.type == 'E' || t.type == 'Z') {
				para_end();
				inp = 0;
			}
		} else if (xis(&t, "text:span")) {
			if (t.type == 'S' && sp < 63) {
				struct ostyle *st = xattr(&t, "text:style-name", v, sizeof v) ? ostyle(v) : NULL;

				stack[sp + 1] = stack[sp] | (st ? st->attr : 0);
				sp++;
			} else if (t.type == 'E' && sp > 0)
				sp--;
		} else if (inp && xis(&t, "text:s")) {
			long n;

			for (n = xattr(&t, "text:c", v, sizeof v) ? atol(v) : 1; n > 0; n--)
				addc(&para, ' ', stack[sp]);
		} else if (inp && xis(&t, "text:tab"))
			addc(&para, '\t', stack[sp]);
		else if (inp && xis(&t, "text:line-break"))
			addc(&para, 0x2028, stack[sp]);
		else if (xis(&t, "text:list-item") && t.type == 'S')
			item = 1;
		else if (xis(&t, "table:table")) {
			if (t.type == 'S')
				ptable = 1;
			else if (t.type == 'E') {
				ptable = 0;
				table_end();
			}
		} else if (xis(&t, "table:table-row") && t.type == 'S')
			table_row();
		else if (xis(&t, "table:table-cell") && (t.type == 'S' || t.type == 'Z'))
			table_cell();
	}
}

static void
odt_read(const char *path)
{
	long n;
	unsigned char *z = readfile(path, &n);
	struct buf x, sx;
	const char *p;

	memset(&x, 0, sizeof x);
	memset(&sx, 0, sizeof sx);
	if (unzip(z, n, "content.xml", &x) < 0)
		die("not an OpenDocument file (.odt): ", path);
	if (unzip(z, n, "styles.xml", &sx) == 0)
		odt_styles(sx.s, sx.s + sx.n);
	odt_styles(x.s, x.s + x.n);
	p = strstr(x.s, "<office:body");
	odt_parse(p ? p : x.s, x.s + x.n, 0);
	space_paragraphs();
	if (sx.s) {
		p = strstr(sx.s, "<office:master-styles");
		if (p)
			odt_parse(p, sx.s + sx.n, 1);
	}
	xfree(x.s);
	xfree(sx.s);
	xfree(z);
}

/* RTF */

static void
rtf_read(const char *path)
{
	long n, i = 0;
	unsigned char *s = readfile(path, &n);
	struct { int attr, skip, uc, dest; } st[128];
	int sp = 0, skipn = 0, anytext = 0, fldpage = 0, tqr = 0, intbl = 0, rowopen = 0;
	char fldbuf[64];
	int nfld = 0;
#define RTFTO(sp)	(st[sp].dest == 1 ? &header : st[sp].dest == 2 ? &footer : &para)

	if (n < 5 || memcmp(s, "{\\rtf", 5) != 0)
		die("not an RTF file: ", path);
	memset(st, 0, sizeof st);
	st[0].uc = 1;
	para_start();
	while (i < n) {
		int c = s[i];

		if (c == '{') {
			if (sp < 127) {
				st[sp + 1] = st[sp];
				sp++;
			}
			i++;
			if (i + 1 < n && s[i] == '\\' && s[i + 1] == '*')
				st[sp].skip = 1;
			continue;
		}
		if (c == '}') {
			if (sp > 0)
				sp--;
			i++;
			continue;
		}
		if (c == '\r' || c == '\n') {
			i++;
			continue;
		}
		if (c == '\\') {
			char w[32];
			int k = 0, neg = 0, has = 0;
			long v = 0;

			i++;
			if (i >= n)
				break;
			if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) {
				/* a control symbol */
				c = s[i++];
				if (c == '\'' && i + 2 <= n) {
					char h[3];

					h[0] = (char)s[i];
					h[1] = (char)s[i + 1];
					h[2] = 0;
					i += 2;
					if (skipn > 0)
						skipn--;
					else if (!st[sp].skip)
						addc(RTFTO(sp), cp1252((int)strtol(h, NULL, 16)), st[sp].attr);
				} else if (!st[sp].skip) {
					if (c == '\\' || c == '{' || c == '}')
						addc(RTFTO(sp), (uch)c, st[sp].attr);
					else if (c == '~')
						addc(RTFTO(sp), ' ', st[sp].attr);
					else if (c == '_')
						addc(RTFTO(sp), '-', st[sp].attr);
					else if (c == '\n' || c == '\r') {
						para_end();
						para_start();
					}
				}
				continue;
			}
			while (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) {
				if (k < 31)
					w[k++] = (char)s[i];
				i++;
			}
			w[k] = 0;
			if (i < n && s[i] == '-') {
				neg = 1;
				i++;
			}
			while (i < n && s[i] >= '0' && s[i] <= '9') {
				v = v * 10 + (s[i] - '0');
				has = 1;
				i++;
			}
			if (neg)
				v = -v;
			if (i < n && s[i] == ' ')
				i++;
			/* the first header and footer; the page number field */
			if (!st[sp].skip && (!strcmp(w, "header") || !strcmp(w, "headerr") || !strcmp(w, "footer") || !strcmp(w, "footerr"))) {
				struct blk *m = w[0] == 'h' ? &header : &footer;

				if (m->n == 0) {
					st[sp].dest = w[0] == 'h' ? 1 : 2;
					continue;
				}
			}
			if (!strcmp(w, "fldinst")) {
				st[sp].skip = 1;
				st[sp].dest = 3;
				nfld = 0;
				continue;
			}
			if (!strcmp(w, "fldrslt") && fldpage) {
				addc(RTFTO(sp - 1 >= 0 ? sp - 1 : 0), 0xe000, st[sp].attr);
				st[sp].skip = 1;
				fldpage = 0;
				continue;
			}
			if (!strcmp(w, "chpgn") && !st[sp].skip) {
				addc(RTFTO(sp), 0xe000, st[sp].attr);
				continue;
			}
			/* destinations left out */
			if (!strcmp(w, "fonttbl") || !strcmp(w, "colortbl") || !strcmp(w, "stylesheet") || !strcmp(w, "info") ||
			    !strcmp(w, "pict") || !strcmp(w, "object") || !strcmp(w, "header") || !strcmp(w, "footer") ||
			    !strcmp(w, "headerl") || !strcmp(w, "headerr") || !strcmp(w, "headerf") || !strcmp(w, "footerl") ||
			    !strcmp(w, "footerr") || !strcmp(w, "footerf") || !strcmp(w, "footnote") || !strcmp(w, "fldinst") ||
			    !strcmp(w, "listtable") || !strcmp(w, "listoverridetable") || !strcmp(w, "rsidtbl") ||
			    !strcmp(w, "generator") || !strcmp(w, "xmlnstbl") || !strcmp(w, "themedata") || !strcmp(w, "datastore") ||
			    !strcmp(w, "latentstyles") || !strcmp(w, "colorschememapping") || !strcmp(w, "pgdsctbl")) {
				st[sp].skip = 1;
				continue;
			}
			if (st[sp].skip)
				continue;
			if (st[sp].dest == 1 || st[sp].dest == 2) {
				/* in a header or footer: its text only */
				struct blk *m = RTFTO(sp);

				if ((!strcmp(w, "par") || !strcmp(w, "tab") || !strcmp(w, "line")) && m->n && m->c[m->n - 1] != ' ')
					adds(m, "   ", 0);
				continue;
			}
			if (!strcmp(w, "par") || !strcmp(w, "sect") || !strcmp(w, "cell") || !strcmp(w, "nestcell")) {
				/* the paragraph's properties last to \pard */
				int al = palign, in = pindent, ha = phang, ki = pkind;

				if (ptable && !intbl && w[0] != 'c' && w[0] != 'n') {
					/* the table has ended */
					ptable = 0;
					rowopen = 0;
					table_end();
				}
				if (ptable && !rowopen) {
					table_row();
					table_cell();
					rowopen = 1;
				}
				para_end();
				para_start();
				if (ptable && (w[0] == 'c' || w[0] == 'n'))
					table_cell();
				palign = al;
				pindent = in;
				phang = ha;
				pkind = ki;
			} else if (!strcmp(w, "pard")) {
				palign = 'L';
				pindent = phang = 0;
				pkind = K_PARA;
				ntabs = 0;
				intbl = 0;
			} else if (!strcmp(w, "intbl")) {
				intbl = 1;
				ptable = 1;
			} else if (!strcmp(w, "tqr") || !strcmp(w, "tqdec"))
				tqr = 1;
			else if (!strcmp(w, "tx")) {
				tab_add((int)((v + TW / 2) / TW), tqr);
				tqr = 0;
			} else if (!strcmp(w, "line"))
				addc(&para, 0x2028, st[sp].attr);
			else if (!strcmp(w, "tab"))
				addc(&para, '\t', st[sp].attr);
			else if (!strcmp(w, "row") || !strcmp(w, "nestrow")) {
				if (rowopen && ntr > 0 && TR[ntr - 1].ncell > 0 && TR[ntr - 1].cell[TR[ntr - 1].ncell - 1].n == 0)
					TR[ntr - 1].ncell--;
				rowopen = 0;
			} else if (!strcmp(w, "page"))
				page_break(); else if (!strcmp(w, "qc"))
				palign = 'C';
			else if (!strcmp(w, "qr"))
				palign = 'R';
			else if (!strcmp(w, "qj"))
				palign = 'J';
			else if (!strcmp(w, "ql"))
				palign = 'L';
			else if (!strcmp(w, "li"))
				pindent = (int)(v > 0 ? v / TW : 0);
			else if (!strcmp(w, "fi"))
				phang = (int)(v / TW);
			else if (!strcmp(w, "plain"))
				st[sp].attr = 0;
			else if (!strcmp(w, "b"))
				st[sp].attr = has && v == 0 ? st[sp].attr & ~A_B : st[sp].attr | 2;
			else if (!strcmp(w, "i"))
				st[sp].attr = has && v == 0 ? st[sp].attr & ~A_I : st[sp].attr | 4;
			else if (!strcmp(w, "ul") || !strcmp(w, "uld") || !strcmp(w, "uldb") || !strcmp(w, "ulw"))
				st[sp].attr = has && v == 0 ? st[sp].attr & ~A_UL : st[sp].attr | 1;
			else if (!strcmp(w, "ulnone"))
				st[sp].attr &= ~A_UL;
			else if (!strcmp(w, "uc"))
				st[sp].uc = (int)v;
			else if (!strcmp(w, "u")) {
				addc(&para, (uch)(v < 0 ? v + 65536 : v), st[sp].attr);
				skipn = st[sp].uc;
			} else if (!strcmp(w, "emdash") || !strcmp(w, "endash"))
				addc(&para, '-', st[sp].attr);
			else if (!strcmp(w, "lquote") || !strcmp(w, "rquote"))
				addc(&para, '\'', st[sp].attr);
			else if (!strcmp(w, "ldblquote") || !strcmp(w, "rdblquote"))
				addc(&para, '"', st[sp].attr);
			else if (!strcmp(w, "bullet"))
				addc(&para, '*', st[sp].attr);
			continue;
		}
		i++;
		if (st[sp].dest == 3 && nfld < 63) {
			fldbuf[nfld++] = (char)c;
			fldbuf[nfld] = 0;
			if (strstr(fldbuf, "PAGE") && !strstr(fldbuf, "PAGES") && !strstr(fldbuf, "NUMPAGES"))
				fldpage = 1;
		}
		if (st[sp].skip)
			continue;
		if (skipn > 0) {
			skipn--;
			continue;
		}
		addc(RTFTO(sp), cp1252(c), st[sp].attr);
		anytext = 1;
	}
	para_end();
	if (ptable) {
		ptable = 0;
		table_end();
	}
	(void)anytext;
	xfree(s);
	/* the empty paragraph at the end */
	while (nb > 0 && B[nb - 1].kind == K_EMPTY)
		nb--;
	space_paragraphs();
	trimblk(&header);
	trimblk(&footer);
#undef RTFTO
}

/* Markdown */

struct mdline {
	const unsigned char	*s;
	int			n;
};

static int
md_blank(const struct mdline *l)
{
	int i;

	for (i = 0; i < l->n; i++)
		if (l->s[i] != ' ' && l->s[i] != '\t')
			return 0;
	return 1;
}

static int
md_indent(const struct mdline *l)
{
	int i, c = 0;

	for (i = 0; i < l->n && (l->s[i] == ' ' || l->s[i] == '\t'); i++)
		c += l->s[i] == '\t' ? 4 - c % 4 : 1;
	return c;
}

/* inline: emphasis, code, links */
static void
md_inline(struct blk *b, const unsigned char *s, int n, int base)
{
	const unsigned char *p = s, *e = s + n;
	int a = base;

	while (p < e) {
		uch c;

		if (*p == '\\' && p + 1 < e && strchr("\\`*_{}[]()#+-.!<>|~\"'", p[1])) {
			addc(b, p[1], a);
			p += 2;
			continue;
		}
		if (*p == '`') {
			const unsigned char *q = p + 1;
			int ticks = 1;

			while (q < e && *q == '`') {
				ticks++;
				q++;
			}
			{
				const unsigned char *r = q;

				while (r + ticks <= e) {
					int k = 0;

					while (k < ticks && r[k] == '`')
						k++;
					if (k == ticks)
						break;
					r++;
				}
				if (r + ticks <= e) {
					while (q < r) {
						c = getutf8(&q, r);
						addc(b, c, a);
					}
					p = r + ticks;
					continue;
				}
			}
		}
		if ((*p == '*' || *p == '_') && p + 1 < e && p[1] == *p) {
			int open = !(a & A_B) || (base & A_B);

			if (!(open && p + 2 < e && p[2] == ' ')) {
				a = (a & A_B) && !(base & A_B) ? a & ~A_B : a | 2;
				p += 2;
				continue;
			}
		}
		if (*p == '*' || (*p == '_' && (p == s || !((p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= 'A' && p[-1] <= 'Z') ||
		    (p[-1] >= '0' && p[-1] <= '9')) || (a & A_I)))) {
			if (!(a & A_I) && p + 1 < e && p[1] != ' ') {
				a |= 4;
				p++;
				continue;
			}
			if ((a & A_I) && p > s && p[-1] != ' ') {
				a &= ~A_I;
				p++;
				continue;
			}
		}
		if (*p == '~' && p + 1 < e && p[1] == '~') {
			p += 2;
			continue;
		}
		if (*p == '<') {
			static const struct { const char *tag; int on, off; } tags[] = {
				{ "<u>", 1, 0 }, { "</u>", 0, A_UL }, { "<b>", 2, 0 }, { "</b>", 0, A_B },
				{ "<strong>", 2, 0 }, { "</strong>", 0, A_B }, { "<i>", 4, 0 }, { "</i>", 0, A_I },
				{ "<em>", 4, 0 }, { "</em>", 0, A_I }, { "<ins>", 1, 0 }, { "</ins>", 0, A_UL }, { NULL, 0, 0 }
			};
			int k;

			for (k = 0; tags[k].tag; k++) {
				int l = (int)strlen(tags[k].tag);

				if (e - p >= l && memcmp(p, tags[k].tag, l) == 0)
					break;
			}
			if (tags[k].tag) {
				a = (a | tags[k].on) & ~tags[k].off;
				p += strlen(tags[k].tag);
				continue;
			}
			if (e - p >= 4 && (memcmp(p, "<br>", 4) == 0 || memcmp(p, "<br/", 4) == 0 || memcmp(p, "<br ", 4) == 0)) {
				while (p < e && *p != '>')
					p++;
				p++;
				addc(b, 0x2028, a);
				continue;
			}
			{
				/* <http://...> */
				const unsigned char *q = p + 1;

				while (q < e && *q != '>' && *q != ' ')
					q++;
				if (q < e && *q == '>' && (memchr(p, ':', q - p) || memchr(p, '@', q - p))) {
					for (p++; p < q;)
						addc(b, getutf8(&p, q), a);
					p = q + 1;
					continue;
				}
			}
		}
		if (*p == '&') {
			const unsigned char *q = p + 1;

			while (q < e && q - p < 10 && *q != ';' && *q != ' ')
				q++;
			if (q < e && *q == ';') {
				xtext(b, (const char *)p, (long)(q - p + 1), a);
				p = q + 1;
				continue;
			}
		}
		if (*p == '[' || (*p == '!' && p + 1 < e && p[1] == '[')) {
			/* [text](url), ![alt](src) */
			int img = *p == '!';
			const unsigned char *t0 = p + (img ? 2 : 1), *t1 = t0, *u0, *u1;
			int depth = 1;

			while (t1 < e && depth) {
				if (*t1 == '[')
					depth++;
				else if (*t1 == ']')
					depth--;
				if (depth)
					t1++;
			}
			if (t1 < e && t1 + 1 < e && t1[1] == '(') {
				u0 = t1 + 2;
				u1 = u0;
				while (u1 < e && *u1 != ')' && *u1 != ' ')
					u1++;
				if (u1 < e) {
					const unsigned char *end = u1;

					while (end < e && *end != ')')
						end++;
					if (end < e) {
						md_inline(b, t0, (int)(t1 - t0), a);
						if (!img && !(u1 - u0 == t1 - t0 && memcmp(u0, t0, t1 - t0) == 0) && *u0 != '#' &&
						    !(u1 - u0 > 7 && memcmp(u0, "mailto:", 7) == 0)) {
							adds(b, " (", a);
							while (u0 < u1)
								addc(b, getutf8(&u0, u1), a);
							addc(b, ')', a);
						}
						p = end + 1;
						continue;
					}
				}
			}
		}
		c = getutf8(&p, e);
		if (c == '\t')
			c = ' ';
		addc(b, c, a);
	}
}

static int
md_isrule(const struct mdline *l)
{
	int i, n = 0, ch = 0;

	for (i = 0; i < l->n; i++) {
		int c = l->s[i];

		if (c == ' ')
			continue;
		if ((c == '-' || c == '*' || c == '_') && (!ch || c == ch)) {
			ch = c;
			n++;
		} else
			return 0;
	}
	return n >= 3;
}

/* a list item: the width of its mark, or 0 */
static int
md_item(const struct mdline *l, int *num)
{
	int i = 0, k;

	while (i < l->n && l->s[i] == ' ')
		i++;
	*num = 0;
	if (i + 1 < l->n && strchr("-*+", l->s[i]) && l->s[i + 1] == ' ')
		return i + 2;
	for (k = i; k < l->n && l->s[k] >= '0' && l->s[k] <= '9'; k++)
		;
	if (k > i && k - i < 4 && k + 1 < l->n && (l->s[k] == '.' || l->s[k] == ')') && l->s[k + 1] == ' ') {
		*num = 1;
		return k + 2;
	}
	return 0;
}

static void
md_cells(const struct mdline *l, int bold)
{
	int i = 0, n = l->n;
	struct blk *c;

	while (i < n && l->s[i] == ' ')
		i++;
	if (i < n && l->s[i] == '|')
		i++;
	while (n > i && (l->s[n - 1] == ' ' || l->s[n - 1] == '|'))
		n--;
	table_row();
	while (i <= n) {
		int j = i;

		while (j < n && !(l->s[j] == '|' && (j == 0 || l->s[j - 1] != '\\')))
			j++;
		c = table_cell();
		md_inline(c, l->s + i, j - i, bold ? 2 : 0);
		i = j + 1;
	}
}

static void
md_read(const char *path)
{
	long n, i;
	unsigned char *s = readfile(path, &n);
	struct mdline *L = NULL;
	int nl = 0, k;

	for (i = 0; i <= n;) {
		long e = i;

		while (e < n && s[e] != '\n')
			e++;
		L = (struct mdline *)xrealloc(L, (nl + 1) * sizeof *L);
		L[nl].s = s + i;
		L[nl].n = (int)(e - i);
		if (L[nl].n > 0 && L[nl].s[L[nl].n - 1] == '\r')
			L[nl].n--;
		nl++;
		i = e + 1;
	}
	if (nl > 0 && L[nl - 1].n == 0)
		nl--;
	for (k = 0; k < nl;) {
		struct mdline *l = &L[k];
		int ind = md_indent(l), mark, num;
		struct blk *b;

		if (md_blank(l)) {
			if (nb > 0 && B[nb - 1].kind != K_EMPTY)
				newblk(K_EMPTY);
			k++;
			continue;
		}
		/* fenced code */
		if (l->n - ind >= 3 && (memcmp(l->s + ind, "```", 3) == 0 || memcmp(l->s + ind, "~~~", 3) == 0)) {
			int fence = l->s[ind];

			for (k++; k < nl; k++) {
				int j = md_indent(&L[k]);

				if (L[k].n - j >= 3 && L[k].s[j] == fence && L[k].s[j + 1] == fence && L[k].s[j + 2] == fence) {
					k++;
					break;
				}
				b = newblk(K_LINE);
				{
					const unsigned char *p = L[k].s, *e = p + L[k].n;

					while (p < e) {
						uch c = getutf8(&p, e);

						if (c == '\t') {
							do
								addc(b, ' ', 0);
							while (b->n % 4);
						} else
							addc(b, c, 0);
					}
				}
				trimblk(b);
				if (b->n == 0)
					b->kind = K_EMPTY;
			}
			continue;
		}
		/* indented code */
		if (ind >= 4 && (nb == 0 || B[nb - 1].kind == K_EMPTY || B[nb - 1].kind == K_LINE)) {
			b = newblk(K_LINE);
			{
				const unsigned char *p = l->s, *e = p + l->n;
				int col = 0;

				while (p < e && col < 4) {
					col += *p == '\t' ? 4 - col % 4 : 1;
					p++;
				}
				while (p < e)
					addc(b, getutf8(&p, e), 0);
			}
			trimblk(b);
			k++;
			continue;
		}
		/* headings */
		if (l->s[ind] == '#') {
			int lv = 0, j = ind;

			while (j < l->n && l->s[j] == '#')
				j++, lv++;
			if (lv <= 6 && (j == l->n || l->s[j] == ' ')) {
				int e = l->n;

				while (e > j && (l->s[e - 1] == '#' || l->s[e - 1] == ' '))
					e--;
				while (j < e && l->s[j] == ' ')
					j++;
				b = newblk(K_HEAD);
				b->level = lv;
				md_inline(b, l->s + j, e - j, 0);
				k++;
				continue;
			}
		}
		if (md_isrule(l)) {
			newblk(K_RULE);
			k++;
			continue;
		}
		if (l->n - ind >= 4 && memcmp(l->s + ind, "<div", 4) == 0 && strstr((const char *)l->s, "page-break")) {
			newblk(K_PAGE);
			k++;
			continue;
		}
		/* a table: | a | b |, then | --- | --- | */
		if (l->s[ind] == '|' && k + 1 < nl) {
			struct mdline *sep = &L[k + 1];
			int j, ok = sep->n > 0;

			for (j = 0; j < sep->n; j++)
				if (!strchr("|-: ", sep->s[j]))
					ok = 0;
			if (ok) {
				md_cells(l, 1);
				for (k += 2; k < nl && !md_blank(&L[k]) && memchr(L[k].s, '|', L[k].n); k++)
					md_cells(&L[k], 0);
				table_end();
				continue;
			}
		}
		/* a paragraph, a list item, a quotation: to a blank line or
		   another block */
		b = newblk(K_PARA);
		mark = md_item(l, &num);
		{
			int quote = l->s[ind] == '>', first = 1;

			if (quote)
				b->indent = 4;
			if (mark) {
				int j = ind;

				b->indent = mark;
				b->hang = -(mark - ind);
				if (num) {
					while (j < mark)
						addc(b, l->s[j++], 0);
				} else
					adds(b, "- ", 0);
			}
			while (k < nl) {
				struct mdline *c = &L[k];
				int ci = md_indent(c), from = ci, dummy;
				int hard = 0, e = c->n;

				if (!first && (md_blank(c) || md_isrule(c) || c->s[ci] == '#' || md_item(c, &dummy) ||
				    (c->n - ci >= 3 && memcmp(c->s + ci, "```", 3) == 0) || (c->s[ci] == '>') != quote))
					break;
				if (first && mark)
					from = mark;
				if (quote) {
					from = ci + 1;
					if (from < c->n && c->s[from] == ' ')
						from++;
				}
				if (e - from >= 2 && c->s[e - 1] == ' ' && c->s[e - 2] == ' ')
					hard = 1;
				if (e > from && c->s[e - 1] == '\\') {
					hard = 1;
					e--;
				}
				while (e > from && c->s[e - 1] == ' ')
					e--;
				if (!first && b->n && b->c[b->n - 1] != 0x2028)
					addc(b, ' ', 0);
				md_inline(b, c->s + from, e - from, 0);
				if (hard)
					addc(b, 0x2028, 0);
				first = 0;
				k++;
				/* setext headings */
				if (k < nl && !quote && !mark && L[k].n > 0) {
					int j, eq = 1, dash = 1, any = 0;

					for (j = 0; j < L[k].n; j++) {
						if (L[k].s[j] == ' ')
							continue;
						any = 1;
						if (L[k].s[j] != '=')
							eq = 0;
						if (L[k].s[j] != '-')
							dash = 0;
					}
					if (any && (eq || dash)) {
						b->kind = K_HEAD;
						b->level = eq ? 1 : 2;
						k++;
						break;
					}
				}
			}
		}
		while (b->n > 0 && b->c[b->n - 1] == 0x2028)
			b->n--;
	}
	while (nb > 0 && B[nb - 1].kind == K_EMPTY)
		nb--;
	xfree(L);
}

/* plain text: the lines as they are, overlong ones as paragraphs */
static void
txt_read(const char *path)
{
	long n, i;
	unsigned char *s = readfile(path, &n);

	for (i = 0; i < n;) {
		long e = i;
		struct blk *b;
		const unsigned char *p;

		while (e < n && s[e] != '\n')
			e++;
		b = newblk(K_LINE);
		for (p = s + i; p < s + e;) {
			uch c = getutf8(&p, s + e);

			if (c == '\r')
				continue;
			if (c == '\t') {
				do
					addc(b, ' ', 0);
				while (b->n % 8);
			} else if (c == '\f') {
				b->kind = K_PAGE;
			} else
				addc(b, c, 0);
		}
		trimblk(b);
		if (b->kind != K_PAGE && b->n == 0)
			b->kind = K_EMPTY;
		else if (b->kind == K_LINE && b->n > width) {
			int k = 0;

			b->kind = K_PARA;
			while (k < b->n && b->c[k] == ' ')
				k++;
			b->indent = k;
			memmove(b->c, b->c + k, (b->n - k) * sizeof(uch));
			memmove(b->a, b->a + k, b->n - k);
			b->n -= k;
		}
		i = e + 1;
	}
	xfree(s);
}

/* Blocks into a LEX document: lines (LL) */

static void
ll_add(const uch *c, const unsigned char *a, int n, const char *raw)
{
	struct lline *l;
	int i, k = 0;

	LL = (struct lline *)xrealloc(LL, (nll + 1) * sizeof *LL);
	l = &LL[nll++];
	if (raw) {
		n = (int)strlen(raw);
		l->s = (unsigned char *)xrealloc(NULL, n + 1);
		l->a = (unsigned char *)xrealloc(NULL, n + 1);
		memcpy(l->s, raw, n);
		memset(l->a, 0, n + 1);
		l->len = n;
		return;
	}
	l->s = (unsigned char *)xrealloc(NULL, n + 1);
	l->a = (unsigned char *)xrealloc(NULL, n + 1);
	for (i = 0; i < n; i++) {
		int b = tolex(c[i]);

		if (b < 0)
			continue;
		l->s[k] = (unsigned char)b;
		l->a[k++] = a ? a[i] : 0;
	}
	l->len = k;
}

static char	ruler_now;

static void
ruler(int kind)
{
	char r[100];
	int i;

	if (kind == ruler_now)
		return;
	ruler_now = (char)kind;
	for (i = 0; i < 80 || i <= width; i++)
		r[i] = '.';
	r[0] = (char)kind;
	r[width] = 'M';
	r[i] = 0;
	ll_add(NULL, NULL, 0, r);
}

/* a paragraph in lines of the width, aligned */
static void
lay_para(const struct blk *b, int align)
{
	uch line[600];
	unsigned char la[600];
	int i = 0, n;

	while (i < b->n) {
		int first = i == 0 || 0, start = n = 0, avail, lastword = -1, k, ind;

		(void)first;
		ind = b->indent + (i == 0 ? b->hang : 0);
		if (ind < 0)
			ind = 0;
		if (align == 'C' || align == 'R')
			ind = 0;
		avail = width - ind;
		if (avail < 10)
			avail = 10;
		while (i < b->n && b->c[i] == ' ')
			i++;
		/* words onto the line */
		while (i < b->n && b->c[i] != 0x2028) {
			int j = i, wl;

			while (j < b->n && b->c[j] != ' ' && b->c[j] != 0x2028)
				j++;
			wl = j - i;
			if (n > 0 && n + 1 + wl > avail)
				break;
			if (n > 0) {
				line[n] = ' ';
				la[n] = (b->a[i - 1] & b->a[i]) & A_UL ? b->a[i] & A_UL : 0;
				if ((b->a[i - 1] & A_UL) && (b->a[i] & A_UL))
					la[n] = b->a[i] & A_UL;
				n++;
			}
			lastword = n;
			for (k = i; k < j && n < avail && n < 590; k++) {
				line[n] = b->c[k];
				la[n++] = b->a[k];
			}
			i = k;
			if (k < j)
				break;		/* a word longer than the line: on the next */
			while (i < b->n && b->c[i] == ' ')
				i++;
		}
		(void)start;
		(void)lastword;
		{
			int hard = i < b->n && b->c[i] == 0x2028, last = i >= b->n || hard, pad = 0, out = 0;
			uch o[700];
			unsigned char oa[700];

			if (align == 'C')
				pad = (width - n) / 2;
			else if (align == 'R')
				pad = width - n;
			else
				pad = ind;
			if (pad < 0)
				pad = 0;
			for (k = 0; k < pad; k++) {
				o[out] = ' ';
				oa[out++] = 0;
			}
			if (align == 'J' && !last && n < avail) {
				/* justified: the spaces spread to M */
				int gaps = 0, extra = avail - n, g = 0;

				for (k = 0; k < n; k++)
					if (line[k] == ' ')
						gaps++;
				for (k = 0; k < n; k++) {
					o[out] = line[k];
					oa[out++] = la[k];
					if (line[k] == ' ' && gaps) {
						int add = extra / gaps + (g < extra % gaps ? 1 : 0), m;

						for (m = 0; m < add; m++) {
							o[out] = ' ';
							oa[out++] = la[k];
						}
						g++;
					}
				}
			} else
				for (k = 0; k < n; k++) {
					o[out] = line[k];
					oa[out++] = la[k];
				}
			ll_add(o, oa, out, NULL);
			if (hard)
				i++;
		}
	}
	if (b->n == 0)
		ll_add(NULL, NULL, 0, "");
}

static void
lex_from_blocks(void)
{
	int i, k;
	char t[600];

	ruler_now = 0;
	ruler(justify ? 'J' : 'F');
	for (k = 0; k < 2; k++) {
		struct blk *m = k ? &footer : &header;

		if (m->n) {
			uch c[600];
			unsigned char a[600];
			int n = 0, j;

			c[n] = '.';
			a[n++] = 0;
			c[n] = k ? 'F' : 'T';
			a[n++] = 0;
			c[n] = ' ';
			a[n++] = 0;
			for (j = 0; j < m->n && n < 580; j++)
				if (m->c[j] == 0xe000) {
					const char *pg = national ? "(SIDA:" : "(PAGE:";

					while (*pg) {
						c[n] = (unsigned char)*pg++;
						a[n++] = 0;
					}
				} else {
					c[n] = m->c[j];
					a[n++] = m->a[j];
				}
			ll_add(c, a, n, NULL);
		}
	}
	for (i = 0; i < nb; i++) {
		struct blk *b = &B[i];

		switch (b->kind) {
		case K_EMPTY:
			ll_add(NULL, NULL, 0, "");
			break;
		case K_PAGE:
			ll_add(NULL, NULL, 0, ".P");
			break;
		case K_RULE:
			for (k = 0; k < width && k < 590; k++)
				t[k] = '-';
			t[k] = 0;
			ll_add(NULL, NULL, 0, t);
			break;
		case K_NOTE:
			{
				struct blk n;

				memset(&n, 0, sizeof n);
				adds(&n, ".N ", 0);
				for (k = 0; k < b->n; k++)
					addc(&n, b->c[k], b->a[k]);
				ll_add(n.c, n.a, n.n, NULL);
				xfree(n.c);
				xfree(n.a);
			}
			break;
		case K_LINE:
			{
				struct blk n;

				memset(&n, 0, sizeof n);
				for (k = 0; k < b->n; k++)
					if (b->c[k] == '\t') {
						do
							addc(&n, ' ', b->a[k]);
						while (n.n % 8);
					} else
						addc(&n, b->c[k], b->a[k]);
				ll_add(n.c, n.a, n.n, NULL);
				xfree(n.c);
				xfree(n.a);
			}
			break;
		case K_HEAD:
			for (k = 0; k < b->n; k++)
				b->a[k] |= 2;
			ruler(justify ? 'J' : 'F');
			lay_para(b, b->level <= 1 ? 'C' : 'L');
			break;
		default:
			ruler(b->align == 'J' || (justify && b->align == 'L') ? 'J' : 'F');
			lay_para(b, b->align == 'L' && justify ? 'J' : b->align);
		}
	}
}

static int
lex_attrchar(int a)
{
	int v = (a >> 1) & 15;

	if (a & 1)
		return v ? 'a' + v - 1 : '_';
	return v ? 'A' + v - 1 : ' ';
}

static void
lex_save(const char *path)
{
	struct buf o;
	int i, k;

	memset(&o, 0, sizeof o);
	for (i = 0; i < nll; i++) {
		struct lline *l = &LL[i];
		int any = 0, n = l->len;

		while (n > 0 && l->s[n - 1] == ' ' && !l->a[n - 1])
			n--;
		bput(&o, (const char *)l->s, n);
		for (k = 0; k < n; k++)
			if (l->a[k])
				any = 1;
		if (any) {
			int e = n;

			bc(&o, 255);
			bc(&o, '\n');
			while (e > 0 && !l->a[e - 1])
				e--;
			for (k = 0; k < e; k++)
				bc(&o, lex_attrchar(l->a[k] & 0x1f));
		}
		bc(&o, '\n');
	}
	writebuf(path, &o);
}

/* The formats */

static const char *
ext(const char *path)
{
	const char *s = strrchr(path, '/'), *d;

	d = strrchr(s ? s : path, '.');
	return d ? d + 1 : "";
}

static int
fmt(const char *path)
{
	static const char *n[] = { "rtf", "docx", "odt", "md", "markdown", "html", "htm", "txt", "text", NULL };
	static const char k[] = "RDOMMHHTT";
	char e[12];
	int i;

	for (i = 0; i < 11 && ext(path)[i]; i++)
		e[i] = (char)(ext(path)[i] >= 'A' && ext(path)[i] <= 'Z' ? ext(path)[i] + 32 : ext(path)[i]);
	e[i] = 0;
	for (i = 0; n[i]; i++)
		if (strcmp(e, n[i]) == 0)
			return k[i];
	return 'L';
}

/* Swedish when the locale is (sv_SE, sv_FI, ..._SE); without a locale,
   English, but on D-NIX (the Swedish D-NIX Amiga port) Swedish */
static int
swedish(void)
{
	const char *l = getenv("LC_ALL");

	if (l == NULL || !*l)
		l = getenv("LC_MESSAGES");
	if (l == NULL || !*l)
		l = getenv("LANG");
	if (l == NULL || !*l || strcmp(l, "C") == 0 || strcmp(l, "POSIX") == 0 || strncmp(l, "C.", 2) == 0)
#ifdef USE_TERMIO
		return 1;
#else
		return 0;
#endif
	return (l[0] == 's' && l[1] == 'v') || strstr(l, "_SE") != NULL;
}

static void
usage(void)
{
	fprintf(stderr, "usage: lexconv [-n | -a] [-w width] [-j] from to\n"
	    "  from, to: a LEX document, or .rtf .docx .odt .md .txt (and .html out)\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int i = 1, f, t;
	const char *from, *to, *nat = getenv("LEXNAT");

	national = nat && *nat ? *nat != '0' : swedish();
	for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (strcmp(argv[i], "-n") == 0)
			national = 1;
		else if (strcmp(argv[i], "-a") == 0)
			national = 0;
		else if (strcmp(argv[i], "-j") == 0)
			justify = 1;
		else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
			width = atoi(argv[++i]);
			if (width < 20 || width > 200)
				usage();
		} else
			usage();
	}
	if (argc - i != 2 || !argv[i][0] || !argv[i + 1][0])
		usage();
	from = argv[i];
	to = argv[i + 1];
	f = fmt(from);
	t = fmt(to);
	if (f == 'L' && t == 'L')
		die("one of the two is to be .rtf .docx .odt .md .html or .txt", NULL);
	if (f == 'H')
		die("HTML is written, not read: ", from);
	switch (f) {
	case 'L':
		lex_load(from);
		if (t != 'T')
			lex_blocks();
		break;
	case 'R':
		rtf_read(from);
		break;
	case 'D':
		docx_read(from);
		break;
	case 'O':
		odt_read(from);
		break;
	case 'M':
		md_read(from);
		break;
	case 'T':
		txt_read(from);
		break;
	}
	if (f != 'L' && (t == 'L' || t == 'T'))
		lex_from_blocks();
	switch (t) {
	case 'L':
		lex_save(to);
		break;
	case 'R':
		rtf_write(to);
		break;
	case 'D':
		docx_write(to);
		break;
	case 'O':
		odt_write(to);
		break;
	case 'M':
		md_write(to);
		break;
	case 'H':
		html_write(to, from);
		break;
	case 'T':
		txt_write(to);
		break;
	}
	printf("%s -> %s\n", from, to);
	return 0;
}
