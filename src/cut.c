/*
 * cut.c - cut and paste (Appendix C.9-C.13) and the buffers of deleted
 * text (ESC -D, -W, -X).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

#define NBUF	8		/* the main buffer and T U V W X Y Z */
#define CUTMAX	16384

struct cbuf {
	struct line	*l;
	int		n;
	int		sentence;	/* cut as a string, not a block */
};

static struct cbuf	buf[NBUF];
static unsigned char	dchars[MAXW];
static int		ndchars;
static unsigned char	dword[MAXW];
static int		ndword = -1;
static struct line	dline;
static int		havedline;

static int
bufno(int b)
{
	if (b == 0)
		return 0;
	if (b >= 'T' && b <= 'Z')
		return 1 + b - 'T';
	return -1;
}

static void
bfree(struct cbuf *c)
{
	int i;

	for (i = 0; i < c->n; i++)
		line_free(&c->l[i]);
	xfree(c->l);
	c->l = NULL;
	c->n = 0;
	c->sentence = 0;
}

static long
used(void)
{
	long t = 0;
	int i, k;

	for (i = 0; i < NBUF; i++)
		for (k = 0; k < buf[i].n; k++)
			t += buf[i].l[k].len + 1;
	return t;
}

static void
badd(struct cbuf *c, const unsigned char *s, const unsigned char *a, int n)
{
	c->l = (struct line *)xrealloc(c->l, (c->n + 1) * sizeof(struct line));
	memset(&c->l[c->n], 0, sizeof(struct line));
	line_setattr(&c->l[c->n], s, NULL, n);
	if (a)
		memcpy(c->l[c->n].a, a, n);
	c->n++;
}

void
cut_mark(void)
{
	ED.markl = ED.cl;
	ED.markc = ED.cc;
}

void
cut_clearall(void)
{
	int i;

	for (i = 0; i < NBUF; i++)
		bfree(&buf[i]);
}

int
cut_used(char *list)
{
	static const char names[] = "*TUVWXYZ";
	int i, n = 0;

	for (i = 0; i < NBUF; i++)
		if (buf[i].n)
			list[n++] = names[i];
	list[n] = 0;
	return n;
}

static unsigned char
attrof(int a)
{
	return (unsigned char)(a & AT_MASK);
}

/* ESC ) x a: a block, from the mark to the cursor */
int
cut_block(int b, int how)
{
	int l1 = ED.markl, l2 = ED.cl, c1 = ED.markc, c2 = ED.cc, i, k;
	struct cbuf tmp;

	if (how != 'B' && how != 'L' && how != 'S') {
		error("AC");
		return -1;
	}
	if (l1 < 0 || l2 < l1 || (c2 < c1 && !(c1 == 0 && c2 == 0))) {
		error("BR");
		return -1;
	}
	memset(&tmp, 0, sizeof tmp);
	if (b >= '0' && b <= '9') {
		/* into a keystore: one line, 78 characters */
		struct line *l = ed_line(l1);
		int n = c2 - c1 + 1;

		if (l1 != l2 || n > KSLEN) {
			error("AX");
			return -1;
		}
		for (k = 0; k < n; k++)
			keystore[b - '0'][k] = (char)line_ch(l, c1 + k);
		keystore[b - '0'][n] = 0;	/* blanks and all: *DTIR builds keys so */
		if (how == 'B' || how == 'S') {
			for (k = c1; k <= c2; k++)
				if (k < l->len && (!ED.protect || ed_unprot(l1, k)))
					l->s[k] = (unsigned char)(ED.protect ? M.paint : ' ');
			if (how == 'S' && !ED.protect)
				line_del(l, c1, c2 - c1 + 1);
			line_trim(l);
			ED.d->changed = 1;
		}
		ED.cc = c1;
		return 0;
	}
	if (bufno(b) < 0) {
		error("AC");
		return -1;
	}
	if (c1 == 0 && c2 == 0) {
		/* whole lines */
		for (i = l1; i < l2; i++) {
			struct line *l = ed_line(i);

			badd(&tmp, l->s, l->a, l->len);
			tmp.l[tmp.n - 1].flags = l->flags;
			for (k = 0; k < l->len; k++)
				tmp.l[tmp.n - 1].a[k] = attrof(l->a[k]);
		}
		if (used() + 1 > CUTMAX) {
			bfree(&tmp);
			error("BF");
			return -1;
		}
		if (how == 'S')
			for (i = l1; i < l2; i++)
				doc_delline(ED.d, l1);
		else if (how == 'B')
			for (i = l1; i < l2; i++)
				ed_line(i)->len = 0;
	} else {
		for (i = l1; i <= l2; i++) {
			struct line *l = ed_line(i);
			unsigned char s[MAXW + 1], a[MAXW + 1];
			int n = 0;

			for (k = c1; k <= c2 && k < MAXW; k++) {
				s[n] = (unsigned char)line_ch(l, k);
				a[n++] = attrof(line_at(l, k));
			}
			while (n > 0 && s[n - 1] == ' ' && !a[n - 1])
				n--;
			badd(&tmp, s, a, n);
			if (how == 'B') {
				for (k = c1; k <= c2 && k < l->len; k++) {
					l->s[k] = ' ';
					l->a[k] = 0;
				}
				line_trim(l);
			} else if (how == 'S' && !(l->flags & LF_CONT))
				line_del(l, c1, c2 - c1 + 1);
		}
	}
	bfree(&buf[bufno(b)]);
	buf[bufno(b)] = tmp;
	if (how != 'L')
		ED.d->changed = 1;
	/* the cursor goes back to the mark, as in LEX-68 */
	ED.cl = l1 < ED.d->n ? l1 : ED.cl;
	ED.cc = c1;
	return 0;
}

/* ESC . x a: a string from the mark to the cursor, under F/J..M margins */
int
cut_sentence(int b, int how)
{
	int l1 = ED.markl, l2 = ED.cl, c1 = ED.markc, c2 = ED.cc, i;
	struct cbuf tmp;
	struct ruler r;

	if (bufno(b) < 0 || (how != 'B' && how != 'L' && how != 'S')) {
		error("AC");
		return -1;
	}
	if (l1 < 0 || l2 < l1 || (l1 == l2 && c2 < c1)) {
		error("BR");
		return -1;
	}
	ed_ruler(&r);
	if (r.kind != 'F' && r.kind != 'J') {
		error("NJ");
		return -1;
	}
	memset(&tmp, 0, sizeof tmp);
	tmp.sentence = 1;
	for (i = l1; i <= l2; i++) {
		struct line *l = ed_line(i);
		int a = i == l1 ? c1 : 0, e = i == l2 ? c2 + 1 : l->len;

		if (e > l->len)
			e = l->len;
		badd(&tmp, a < e ? l->s + a : (const unsigned char *)"", a < e ? l->a + a : NULL, a < e ? e - a : 0);
	}
	if (how == 'B') {
		for (i = l1; i <= l2; i++) {
			struct line *l = ed_line(i);
			int a = i == l1 ? c1 : 0, e = i == l2 ? c2 + 1 : l->len, k;

			for (k = a; k < e && k < l->len; k++)
				l->s[k] = ' ';
			line_trim(l);
		}
	} else if (how == 'S') {
		struct line *a = ed_line(l1), *z = ed_line(l2);
		int rest = c2 + 1 < z->len ? z->len - c2 - 1 : 0;
		unsigned char *t = (unsigned char *)xrealloc(NULL, rest + 1), *ta = (unsigned char *)xrealloc(NULL, rest + 1);

		memcpy(t, z->s + c2 + 1, rest);
		memcpy(ta, z->a + c2 + 1, rest);
		a = ed_line(l1);
		if (a->len > c1)
			a->len = c1;
		line_ins(a, c1, t, rest, 0);
		memcpy(a->a + c1, ta, rest);
		xfree(t);
		xfree(ta);
		for (i = l2; i > l1; i--)
			doc_delline(ED.d, i);
		ED.cl = l1;
		ED.cc = c1;
		ed_justify(0);
		ED.cl = l1;
		ED.cc = c1;
	}
	bfree(&buf[bufno(b)]);
	buf[bufno(b)] = tmp;
	if (how != 'L')
		ED.d->changed = 1;
	return 0;
}

/* ESC * x a: paste (E elbow, I insert, O overlay, D delete the buffer) */
int
paste(int b, int how)
{
	struct cbuf *c;
	int i, k;

	if (bufno(b) < 0) {
		error("AC");
		return -1;
	}
	c = &buf[bufno(b)];
	if (how == 'D') {
		bfree(c);
		return 0;
	}
	if (how != 'E' && how != 'I' && how != 'O') {
		error("AC");
		return -1;
	}
	if (c->n == 0) {
		error("CM");
		return -1;
	}
	if (c->sentence) {
		if (how == 'O') {
			for (i = 0; i < c->n; i++) {
				for (k = 0; k < c->l[i].len; k++)
					ed_typechar(c->l[i].s[k], 1);
				if (i + 1 < c->n)
					ed_typechar(' ', 1);
			}
		} else {
			int oi = ED.ins;

			ED.ins = 1;
			for (i = 0; i < c->n; i++) {
				for (k = 0; k < c->l[i].len; k++)
					ed_typechar(c->l[i].s[k], 1);
				if (i + 1 < c->n)
					ed_typechar(' ', 1);
			}
			ED.ins = oi;
			{
				int sl = ED.cl, sc = ED.cc;

				ed_justify(0);
				ED.cl = sl;
				ED.cc = sc;
			}
		}
		ED.d->changed = 1;
		return 0;
	}
	for (i = 0; i < c->n; i++) {
		struct line *l, *s = &c->l[i];

		if (how == 'I') {
			doc_insline(ED.d, ED.cl + i, (const unsigned char *)"", 0);
			l = ed_line(ED.cl + i);
			line_ins(l, ED.cc, s->s, s->len, 0);
			memcpy(l->a + ED.cc, s->a, s->len);
			if (ED.cc == 0)
				l->flags = s->flags;
		} else if (how == 'E') {
			l = ed_line(ED.cl + i);
			line_ins(l, ED.cc, s->s, s->len, 0);
			memcpy(l->a + ED.cc, s->a, s->len);
			if (l->len > t_cols)
				l->len = t_cols;
		} else {
			l = ed_line(ED.cl + i);
			for (k = 0; k < s->len && ED.cc + k < t_cols; k++)
				if (!ED.protect || ed_unprot(ED.cl + i, ED.cc + k))
					line_put(l, ED.cc + k, s->s[k], s->a[k] | (line_at(l, ED.cc + k) & (AT_UNPROT | AT_BG)));
		}
		line_trim(l);
	}
	ed_markrulers();
	ED.d->changed = 1;
	return 0;
}

/* ESC } a: blanks over the block from the mark to the cursor */
int
paste_blanks(int how)
{
	int l1 = ED.markl, l2 = ED.cl, c1 = ED.markc, c2 = ED.cc, i, k;
	unsigned char sp[MAXW + 1];

	if (l1 < 0 || l2 < l1 || c2 < c1) {
		error("BR");
		return -1;
	}
	memset(sp, ' ', sizeof sp);
	if (c1 == 0 && c2 == 0) {
		for (i = l1; i < l2; i++)
			if (how == 'I')
				doc_insline(ED.d, l1, (const unsigned char *)"", 0);
			else
				ed_line(i)->len = 0;
		ED.d->changed = 1;
		return 0;
	}
	for (i = l1; i <= l2; i++) {
		struct line *l;

		if (how == 'I') {
			doc_insline(ED.d, l2 + 1 + (i - l1), (const unsigned char *)"", 0);
			continue;
		}
		l = ed_line(i);
		if (how == 'E') {
			line_ins(l, c1, sp, c2 - c1 + 1, 0);
			if (l->len > t_cols)
				l->len = t_cols;
		} else
			for (k = c1; k <= c2; k++)
				if (k < l->len)
					line_put(l, k, ' ', 0);
		line_trim(l);
	}
	ED.d->changed = 1;
	return 0;
}

/* Deleted text */

void
undel_char(int c)
{
	if (ndchars == MAXW) {
		memmove(dchars, dchars + 1, MAXW - 1);
		ndchars--;
	}
	dchars[ndchars++] = (unsigned char)c;
}

void
undel_word(const unsigned char *s, int n)
{
	if (n > MAXW)
		n = MAXW;
	memcpy(dword, s, n);
	ndword = n;
}

void
undel_line(const struct line *l)
{
	line_copy(&dline, l);
	dline.flags &= ~LF_CONT;
	havedline = 1;
}

void
undel_putchar(void)
{
	unsigned char c;

	if (ndchars == 0)
		return;
	c = dchars[--ndchars];
	line_ins(ed_line(ED.cl), ED.cc, &c, 1, 0);
	ED.d->changed = 1;
}

void
undel_putword(void)
{
	if (ndword < 0)
		return;
	line_ins(ed_line(ED.cl), ED.cc, dword, ndword, 0);
	ed_wrap(ED.cl);
	ED.d->changed = 1;
}

void
undel_putline(void)
{
	if (!havedline)
		return;
	doc_insline(ED.d, ED.cl, (const unsigned char *)"", 0);
	line_copy(ed_line(ED.cl), &dline);
	ED.d->changed = 1;
}

/* the buffers in the work file: "B n lines sentence", then each line
   as its length, the text and the attributes */
void
cut_save(FILE *f)
{
	int i, k;

	for (i = 0; i < NBUF; i++) {
		if (buf[i].n == 0)
			continue;
		fprintf(f, "B %d %d %d\n", i, buf[i].n, buf[i].sentence);
		for (k = 0; k < buf[i].n; k++) {
			fprintf(f, "%d ", buf[i].l[k].len);
			fwrite(buf[i].l[k].s, 1, buf[i].l[k].len, f);
			fwrite(buf[i].l[k].a, 1, buf[i].l[k].len, f);
			putc('\n', f);
		}
	}
}

int
cut_load1(FILE *f, int i, int n, int sentence)
{
	unsigned char *s, *a;
	int k, len;

	if (i < 0 || i >= NBUF)
		return -1;
	bfree(&buf[i]);
	buf[i].sentence = sentence;
	for (k = 0; k < n; k++) {
		if (fscanf(f, "%d", &len) != 1 || len < 0 || len > CUTMAX || getc(f) != ' ')
			return -1;
		s = (unsigned char *)xrealloc(NULL, len + 1);
		a = (unsigned char *)xrealloc(NULL, len + 1);
		if ((int)fread(s, 1, len, f) != len || (int)fread(a, 1, len, f) != len) {
			xfree(s);
			xfree(a);
			return -1;
		}
		getc(f);
		badd(&buf[i], s, a, len);
		xfree(s);
		xfree(a);
	}
	return 0;
}
