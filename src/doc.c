/*
 * doc.c - documents: rows of text with attributes, rulers, and the files
 * LEX keeps them in.
 *
 * A document is a text file.  A line wider than the screen is held as
 * rows, each but the last flagged LF_CONT (LEX shows "&" in the last
 * column).  A line with attributes ends with the character 255 and is
 * followed by a line that shows them (Appendix I of the manual): blank
 * none, "_" attribute 1, "A"-"O" attributes 2-5 combined, "a"-"o" the
 * same with 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

struct doc *
doc_new(void)
{
	struct doc *d = (struct doc *)xrealloc(NULL, sizeof(struct doc));

	memset(d, 0, sizeof *d);
	return d;
}

void
line_free(struct line *l)
{
	xfree(l->s);
	xfree(l->a);
	memset(l, 0, sizeof *l);
}

void
doc_clear(struct doc *d)
{
	int i;

	for (i = 0; i < d->n; i++)
		line_free(&d->l[i]);
	d->n = 0;
	d->changed = 1;
}

void
doc_free(struct doc *d)
{
	if (d == NULL)
		return;
	doc_clear(d);
	xfree(d->l);
	xfree(d);
}

static void
grow(struct line *l, int need)
{
	int ncap;

	if (need <= l->cap)
		return;
	ncap = need < 32 ? 32 : need * 2;
	l->s = (unsigned char *)xrealloc(l->s, ncap);
	l->a = (unsigned char *)xrealloc(l->a, ncap);
	l->cap = ncap;
}

int
doc_insline(struct doc *d, int at, const unsigned char *s, int len)
{
	if (d->n == d->cap) {
		d->cap = d->cap ? d->cap * 2 : 64;
		d->l = (struct line *)xrealloc(d->l, d->cap * sizeof(struct line));
	}
	if (at < 0)
		at = 0;
	if (at > d->n)
		at = d->n;
	memmove(d->l + at + 1, d->l + at, (d->n - at) * sizeof(struct line));
	d->n++;
	memset(&d->l[at], 0, sizeof(struct line));
	line_set(&d->l[at], s, len);
	d->changed = 1;
	return at;
}

void
doc_delline(struct doc *d, int at)
{
	if (at < 0 || at >= d->n)
		return;
	line_free(&d->l[at]);
	memmove(d->l + at, d->l + at + 1, (d->n - at - 1) * sizeof(struct line));
	d->n--;
	d->changed = 1;
}

struct line *
doc_line(struct doc *d, int i)
{
	int ch = d->changed;

	while (d->n <= i)
		doc_insline(d, d->n, (const unsigned char *)"", 0);
	d->changed = ch;
	return &d->l[i];
}

void
line_set(struct line *l, const unsigned char *s, int len)
{
	grow(l, len + 1);
	if (len)
		memcpy(l->s, s, len);
	memset(l->a, 0, len + 1);
	l->len = len;
}

void
line_setattr(struct line *l, const unsigned char *s, const unsigned char *a, int len)
{
	line_set(l, s, len);
	if (a && len)
		memcpy(l->a, a, len);
}

void
line_copy(struct line *to, const struct line *from)
{
	line_setattr(to, from->s, from->a, from->len);
	to->flags = from->flags;
}

void
line_pad(struct line *l, int n)
{
	if (n > l->len) {
		grow(l, n + 1);
		memset(l->s + l->len, ' ', n - l->len);
		memset(l->a + l->len, 0, n - l->len);
		l->len = n;
	}
}

void
line_ins(struct line *l, int at, const unsigned char *s, int n, int attr)
{
	line_pad(l, at);
	grow(l, l->len + n + 1);
	memmove(l->s + at + n, l->s + at, l->len - at);
	memmove(l->a + at + n, l->a + at, l->len - at);
	memcpy(l->s + at, s, n);
	memset(l->a + at, attr, n);
	l->len += n;
}

void
line_del(struct line *l, int at, int n)
{
	if (at >= l->len || n <= 0)
		return;
	if (at + n > l->len)
		n = l->len - at;
	memmove(l->s + at, l->s + at + n, l->len - at - n);
	memmove(l->a + at, l->a + at + n, l->len - at - n);
	l->len -= n;
}

void
line_trim(struct line *l)
{
	while (l->len > 0 && l->s[l->len - 1] == ' ' && !(l->a[l->len - 1] & (AT_MASK | AT_UNPROT)))
		l->len--;
}

int
line_ch(const struct line *l, int at)
{
	return at >= 0 && at < l->len ? l->s[at] : ' ';
}

int
line_at(const struct line *l, int at)
{
	return at >= 0 && at < l->len ? l->a[at] : 0;
}

void
line_put(struct line *l, int at, int c, int attr)
{
	if (at < 0)
		return;
	line_pad(l, at + 1);
	l->s[at] = (unsigned char)c;
	l->a[at] = (unsigned char)attr;
}

/* Rulers */

int
isruler(const unsigned char *s, int len)
{
	int i, dots = 0, marks = 0;

	/* across the whole screen, as LEX-68 wants it */
	if (len < (t_cols > 0 && t_cols < 80 ? t_cols : 80))
		return 0;
	for (i = 0; i < len; i++) {
		if (s[i] == '.')
			dots++;
		else if (strchr("CFJMLRTH#,", s[i]) && s[i])
			marks++;
		else
			return 0;
	}
	return marks > 0 && dots >= len / 3;
}

void
ruler_parse(struct ruler *r, const char *s, int len)
{
	int i;

	if (len > MAXW)
		len = MAXW;
	memcpy(r->s, s, len);
	r->s[len] = 0;
	r->len = len;
	r->left = r->right = r->hyph = -1;
	r->kind = 0;
	for (i = 0; i < len; i++) {
		switch (s[i]) {
		case 'F': case 'J': case 'C':
			if (!r->kind) {
				r->kind = s[i];
				r->left = i;
			}
			break;
		case 'M':
			if (r->right < 0)
				r->right = i;
			break;
		case 'H':
			if (r->hyph < 0)
				r->hyph = i;
			break;
		}
	}
}

/* The next tab stop after col: L, R, T (not in insert mode), #, C, F, J, M. */
int
ruler_next(const struct ruler *r, int col, int insmode)
{
	int i;

	for (i = col + 1; i < r->len; i++)
		switch (r->s[i]) {
		case 'T':
			if (insmode)
				break;
			/* fall through */
		case 'L': case 'R': case 'C': case 'F': case 'J':
			return i;
		case '#':
			if (i == 0 || (r->s[i - 1] != '#' && r->s[i - 1] != ','))
				return i;
			break;
		}
	return -1;
}

int
ruler_prev(const struct ruler *r, int col)
{
	int i;

	for (i = col - 1; i >= 0; i--)
		if (i < r->len && strchr("TLRCFJ#", r->s[i]) && r->s[i] &&
		    (r->s[i] != '#' || i == 0 || (r->s[i - 1] != '#' && r->s[i - 1] != ',')))
			return i;
	return -1;
}

/* Files */

static int
attrchar(int a)
{
	int v = (a >> 1) & 15;

	if (a & 1)
		return v ? 'a' + v - 1 : '_';
	return v ? 'A' + v - 1 : ' ';
}

static int
charattr(int c)
{
	if (c == '_')
		return 1;
	if (c >= 'A' && c <= 'O')
		return (c - 'A' + 1) << 1;
	if (c >= 'a' && c <= 'o')
		return ((c - 'a' + 1) << 1) | 1;
	return 0;
}

/* a whole line of the file as rows of the screen's width */
static void
addline(struct doc *d, const unsigned char *s, const unsigned char *a, int len, int width)
{
	int at = 0;

	if (len >= width && width > 1) {
		while (len - at > width - 1) {
			int i = doc_insline(d, d->n, s + at, width - 1);

			if (a)
				memcpy(d->l[i].a, a + at, width - 1);
			d->l[i].flags = LF_CONT;
			at += width - 1;
		}
	}
	{
		int i = doc_insline(d, d->n, s + at, len - at);

		if (a)
			memcpy(d->l[i].a, a + at, len - at);
		if (isruler(d->l[i].s, d->l[i].len))
			d->l[i].flags |= LF_RULER;
	}
}

static int
getline_(FILE *f, unsigned char *buf, int max)
{
	int c, n = 0;

	while ((c = getc(f)) != EOF && c != '\n') {
		if (c == '\r')
			continue;
		if (c == '\t') {
			do {
				if (n < max)
					buf[n++] = ' ';
			} while (n % 8);
			continue;
		}
		if (n < max)
			buf[n++] = (unsigned char)c;
	}
	if (c == EOF && n == 0)
		return -1;
	return n;
}

int
doc_load(struct doc *d, const char *path, int width)
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf, *abuf, *a;
	int n, k, term = M.attrterm ? M.attrterm : 255;

	if (f == NULL)
		return -1;
	buf = (unsigned char *)xrealloc(NULL, 4096);
	abuf = (unsigned char *)xrealloc(NULL, 4096);
	while ((n = getline_(f, buf, 4000)) >= 0) {
		a = NULL;
		if (n > 0 && buf[n - 1] == term) {
			int m = getline_(f, abuf, 4000);

			n--;
			if (m < 0)
				m = 0;
			for (k = 0; k < n; k++)
				abuf[k] = (unsigned char)(k < m ? charattr(abuf[k]) : 0);
			a = abuf;
		}
		if (n > MAXW)
			n = MAXW;
		addline(d, buf, a, n, width);
	}
	fclose(f);
	xfree(buf);
	xfree(abuf);
	d->changed = 0;
	return 0;
}

static int
isctrlline(const struct line *l)
{
	return l->len >= 2 && l->s[0] == '.' && l->s[1] >= 'A' && l->s[1] <= 'Z';
}

static void
count1(const unsigned char *s, int len, long *words, long *chars)
{
	int k, in = 0;

	for (k = 0; k < len; k++) {
		if (s[k] != ' ' && !in)
			(*words)++;
		in = s[k] != ' ';
		if (s[k] != ' ')
			(*chars)++;
	}
}

void
doc_count(struct doc *d, long *lines, long *words, long *chars)
{
	int i;

	*lines = *words = *chars = 0;
	for (i = 0; i < d->n; i++) {
		struct line *l = &d->l[i];

		if ((l->flags & LF_RULER) || isctrlline(l))
			continue;
		if (!(l->flags & LF_CONT))
			(*lines)++;
		count1(l->s, l->len, words, chars);
	}
}

int
doc_save(struct doc *d, const char *path, long *lines, long *words, long *chars)
{
	FILE *f;
	int i, n, k, any, term = M.attrterm ? M.attrterm : 255;
	unsigned char *s, *a;
	int len, cap = 1024;

	if ((f = fopen(path, "wb")) == NULL)
		return -1;
	for (n = d->n; n > 0; n--) {
		line_trim(&d->l[n - 1]);
		if (d->l[n - 1].len || (n >= 2 && (d->l[n - 2].flags & LF_CONT)))
			break;
	}
	s = (unsigned char *)xrealloc(NULL, cap);
	a = (unsigned char *)xrealloc(NULL, cap);
	for (i = 0; i < n; i++) {
		len = 0;
		for (;;) {
			struct line *l = &d->l[i];

			if (len + l->len + 1 > cap) {
				cap = (len + l->len + 1) * 2;
				s = (unsigned char *)xrealloc(s, cap);
				a = (unsigned char *)xrealloc(a, cap);
			}
			memcpy(s + len, l->s, l->len);
			for (k = 0; k < l->len; k++)
				a[len + k] = l->a[k] & AT_MASK;
			len += l->len;
			if (!(l->flags & LF_CONT) || i + 1 >= n)
				break;
			/* a continued row is full: its blanks count */
			if (l->len < t_cols - 1) {
				int pad = t_cols - 1 - l->len;

				if (len + pad + 1 > cap) {
					cap = (len + pad + 1) * 2;
					s = (unsigned char *)xrealloc(s, cap);
					a = (unsigned char *)xrealloc(a, cap);
				}
				memset(s + len, ' ', pad);
				memset(a + len, 0, pad);
				len += pad;
			}
			i++;
		}
		while (len > 0 && s[len - 1] == ' ' && !a[len - 1])
			len--;
		any = 0;
		for (k = 0; k < len; k++)
			if (a[k])
				any = 1;
		fwrite(s, 1, len, f);
		if (any) {
			int m = len;

			putc(term, f);
			putc('\n', f);
			for (k = 0; k < len; k++)
				a[k] = (unsigned char)attrchar(a[k]);
			while (m > 0 && a[m - 1] == ' ')
				m--;
			fwrite(a, 1, m, f);
		}
		putc('\n', f);
	}
	xfree(s);
	xfree(a);
	if (fclose(f) != 0)
		return -1;
	if (lines)
		doc_count(d, lines, words, chars);
	d->changed = 0;
	return 0;
}
