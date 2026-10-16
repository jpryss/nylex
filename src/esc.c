/*
 * esc.c - the escape sequences (Appendix C of the manual).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

char		funcstr[4][KSLEN + 1];	/* 0 find/get, 1 repeat, 2 merge, 3 extract */
static char	escbuf[64];
static int	nesc, lastesc;
static char	prevesc[64];

extern int	pd_lpp;
extern void	ed_message(const char *s);
extern int	ed_parastart(int i);
extern int	ed_paraend(int i);

/* a key of the sequence being read (kept for ESC =) */
static int
ek(void)
{
	int k = in_key();

	if (nesc < (int)sizeof escbuf - 1 && k != K_NONE)
		escbuf[nesc++] = (char)KC(k);
	return k;
}

static int
eku(void)
{
	return upcase(KC(ek()));
}

/* A string after a prompt on the cursor's row; -1 if given up. */
int
read_string(const char *prompt, char *buf, int max, int *term)
{
	int row = ED.cl - ED.top, n;

	if (row < 0 || row >= t_rows)
		row = 0;
	if (term)
		*term = CR;
	n = ui_line(row, prompt, buf, max, 0, term);
	ED.dirty = 1;
	return n;
}

/* a message on the cursor's row until Return (not while a program runs) */
static void
show(const char *s)
{
	int k;

	if (shutup)
		return;
	if (in_depth() > 0 && !in_paused()) {
		/* "typeahead is suspended": the user answers it */
	}
	for (;;) {
		int row = ED.cl - ED.top;

		ed_draw();
		t_clearrow(row);
		t_puts(row, 0, s, vdu_bgattr);
		t_move(row, (int)strlen(s) < t_cols ? (int)strlen(s) : t_cols - 1);
		t_refresh();
		k = t_getkey();
		if (k == '\r' || k == '\n' || k == K_NONE)
			break;
		if (k == ESC) {
			k = t_getkey();
			if (k == 'E' || k == 'e' || k == 'Z' || k == 'z')
				break;
		}
	}
}

static void
typeat(const char *s)
{
	ed_typestr(s);
}

void
esc_clearfuncs(void)
{
	int i;

	for (i = 0; i < 4; i++)
		funcstr[i][0] = 0;
}

/* ESC S n: see and set keystore n in background mode */
void
esc_setks(int n)
{
	char b[KSLEN + 1], p[4];
	int k, len, row, first = 1;

	strcpy(b, keystore[n]);
	len = (int)strlen(b);
	p[0] = (char)('0' + n);
	p[1] = '=';
	p[2] = 0;
	for (;;) {
		row = ED.cl - ED.top;
		if (!shutup && in_depth() == 0) {
			ed_draw();
			t_clearrow(row);
			t_puts(row, 0, p, vdu_bgattr);
			t_puts(row, 2, b, vdu_bgattr);
			t_move(row, 2 + len < t_cols ? 2 + len : t_cols - 1);
		}
		k = in_key();
		if (k == K_NONE || k == CR)
			break;
		if (k == DEL && !(k & K_LIT)) {
			if (len > 0)
				b[--len] = 0;
			continue;
		}
		if (k == CTL('C'))
			break;
		{
			char v[4];

			if (in_prognow())
				vis_of(k, v);
			else {
				v[0] = (char)KC(k);
				v[1] = 0;
			}
			if (first) {
				len = 0;
				first = 0;
			}
			if (len + (int)strlen(v) <= KSLEN) {
				strcpy(b + len, v);
				len += (int)strlen(v);
			}
		}
	}
	b[len] = 0;
	strcpy(keystore[n], b);
	ED.dirty = 1;
}

/* ESC # and ESC @: right align or centre between the stops around the cursor */
static void
align(int how)
{
	struct ruler r;
	struct line *l = ed_line(ED.cl);
	int a, b, e, len, start, end, k;
	unsigned char buf[MAXW + 1], ab[MAXW + 1];

	ed_ruler(&r);
	a = ruler_prev(&r, ED.cc + 1);
	if (a < 0)
		a = 0;
	end = ruler_next(&r, ED.cc, 0);
	if (end < 0 || (r.right > ED.cc && r.right < end))
		end = r.right > ED.cc ? r.right : t_cols - 1;
	for (b = a; b < ED.cc && line_ch(l, b) == ' '; b++)
		;
	for (e = ED.cc; e > b && line_ch(l, e - 1) == ' '; e--)
		;
	if (e <= b) {
		/* nothing before the cursor: the text of the line from it */
		b = ED.cc;
		while (b < l->len && l->s[b] == ' ')
			b++;
		for (e = b; e < l->len && e < end; e++)
			;
		while (e > b && l->s[e - 1] == ' ')
			e--;
		if (e <= b)
			return;
	}
	len = e - b;
	memcpy(buf, l->s + b, len);
	memcpy(ab, l->a + b, len);
	for (k = b; k < e; k++)
		line_put(l, k, ' ', 0);
	start = how == '@' ? a + (end - a - len) / 2 : end - len;
	if (start < 0)
		start = 0;
	for (k = 0; k < len; k++)
		line_put(l, start + k, buf[k], ab[k]);
	line_trim(l);
	ED.cc = end;		/* LEX-68 leaves the cursor at the stop */
	ED.d->changed = 1;
}

/* ESC Line feed: the rest of the paragraph becomes a new one below */
static void
splitpara(void)
{
	struct line *l = ed_line(ED.cl);
	struct ruler r;
	int e = ed_paraend(ED.cl), i, ind, n;

	ed_ruler(&r);
	ind = (r.kind == 'F' || r.kind == 'J') && r.left > 0 ? r.left : 0;
	n = l->len > ED.cc ? l->len - ED.cc : 0;
	doc_insline(ED.d, ED.cl + 1, (const unsigned char *)"", 0);
	i = doc_insline(ED.d, ED.cl + 2, (const unsigned char *)"", 0);
	l = ed_line(ED.cl);
	{
		unsigned char sp[MAXW + 1];
		struct line *m = ed_line(i);

		memset(sp, ' ', ind);
		line_set(m, sp, ind);
		if (n)
			line_ins(m, ind, l->s + ED.cc, n, 0);
		if (n)
			memcpy(m->a + ind, l->a + ED.cc, n);
	}
	l->len = ED.cc;
	line_trim(l);
	(void)e;
	if (r.kind == 'F' || r.kind == 'J') {
		int scl = ED.cl, scc = ED.cc;

		ED.cl = i;
		ED.cc = ind;
		ed_justify(0);
		ED.cl = scl;
		ED.cc = scc;
	}
	ED.d->changed = 1;
}

/* ESC &: a wide line, continued on the next row */
static void
wideline(void)
{
	struct line *l = ed_line(ED.cl);

	if (l->flags & LF_CONT)
		return;
	l->flags |= LF_CONT;
	if (ED.cl + 1 >= ED.d->n)
		ed_line(ED.cl + 1);
	ED.dirty = 1;
}

static void
copyline(int dir)
{
	struct line *l = ed_line(ED.cl), *o;
	struct line t;
	int k;

	if (ED.cl + dir < 0)
		return;
	o = ed_line(ED.cl + dir);
	l = ed_line(ED.cl);
	memset(&t, 0, sizeof t);
	line_copy(&t, l);
	undel_line(&t);
	line_free(&t);
	for (k = ED.cc; k < o->len || k < l->len; k++) {
		if (ED.protect && !ed_unprot(ED.cl, k))
			continue;
		line_put(l, k, line_ch(o, k), (line_at(o, k) & AT_MASK) | (line_at(l, k) & (AT_UNPROT | AT_BG)));
	}
	line_trim(l);
	ED.d->changed = 1;
}

/* the end of the previous (dir -1) or next word */
static void
wordend(int dir)
{
	struct line *l;

	if (dir > 0) {
		l = ed_line(ED.cl);
		if (line_ch(l, ED.cc) != ' ' && line_ch(l, ED.cc + 1) == ' ')
			ED.cc++;
		for (;;) {
			l = ed_line(ED.cl);
			while (ED.cc < l->len && l->s[ED.cc] == ' ')
				ED.cc++;
			if (ED.cc < l->len)
				break;
			if (ED.cl + 1 >= ED.top + ED.rows || ED.cl + 1 >= ED.d->n)
				return;
			ED.cl++;
			ED.cc = 0;
		}
		while (ED.cc + 1 < l->len && l->s[ED.cc + 1] != ' ')
			ED.cc++;
		return;
	}
	for (;;) {
		l = ed_line(ED.cl);
		if (ED.cc > l->len)
			ED.cc = l->len;
		while (ED.cc > 0 && line_ch(l, ED.cc) != ' ')
			ED.cc--;
		while (ED.cc > 0 && line_ch(l, ED.cc) == ' ')
			ED.cc--;
		if (line_ch(l, ED.cc) != ' ')
			return;
		if (ED.cl <= ED.top) {
			ED.cc = 0;
			return;
		}
		ED.cl--;
		ED.cc = ed_line(ED.cl)->len;
	}
}

static void
lastonscreen(void)
{
	int i, bl = ED.top, bc = 0;

	if (ED.protect) {
		int l = ED.top + ED.rows - 1, c = t_cols - 1;

		if (ed_nextunprot(&l, &c, -1)) {
			ED.cl = l;
			ED.cc = c;
		}
		return;
	}
	for (i = ED.top; i < ED.top + ED.rows && i < ED.d->n; i++) {
		struct line *l = &ED.d->l[i];
		int e = l->len;

		while (e > 0 && l->s[e - 1] == ' ')
			e--;
		if (e > 0) {
			bl = i;
			bc = e;
		}
	}
	ED.cl = bl;
	ED.cc = bc < t_cols ? bc : t_cols - 1;
}

static void
nextpara(int dir)
{
	int i = ED.cl;

	if (dir > 0) {
		i = ed_paraend(i) + 1;
		while (i < ED.d->n && (ED.d->l[i].len == 0 || (ED.d->l[i].flags & LF_RULER) ||
		    ed_parastart(i) != i))
			i++;
		while (i < ED.d->n) {
			struct line *l = &ED.d->l[i];
			int k, blank = 1;

			for (k = 0; k < l->len; k++)
				if (l->s[k] != ' ')
					blank = 0;
			if (!blank && !(l->flags & LF_RULER))
				break;
			i++;
		}
		if (i >= ED.d->n) {
			error("EB");
			return;
		}
	} else {
		int s = ed_parastart(i);

		if (s == i && ED.cc == 0) {
			i = s - 1;
			while (i > 0) {
				struct line *l = &ED.d->l[i];
				int k, blank = 1;

				for (k = 0; k < l->len; k++)
					if (l->s[k] != ' ')
						blank = 0;
				if (!blank && !(l->flags & LF_RULER))
					break;
				i--;
			}
			if (i < 0) {
				error("TB");
				return;
			}
			i = ed_parastart(i);
		} else
			i = s;
	}
	ED.cl = i;
	{
		struct line *l = ed_line(i);
		int k = 0;

		while (k < l->len && l->s[k] == ' ')
			k++;
		ED.cc = k;
	}
}

/* ESC QH: manual hyphenation of the line */
static void
hyphenate(void)
{
	struct line *l;
	int k;

	ED.cc = ed_line(ED.cl)->len;
	for (;;) {
		ed_draw();
		k = in_key();
		if (k == ESC) {
			k = upcase(KC(in_key()));
			if (k == 'E' || k == 'Z')
				break;
			if (k == 'J') {
				ed_justify(1);
				break;
			}
			error("HC");
			continue;
		}
		if (k == CTL('U') && ED.cl > 0)
			ED.cl--;
		else if (k == CTL('J') || k == LF)
			ED.cl++;
		else if (k == CTL('N') || k == CTL('E')) {
			/* the last character of the line goes to the next */
			struct line *n;

			l = ed_line(ED.cl);
			n = ed_line(ED.cl + 1);
			line_trim(l);
			if (l->len == 0)
				continue;
			{
				struct ruler r;
				unsigned char c = l->s[l->len - 1];

				ed_ruler(&r);
				if (r.right > 0 && n->len >= r.right) {
					if (k == CTL('E')) {
						int scl = ED.cl;

						ED.cl++;
						ed_justify(1);
						ED.cl = scl;
					} else {
						error("HF");
						continue;
					}
				}
				n = ed_line(ED.cl + 1);
				l = ed_line(ED.cl);
				if (n->len && n->s[0] != ' ' && l->len >= 2 && l->s[l->len - 2] != ' ' && c != '-') {
					line_ins(n, r.left > 0 ? r.left : 0, &c, 1, 0);
					l->s[l->len - 1] = '-';
				} else {
					line_ins(n, r.left > 0 ? r.left : 0, &c, 1, 0);
					l->len--;
				}
			}
			ED.cc = l->len;
			ED.d->changed = 1;
		} else if (k == CTL('P')) {
			struct line *n;
			struct ruler r;
			int s;

			l = ed_line(ED.cl);
			n = ed_line(ED.cl + 1);
			ed_ruler(&r);
			line_trim(l);
			if (r.right > 0 && l->len >= r.right) {
				error("HC");
				continue;
			}
			for (s = 0; s < n->len && n->s[s] == ' '; s++)
				;
			if (s >= n->len)
				continue;
			if (l->len && l->s[l->len - 1] == '-')
				l->len--;
			line_put(l, l->len, n->s[s], n->a[s]);
			line_del(n, s, 1);
			if (s < n->len && n->s[s] != ' ' && l->s[l->len - 1] != ' ')
				line_put(l, l->len, '-', 0);
			ED.cc = l->len;
			ED.d->changed = 1;
		} else if (k == 'I' || k == 'i') {
			char b[64];

			sprintf(b, "%s?", text(0, 39));
			ed_message(b);
		} else
			error("HC");
	}
	ED.cc = 0;
}

static void
showhyph(void)
{
	struct line *l = ed_line(ED.cl);
	int b = ED.cc, e, hp;
	char b2[120];
	struct ruler r;

	while (b > 0 && line_ch(l, b - 1) != ' ')
		b--;
	for (e = b; e < l->len && l->s[e] != ' '; e++)
		;
	ed_ruler(&r);
	hp = hyph_point(l->s + b, e - b, e - b - 1);
	if (hp > 0)
		sprintf(b2, "%s%.*s-", text(0, 39), hp, (char *)l->s + b);
	else
		sprintf(b2, "%s", text(0, 39));
	show(b2);
}

/* a program run from LEX: QS (the record names it), QT, QW */
static void
forkcmd(int how)
{
	char b[KSLEN + 1], *t;
	long len;
	int term;

	b[0] = 0;
	if (how == 'S') {
		if (read_string(text(0, 54), b, KEYLEN + 1, &term) < 0)
			return;
		if (rec_text(b, &t, &len, '_') < 0) {
			error("NK");
			return;
		}
		{
			int n = len > 40 ? 40 : (int)len;

			memcpy(b, t, n);
			b[n] = 0;
			while (n > 0 && b[n - 1] == ' ')
				b[--n] = 0;
		}
		xfree(t);
		fork_prog(b, 1);
		return;
	}
	if (read_string(text(0, 55), b, KSLEN, &term) < 0)
		return;
	fork_prog(b, how == 'T');
}

static void
markpos(int n)
{
	ED.posl[n] = ED.cl;
	ED.posc[n] = ED.cc;
}

static void
gotopos(int n)
{
	if (n < 0 || n > 9 || ED.posl[n] < 0) {
		ED.cl = ED.top;
		ED.cc = 0;
		return;
	}
	if (ED.posl[n] < ED.top || ED.posl[n] >= ED.top + ED.rows) {
		error("MS");
		return;
	}
	ED.cl = ED.posl[n];
	ED.cc = ED.posc[n];
}

/* ESC M n: the ruler n into the text, and in force */
static void
showruler(int n)
{
	struct ruler r;
	const char *s;

	if (n == 0) {
		ed_ruler(&r);
		s = r.s;
	} else {
		s = ED.rulers[n];
		if (!s[0]) {
			if (n >= 6 && n <= 8) {
				static char tabs[MAXW + 1];
				int i;

				for (i = 0; i < t_cols; i++)
					tabs[i] = i % 8 ? '.' : 'T';
				tabs[t_cols] = 0;
				s = tabs;
			} else if (n == 9) {
				static char dots[MAXW + 1];

				memset(dots, '.', t_cols);
				dots[t_cols] = 0;
				s = dots;
			} else {
				error("MC");
				return;
			}
		}
	}
	if (ED.protect) {
		error("PM");
		return;
	}
	doc_insline(ED.d, ED.cl, (const unsigned char *)s, (int)strlen(s));
	ED.d->l[ED.cl].flags = LF_RULER;
	ED.cl++;
	ED.cc = 0;		/* as LEX-68: the start of the line under it */
	ED.ignrul = 0;
	ED.useover = 0;
}

static void
userruler(int n, int ignore)
{
	const char *s = ED.rulers[n];

	if (n == 0)
		return;
	if (!s[0]) {
		char b[MAXW + 1];
		int i;

		for (i = 0; i < t_cols; i++)
			b[i] = (n == 9 || i % 8) ? '.' : 'T';
		b[t_cols] = 0;
		ruler_parse(&ED.over, b, t_cols);
	} else
		ruler_parse(&ED.over, s, (int)strlen(s));
	ED.useover = ED.cl + 1;
	if (ignore)
		ED.ignrul = 1;
}

static void
storeruler(int n)
{
	struct line *l = ed_line(ED.cl);

	if (!isruler(l->s, l->len)) {
		error("RI");
		return;
	}
	memcpy(ED.rulers[n], l->s, l->len);
	ED.rulers[n][l->len] = 0;
}

static void
tabinruler(int add)
{
	struct ruler r;
	int k;

	ed_ruler(&r);
	if (add) {
		int c = '.';

		for (k = ED.cc - 1; k >= 0; k--)
			if (k < r.len && strchr("TLRCJF#", r.s[k]) && r.s[k]) {
				c = r.s[k];
				if (c == 'F' || c == 'J' || c == 'C')
					c = 'T';
				break;
			}
		if (c == '.')
			c = 'T';
		while (r.len <= ED.cc)
			r.s[r.len++] = '.';
		r.s[r.len] = 0;
		r.s[ED.cc] = (char)c;
	} else {
		for (k = ED.cc - 1; k >= 0; k--)
			if (k < r.len && strchr("TLRC#", r.s[k]) && r.s[k]) {
				r.s[k] = '.';
				break;
			}
	}
	ruler_parse(&ED.over, r.s, r.len);
	ED.useover = ED.cl + 1;
}

static void
showattrs(void)
{
	char b[80];
	int i;

	strcpy(b, text(0, 57));
	for (i = 1; i <= 5; i++) {
		char c[2];

		c[0] = (ED.attr & (1 << (i - 1))) ? (char)('0' + i) : '-';
		c[1] = 0;
		strcat(b, c);
	}
	show(b);
}

static void
lineattrs(void)
{
	struct line *l = ed_line(ED.cl);
	char b[MAXW + 1];
	int k;

	for (k = 0; k < l->len && k < t_cols; k++) {
		int a = l->a[k] & AT_MASK, v = (a >> 1) & 15;

		b[k] = (char)((a & 1) ? (v ? 'a' + v - 1 : '_') : (v ? 'A' + v - 1 : ' '));
	}
	b[k] = 0;
	if (ED.cl + 1 - ED.top < ED.rows) {
		int row = ED.cl + 1 - ED.top, kk;

		for (;;) {
			ed_draw();
			t_clearrow(row);
			t_puts(row, 0, b, vdu_bgattr);
			t_refresh();
			kk = in_key();
			if (kk == CR || kk == LF || kk == K_NONE)
				break;
			if (kk == ESC) {
				in_key();
				break;
			}
		}
	}
}

/* ESC A, ESC B followed by a digit, a letter: attributes */
int
esc_attr(int k)
{
	int c = eku();
	struct line *l;
	int i;

	if (k == 'A') {
		if (c == '0')
			ED.attr = 0;
		else if (c >= '1' && c <= '5')
			ED.attr |= 1 << (c - '1');
		else if (c >= 'A' && c <= 'O')
			ED.attr = (ED.attr & 1) | ((c - 'A' + 1) << 1);
		else
			error("AC");
		return 0;
	}
	/* B */
	if (c == '0') {
		ED.attr = 0;
		return 0;
	}
	if (c >= '1' && c <= '5') {
		ED.attr &= ~(1 << (c - '1'));
		return 0;
	}
	switch (c) {
	case 'L':
		l = ed_line(ED.cl);
		for (i = ED.cc; i < l->len; i++)
			l->a[i] = (unsigned char)((l->a[i] & ~AT_MASK) | ED.attr);
		ED.d->changed = 1;
		break;
	case 'W':
		l = ed_line(ED.cl);
		i = ED.cc;
		for (; i < l->len && l->s[i] == ' '; i++)
			l->a[i] = (unsigned char)((l->a[i] & ~AT_MASK) | ED.attr);
		for (; i < l->len && l->s[i] != ' '; i++)
			l->a[i] = (unsigned char)((l->a[i] & ~AT_MASK) | ED.attr);
		ED.d->changed = 1;
		break;
	case 'T':
		lineattrs();
		break;
	case 'U':
		showattrs();
		break;
	case 'A':
	case 'D':
	case 'H':
	case 'V':
		/* sorts */
		{
			extern int esc_sortk(int a, int b);

			if (c == 'H' || c == 'V')
				esc_sortk(c, eku());
			else
				esc_sortk(c, 0);
		}
		break;
	default:
		error("AC");
	}
	return 0;
}

static int
exit_edit(int abandon)
{
	if (abandon && I.sure && in_depth() == 0) {
		char q[100];
		int row = ED.cl - ED.top;

		sprintf(q, "%s%s", text(0, 29), text(0, 30));
		ed_draw();
		t_clearrow(row);
		if (ui_yesno(row, 0, q) != 1)
			return 0;
	}
	ED.exitcode = abandon ? EX_ABANDON : EX_EXIT;
	return 1;
}

static void
info(const char *s)
{
	char b[200];

	sprintf(b, "%.190s", s);
	show(b);
}

/*
 * The escape sequences.  Returns 1 if the edit session ends.
 */
int
ed_esc(void)
{
	int k, c, r = 0;
	char b[MAXW + 1];

	nesc = 0;
	k = ek();
	if (k & K_LIT)
		k = KC(k);
	if (k >= 'a' && k <= 'z')
		k = upcase(k);
	switch (k) {
	case ' ':
		break;
	case CR:
		ed_cr(1);
		break;
	case LF:
		splitpara();
		break;
	case TAB:
		ed_tab(1);
		break;
	case CTL('X'):
		undel_putline();
		break;
	case '#':
	case '@':
		align(k);
		break;
	case '&':
		wideline();
		break;
	case '\\':
		ED.vert = !ED.vert;
		break;
	case '+':
		calc_run(0);
		break;
	case '=':
		if (lastesc && prevesc[0]) {
			char s[66];

			s[0] = ESC;
			strcpy(s + 1, prevesc);
			in_kbd(s, (long)strlen(s));
		}
		return 0;
	case '(':
		cut_mark();
		markpos(0);
		break;
	case ')':
		c = eku();
		if (c >= '0' && c <= '9')
			cut_block(c, eku());
		else if (c >= 'T' && c <= 'Z' && c != 'S')
			cut_block(c, eku());
		else
			cut_block(0, c);
		break;
	case '.':
		c = eku();
		if (c >= 'T' && c <= 'Z')
			cut_sentence(c, eku());
		else
			cut_sentence(0, c);
		break;
	case '*':
		c = eku();
		if ((c >= 'T' && c <= 'Z') || (c >= '0' && c <= '9'))
			paste(c, eku());
		else
			paste(0, c);
		break;
	case '}':
		paste_blanks(eku());
		break;
	case '<':
		gotopos(KC(ek()) - '0');
		break;
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		in_prog(keystore[k - '0'], (long)strlen(keystore[k - '0']), 0);
		break;
	case 'A':
	case 'B':
		esc_attr(k);
		break;
	case 'C':
		c = eku();
		switch (c) {
		case 'A':
			if (ED.protect)
				break;
			{
				struct line *l = ed_line(ED.cl);

				if (l->len > ED.cc)
					l->len = ED.cc;
				while (ED.d->n > ED.cl + 1)
					doc_delline(ED.d, ED.d->n - 1);
				ED.d->changed = 1;
			}
			break;
		case 'B':
		case 'F':
			{
				int i, kk;

				for (i = ED.cl; i < ED.top + ED.rows && i < ED.d->n; i++) {
					struct line *l = &ED.d->l[i];

					for (kk = i == ED.cl ? ED.cc : 0; kk < l->len; kk++) {
						/* in protect mode the protected cells are the
						   background, the fields the foreground */
						int bg = ED.protect ? !(l->a[kk] & AT_UNPROT) : (l->a[kk] & AT_BG) != 0;

						if (c == 'B' ? bg : !bg) {
							if (ED.protect && c == 'B')
								continue;
							l->s[kk] = (unsigned char)(ED.protect ? M.paint : ' ');
							if (!ED.protect)
								l->a[kk] = 0;
						}
					}
					line_trim(l);
				}
				ED.d->changed = 1;
			}
			break;
		case 'P':
			{
				int i, kk;

				for (i = ED.cl; i < ED.d->n; i++) {
					struct line *l = &ED.d->l[i];

					for (kk = 0; kk < l->len; kk++) {
						if (!ED.protect || !(l->a[kk] & AT_UNPROT) || l->s[kk] == M.paint)
							l->s[kk] = ' ';
						l->a[kk] &= AT_MASK;
					}
					line_trim(l);
				}
				for (i = 0; i < ED.d->n; i++)
					for (kk = 0; kk < ED.d->l[i].len; kk++)
						ED.d->l[i].a[kk] &= AT_MASK;
				ED.protect = 0;
				ED.d->changed = 1;
			}
			break;
		case 'D':
			b[0] = 0;
			if (read_string(text(0, 56), b, 8, NULL) > 0) {
				char e[10];

				sprintf(e, "%s%.6s", b[0] == '.' ? "" : ".", b);
				ed_load_details(e);
			}
			break;
		case 'S':
			ED.statusline = 0;
			break;
		case 'T':
			esc_cond();
			break;
		case 'U':
			esc_clearfuncs();
			break;
		default:
			error("AC");
		}
		break;
	case 'E':
		r = exit_edit(0);
		break;
	case 'F':
		esc_search('F', 0);
		break;
	case 'G':
		esc_search('G', 0);
		break;
	case 'H':
		{
			char ec[3];

			ec[0] = (char)errcode[0];
			ec[1] = (char)errcode[1];
			ec[2] = 0;
			sprintf(b, "%s- %s", text(0, 37), errtext(ec));
		}
		show(b);
		errcode[0] = errcode[1] = 'Z';
		break;
	case 'I':
		if (ED.protect) {
			error("PM");
			break;
		}
		ED.ins = 1;
		break;
	case 'J':
		ed_justify(0);
		break;
	case 'K':
		date_yymmdd(b);
		typeat(b);
		break;
	case 'L':
		ed_status(b);
		show(b);
		break;
	case 'M':
		c = eku();
		if (c >= '0' && c <= '9')
			showruler(c - '0');
		else if (c == 'Z')
			tabinruler(1);
		else {
			esc_merge(c);
		}
		break;
	case 'N':
		rec_nextkey(1, 0);
		break;
	case 'O':
		esc_open();
		break;
	case 'P':
		ed_paginate();
		break;
	case 'Q':
		c = eku();
		switch (c) {
		case 'A': copyline(-1); break;
		case 'B': copyline(1); break;
		case 'C':
			{
				struct line *l = ed_line(ED.cl);
				int a = line_ch(l, ED.cc), bb = line_ch(l, ED.cc + 1);
				int aa = line_at(l, ED.cc), ab = line_at(l, ED.cc + 1);

				line_put(l, ED.cc, bb, ab);
				line_put(l, ED.cc + 1, a, aa);
				line_trim(l);
				ED.d->changed = 1;
			}
			break;
		case 'D':
			if (!ED.protect)
				ED.ins = !ED.ins;
			break;
		case 'E':
			ED.cl = ED.d->n;
			while (ED.cl > 0 && ED.d->l[ED.cl - 1].len == 0)
				ED.cl--;
			ED.cc = 0;
			ED.top = ED.cl - ED.rows + 1;
			break;
		case 'F': nextpara(1); break;
		case 'G': nextpara(-1); break;
		case 'H': hyphenate(); break;
		case 'I': showhyph(); break;
		case 'J': ed_justify(1); break;
		case 'K': ED.cc = 0; break;
		case 'L': lastonscreen(); break;
		case 'M': ED.cl = ED.cc = ED.top = 0; break;
		case 'N': time_hms(b, M.timesep); typeat(b); break;
		case 'R': t_redraw(); break;
		case 'S': forkcmd('S'); break;
		case 'T': forkcmd('T'); break;
		case 'U': wordend(-1); break;
		case 'V': wordend(1); break;
		case 'W': forkcmd('W'); break;
		case 'X':
			ED.exitcode = EX_REEDITPOS;
			r = 1;
			break;
		case 'Y': rec_nextkey(1, 1); break;
		default: error("AC");
		}
		break;
	case 'R':
		c = eku();
		if (c >= '0' && c <= '9') {
			in_lastkeys(keystore[c - '0'], KSLEN);
			break;
		}
		switch (c) {
		case 'C': in_clearlast(); break;
		case 'D': typeat(ED.mode == ED_PLAY ? "" : ED.path); break;
		case 'G':
			{
				extern const char *esc_mergename(void);

				info(esc_mergename());
			}
			break;
		case 'I':
			sprintf(b, "%s%s %s %s", text(1, 40), montkey, instkey, pnstkey);
			info(b);
			break;
		case 'K':
			b[0] = 0;
			if (read_string(text(0, 40), b, 32, NULL) > 0)
				perform(b, 0);
			break;
		case 'L': ed_status(b); typeat(b); break;
		case 'M': typeat(vmfname); break;
		case 'N':
			if (ED.mode != ED_PLAY) {
				char key[12];

				reg_key(key, ED.name);
				key[10] = 0;
				typeat(key);
			}
			break;
		case 'O':
			{
				extern char curopt[16];

				typeat(curopt);
			}
			break;
		case 'S':
			{
				char s[KSLEN + 1];

				in_lastkeys(s, KSLEN);
				ED.cc = 0;
				typeat(s);
			}
			break;
		case 'V': sprintf(b, ")%s", vdukey); typeat(b); break;
		case 'W': typeat(workname); break;
		default: error("AC");
		}
		break;
	case 'S':
		c = eku();
		if (c >= '0' && c <= '9') {
			esc_setks(c - '0');
			break;
		}
		switch (c) {
		case '(':
			c = KC(ek());
			if (c >= '0' && c <= '9')
				markpos(c - '0');
			break;
		case 'B': ED.bgmode = 1; break;
		case 'D':
			sprintf(b, "%d", I.delay);
			if (read_string(text(0, 35), b, 6, NULL) > 0)
				I.delay = atoi(b);
			break;
		case 'E': showerr = 1; break;
		case 'F':
			{
				char spec[80];
				int n = 0, kk;

				while ((kk = in_key()) != CR && kk != K_NONE && n < 79)
					spec[n++] = (char)KC(kk);
				spec[n] = 0;
				if (foreign_set(spec) < 0) {
					error("FF");
					in_abort();
				}
			}
			break;
		case 'G': ED.ignrul = 1; break;
		case 'H': shush = 1; break;
		case 'L': ED.statusline = 1; break;
		case 'M':
			{
				char nm[16];
				int n = 0, kk;

				while ((kk = in_key()) != CR && kk != K_NONE && n < 15)
					nm[n++] = (char)KC(kk);
				nm[n] = 0;
				mask_set(nm);
			}
			break;
		case 'P': ed_setprotect(1); break;
		case 'Q':
			c = KC(ek());
			ED.sqch = c == CR ? '\n' : c;
			break;
		case 'S': vdu_seq2(KC(ek()) - '0'); break;
		case 'T': vdu_seq4(KC(ek()) - '0'); break;
		case 'U': shutup = 1; break;
		case 'V':
			{
				int d = KC(in_key()), n = 0, kk;
				char s[256];

				while ((kk = in_key()) != K_NONE && KC(kk) != d && n < 255)
					s[n++] = (char)KC(kk);
				vdu_send(s, n);
			}
			break;
		case 'W': ED.pagewarn = 1; break;
		case 'X':
			{
				int d = t_getkey(), kk, n = 0;
				char s[200];

				s[0] = 0;
				while ((kk = t_getbyte(-1)) >= 0 && kk != d && n < 180) {
					sprintf(s + n, "%03d ", kk);
					n += 4;
					ed_message(s);
					ed_draw();
					t_refresh();
				}
			}
			break;
		default: error("AC");
		}
		break;
	case 'T':
		date_short(b);
		typeat(b);
		break;
	case 'U':
		c = eku();
		if (c >= '0' && c <= '9')
			userruler(c - '0', 0);
		else if (c == 'U')
			userruler(KC(ek()) - '0', 1);
		else if (c == 'B')
			ED.bgmode = 0;
		else if (c == 'F')
			foreign_unset();
		else if (c == 'M')
			mask_unset();
		else if (c == 'P')
			ed_setprotect(0);
		else
			error("AC");
		break;
	case 'V':
		ED.exitcode = EX_REEDIT;
		r = 1;
		break;
	case 'W':
		c = eku();
		if (c >= '0' && c <= '9')
			storeruler(c - '0');
		else {
			esc_extract(c);
		}
		break;
	case 'X':
		esc_repeat();
		break;
	case 'Y':
		date_full(b);
		typeat(b);
		break;
	case 'Z':
		r = exit_edit(1);
		break;
	case '-':
		c = eku();
		switch (c) {
		case 'D':
		case 'P': undel_putchar(); break;
		case 'W': undel_putword(); break;
		case 'X':
		case 'V': undel_putline(); break;
		case 'I': ED.ins = 0; break;
		case 'N': rec_nextkey(-1, 0); break;
		case 'G': esc_search('G', 1); break;
		case '\\': ED.vert = 0; break;
		case 'M':
			if (eku() == 'Z')
				tabinruler(0);
			else
				error("MC");
			break;
		case 'S':
			c = eku();
			switch (c) {
			case 'E': showerr = 0; break;
			case 'G':
				ED.ignrul = 0;
				ED.useover = 0;
				break;
			case 'H': shush = 0; ED.dirty = 1; break;
			case 'L': ED.statusline = 0; break;
			case 'U': shutup = 0; shush = 0; t_redraw(); break;
			case 'W': ED.pagewarn = 0; break;
			default: error("AC");
			}
			break;
		default: error("AC");
		}
		break;
	default:
		if (k == M.endkey)
			esc_record(eku(), 0);
		else if (k == M.vend)
			esc_record(eku(), 1);
		else
			error("AC");
		break;
	}
	if (k != '=') {
		escbuf[nesc] = 0;
		strcpy(prevesc, escbuf);
		lastesc = 1;
	}
	return r;
}
