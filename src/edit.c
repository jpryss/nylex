/*
 * edit.c - the editor: the screen of text, typing, the control functions
 * (Appendix B), rulers, word wrap, justification, tab stops and the
 * edit session itself.  The escape sequences are in esc.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lex.h"

struct edstate	ED;
char		keystore[NKS][KSLEN + 1];

static char	msg[200];
static int	msgon;
extern void	(*ui_drawfn)(void);

/* print details of the document type (*.EXT) */
int	pd_lm = 0, pd_alt = 0, pd_widow = 3, pd_orphan = 3, pd_lpp = 60, pd_total = 0, pd_foot = 'A';

static int
width(void)
{
	return t_cols < MAXW ? t_cols : MAXW;
}

struct line *
ed_line(int i)
{
	return doc_line(ED.d, i);
}

void
ed_message(const char *s)
{
	strncpy(msg, s, sizeof msg - 1);
	msg[sizeof msg - 1] = 0;
	msgon = 1;
	ED.dirty = 1;
}

static int
iscontrolline(int i)
{
	struct line *l;

	if (i < 0 || i >= ED.d->n)
		return 0;
	l = &ED.d->l[i];
	return l->len >= 2 && l->s[0] == '.' && l->s[1] >= 'A' && l->s[1] <= 'Z';
}

void
ed_markrulers(void)
{
	int i;

	for (i = 0; i < ED.d->n; i++) {
		struct line *l = &ED.d->l[i];

		if (isruler(l->s, l->len))
			l->flags |= LF_RULER;
		else
			l->flags &= ~LF_RULER;
	}
}

static void
defruler(struct ruler *r)
{
	if (ED.rulers[1][0])
		ruler_parse(r, ED.rulers[1], (int)strlen(ED.rulers[1]));
	else {
		char b[MAXW + 1];
		int w = width(), m = w > 72 ? 69 : w - 2;

		memset(b, '.', w);
		b[0] = 'F';
		b[m] = 'M';
		b[w] = 0;
		ruler_parse(r, b, w);
	}
}

/* the ruler in force at a line */
void
ed_rulerat(int line, struct ruler *r)
{
	int i;

	for (i = line < ED.d->n ? line : ED.d->n - 1; i >= 0; i--)
		if (ED.d->l[i].flags & LF_RULER)
			break;
	if (ED.useover && (ED.ignrul || i < ED.useover - 1)) {
		*r = ED.over;
		return;
	}
	if (i >= 0) {
		ruler_parse(r, (char *)ED.d->l[i].s, ED.d->l[i].len);
		return;
	}
	if (ED.useover) {
		*r = ED.over;
		return;
	}
	defruler(r);
}

void
ed_ruler(struct ruler *r)
{
	ed_rulerat(ED.cl, r);
}

/* Protection */

int
ed_unprot(int l, int c)
{
	if (!ED.protect)
		return 1;
	return (line_at(ed_line(l), c) & AT_UNPROT) != 0;
}

/* the next (dir 1) or previous (-1) unprotected cell from (l, c), itself
   included; 0 if there is none */
int
ed_nextunprot(int *l, int *c, int dir)
{
	int i = *l, k = *c, w = width();

	if (!ED.protect)
		return 1;
	while (i >= 0 && i < ED.d->n) {
		struct line *ln = &ED.d->l[i];

		while (k >= 0 && k < w) {
			if (line_at(ln, k) & AT_UNPROT) {
				*l = i;
				*c = k;
				return 1;
			}
			k += dir;
		}
		i += dir;
		k = dir > 0 ? 0 : w - 1;
	}
	return 0;
}

static void
firstunprot(void)
{
	int l = ED.top, c = 0;

	if (ed_nextunprot(&l, &c, 1)) {
		ED.cl = l;
		ED.cc = c;
	}
}

void
ed_setprotect(int on)
{
	int i, k, any = 0;

	for (i = 0; i < ED.d->n; i++) {
		struct line *l = &ED.d->l[i];

		for (k = 0; k < l->len; k++) {
			if (on && l->s[k] == M.paint) {
				l->a[k] = (unsigned char)((l->a[k] & AT_MASK) | AT_UNPROT | AT_BG);
				any = 1;
			} else
				l->a[k] &= (unsigned char)(on ? ~(AT_UNPROT | AT_BG) : ~AT_UNPROT);
		}
	}
	if (on && !any) {
		error("NP");
		return;
	}
	ED.protect = on;
	if (on)
		ED.bgmode = 0;
	if (on && !ed_unprot(ED.cl, ED.cc)) {
		int l = ED.cl, c = ED.cc;

		if (ed_nextunprot(&l, &c, 1)) {
			ED.cl = l;
			ED.cc = c;
		}
	}
	ED.dirty = 1;
}

/* The screen */

int
ed_screenrow(int line)
{
	return line - ED.top;
}

static void
fixtop(void)
{
	if (ED.cl < ED.top)
		ED.top = ED.cl;
	if (ED.cl >= ED.top + ED.rows)
		ED.top = ED.cl - ED.rows + 1;
	if (ED.top < 0)
		ED.top = 0;
}

void
ed_status(char *b)
{
	struct ruler r;
	int i, page = P.pagestart, lines4 = 0, sp4 = 4;
	char pcc[13];

	strcpy(pcc, I.pcc);
	for (i = 0; i < ED.cl && i < ED.d->n; i++) {
		struct line *l = &ED.d->l[i];

		if (iscontrolline(i)) {
			if (l->s[1] == 'P') {
				page++;
				lines4 = 0;
			} else if (l->s[1] == 'S' && l->len > 2)
				sp4 = (int)(atof((char *)l->s + 2) * 4 + 0.5);
			else if (l->s[1] == 'C') {
				int k;

				for (k = 0; k < 12; k++)
					pcc[k] = k + 3 < l->len ? (char)l->s[k + 3] : '.';
			}
			continue;
		}
		if (l->flags & (LF_RULER | LF_CONT))
			continue;
		lines4 += sp4 > 0 ? sp4 : 4;
	}
	ed_ruler(&r);
	sprintf(b, "%s%-12.40s L=%4d.%02d A=%5d C=%3d P=%4d .S %d.%02d .C=%s",
	    text(0, 36), ED.mode == ED_PLAY ? "" : ED.name, lines4 / 4, (lines4 % 4) * 25,
	    ED.cl + 1, ED.cc + 1, page, sp4 / 4, (sp4 % 4) * 25, pcc);
}

void
ed_draw(void)
{
	int r, c, w = width(), rows;

	ED.rows = t_rows - (ED.statusline ? 1 : 0);
	rows = ED.rows;
	fixtop();
	if (getenv("LEXTRACE")) {
		FILE *f = fopen(getenv("LEXTRACE"), "a");

		if (f) {
			fprintf(f, "ed_draw shutup=%d shush=%d rows=%d n=%d top=%d\n", shutup, shush, rows, ED.d->n, ED.top);
			fclose(f);
		}
	}
	if (shutup || shush)
		return;
	for (r = 0; r < rows; r++) {
		int i = ED.top + r;

		t_clearrow(r);
		if (i < ED.d->n) {
			struct line *l = &ED.d->l[i];

			for (c = 0; c < l->len && c < w; c++) {
				int a = l->a[c], ta = vdu_attr(a & AT_MASK);

				if (a & AT_BG)
					ta |= vdu_bgattr;
				t_put(r, c, l->s[c], ta);
			}
			if (l->flags & LF_CONT)
				t_put(r, w - 1, M.cont, A_NORM);
		}
		if (i == ED.markl && ED.markc < w)
			t_put(r, ED.markc, ED.markc < ed_line(i)->len ? ed_line(i)->s[ED.markc] : ' ', A_REV);
	}
	if (ED.statusline) {
		char b[200];

		ed_status(b);
		t_clearrow(t_rows - 1);
		t_puts(t_rows - 1, 0, b, vdu_bgattr);
	}
	if (msgon) {
		int mr = ED.statusline ? t_rows - 1 : ED.cl - ED.top;

		/* over the line, which shows after the message (LEX-68) */
		t_puts(mr, 0, "-   ", vdu_bgattr);
		t_puts(mr, 4, msg, vdu_bgattr);
		t_move(mr, 1);
	} else
		t_move(ED.cl - ED.top, ED.cc < w ? ED.cc : w - 1);
}

/* Editing helpers */

static int
cols(void)
{
	return width();
}

static int
blankline(int i)
{
	struct line *l;
	int k;

	if (i < 0 || i >= ED.d->n)
		return 1;
	l = &ED.d->l[i];
	if (l->flags & LF_RULER)
		return 1;
	for (k = 0; k < l->len; k++)
		if (l->s[k] != ' ')
			return 0;
	return 1;
}

/* a line that ends a paragraph: blank, a ruler, a control line, or ~ */
static int
parabreak(int i)
{
	struct line *l;
	int k;

	if (blankline(i) || iscontrolline(i))
		return 1;
	l = &ED.d->l[i];
	for (k = l->len - 1; k >= 0 && l->s[k] == ' '; k--)
		;
	return k >= 0 && I.pcc[1] != '.' && l->s[k] == (unsigned char)I.pcc[1];
}

static int
nolf(int i)
{
	struct line *l;
	int k;

	if (i < 0 || i >= ED.d->n || I.pcc[1] == '.')
		return 0;
	l = &ED.d->l[i];
	for (k = l->len - 1; k >= 0 && l->s[k] == ' '; k--)
		;
	return k >= 0 && l->s[k] == (unsigned char)I.pcc[1];
}

int
ed_parastart(int i)
{
	if (parabreak(i))
		return i;
	while (i > 0 && !parabreak(i - 1) && !nolf(i - 1))
		i--;
	return i;
}

int
ed_paraend(int i)
{
	while (i + 1 < ED.d->n && !parabreak(i + 1) && !nolf(i))
		i++;
	return i;
}

static int
newattr(void)
{
	return (ED.attr & AT_MASK) | (ED.bgmode ? AT_BG : 0);
}

/*
 * Word wrap: while line i reaches the right margin (M), its last words
 * go to the front of the next line of the paragraph, or to a new line.
 */
void
ed_wrap(int i)
{
	struct ruler r;
	int guard = 0;

	ed_rulerat(i, &r);
	if ((r.kind != 'F' && r.kind != 'J') || r.right <= 0)
		return;
	while (i < ED.d->n && guard++ < 500) {
		struct line *l = &ED.d->l[i];
		int lim = r.right, b, e, s, n, nl, joined, k, ind;
		unsigned char *ts, *ta;

		if (l->flags & LF_RULER)
			return;
		line_trim(l);
		if (l->len <= lim)
			return;
		for (b = lim; b > r.left && l->s[b] != ' '; b--)
			;
		if (b <= r.left)
			return;		/* a word as long as the line stays */
		for (s = b; s < l->len && l->s[s] == ' '; s++)
			;
		e = b;
		while (e > 0 && l->s[e - 1] == ' ')
			e--;
		n = l->len - s;
		ts = (unsigned char *)xrealloc(NULL, n + 1);
		ta = (unsigned char *)xrealloc(NULL, n + 1);
		memcpy(ts, l->s + s, n);
		memcpy(ta, l->a + s, n);
		nl = i + 1;
		if (nl >= ED.d->n || parabreak(nl) || nolf(i)) {
			doc_insline(ED.d, nl, (const unsigned char *)"", 0);
			joined = 0;
		} else
			joined = 1;
		l = &ED.d->l[i];
		{
			struct line *m = &ED.d->l[nl];

			ind = r.left > 0 ? r.left : 0;
			for (k = 0; k < m->len && m->s[k] == ' '; k++)
				;
			if (joined) {
				if (k > ind)
					ind = k;
				line_del(m, 0, k);
				line_ins(m, 0, (const unsigned char *)" ", 1, 0);
			}
			line_ins(m, 0, ts, n, 0);
			memcpy(m->a, ta, n);
			if (ind > 0) {
				line_ins(m, 0, (const unsigned char *)"", 0, 0);
				line_pad(m, 0);
				{
					unsigned char sp[MAXW + 1];

					memset(sp, ' ', ind);
					line_ins(m, 0, sp, ind, 0);
				}
			}
			if (ED.cl == i && ED.cc >= s) {
				ED.cl = nl;
				ED.cc = ED.cc - s + ind;
			} else if (ED.cl == nl && joined)
				ED.cc += n + 1 + ind - k;
		}
		xfree(ts);
		xfree(ta);
		l->len = e;
		ED.d->changed = 1;
		i++;
	}
}

/* The end key typed: a record key before it on the line is performed. */
static int
try_perform(void)
{
	struct line *l = ed_line(ED.cl);
	int p, n;
	char key[32];

	for (p = ED.cc - 1; p >= 0 && p >= ED.cc - 20; p--)
		if (l->s[p] == M.startkey && p < l->len)
			break;
	if (p < 0 || p < ED.cc - 20 || ED.cc - p - 1 < 1)
		return 0;
	n = ED.cc - p - 1;
	memcpy(key, l->s + p + 1, n);
	key[n] = 0;
	{
		/* the key goes from the screen */
		int k;

		if (ED.protect) {
			/* on a form only what was typed goes: the cells
			   are painted again */
			int first = -1;

			for (k = p; k < ED.cc && k < l->len; k++)
				if (l->a[k] & AT_UNPROT) {
					l->s[k] = (unsigned char)M.paint;
					if (first < 0)
						first = k;
				}
			ED.cc = first >= 0 ? first : ED.cc;
		} else {
			for (k = p; k < ED.cc && k < l->len; k++) {
				l->s[k] = ' ';
				l->a[k] &= AT_UNPROT | AT_BG;
			}
			line_trim(l);
			ED.cc = p;
		}
	}
	perform(key, 1);
	return 1;
}

static void
nextrow(void)
{
	ED.cl++;
	ED.cc = 0;
}

void
ed_typechar(int c, int lit)
{
	struct line *l;
	struct ruler r;
	int w = cols();

	if (!lit && c == M.endkey && try_perform())
		return;
	if (ED.protect && !ed_unprot(ED.cl, ED.cc)) {
		int ll = ED.cl, cc = ED.cc;

		if (!ed_nextunprot(&ll, &cc, 1)) {
			error("PM");
			return;
		}
		ED.cl = ll;
		ED.cc = cc;
	}
	l = ed_line(ED.cl);
	ed_ruler(&r);
	if (ED.protect) {
		int a = line_at(l, ED.cc);

		line_put(l, ED.cc, c, (a & (AT_UNPROT | AT_BG)) | (ED.attr & AT_MASK));
	} else if (ED.ins && !(l->flags & LF_RULER)) {
		unsigned char ch = (unsigned char)c;

		line_ins(l, ED.cc, &ch, 1, newattr());
		if (l->len > w && !(r.kind == 'F' || r.kind == 'J')) {
			/* pushed off the edge: lost */
			l->len = w;
		}
	} else
		line_put(l, ED.cc, c, newattr());
	ED.d->changed = 1;
	if (ED.vert) {
		if (ED.ins) {
			/* the next character goes below, a line made for it */
		}
		ED.cl++;
		return;
	}
	ED.cc++;
	if ((l->flags & LF_RULER) || isruler(l->s, l->len)) {
		ed_markrulers();
		if (ED.cc >= w)
			nextrow();
		return;
	}
	if (ED.protect) {
		int ll = ED.cl, cc = ED.cc;

		if (!ed_unprot(ll, cc) && ed_nextunprot(&ll, &cc, 1)) {
			/* on to the next field */
			if (ll == ED.cl || cc == 0 || 1) {
				ED.cl = ll;
				ED.cc = cc;
			}
		}
		return;
	}
	if ((r.kind == 'F' || r.kind == 'J') && r.right > 0 &&
	    ((ED.cc - 1 >= r.right && c != ' ') || (ED.ins && l->len > r.right)))
		ed_wrap(ED.cl);
	if (ED.cc >= w) {
		ED.cl++;
		ED.cc = (r.kind == 'F' || r.kind == 'J') && r.left > 0 ? r.left : 0;
	}
}

void
ed_typestr(const char *s)
{
	while (*s)
		ed_typechar((unsigned char)*s++, 1);
}

void
ed_typelit(const char *s)
{
	ed_typestr(s);
}

/* Return: the start of the next line (with margin: ESC Return) */
void
ed_cr(int margin)
{
	struct ruler r;

	ED.cl++;
	ED.cc = 0;
	if (margin) {
		ed_ruler(&r);
		if ((r.kind == 'F' || r.kind == 'J') && r.left > 0)
			ED.cc = r.left;
	}
	if (ED.protect) {
		int l = ED.cl, c = ED.cc;

		if (ed_nextunprot(&l, &c, 1)) {
			ED.cl = l;
			ED.cc = c;
		}
	}
	if (ED.pagewarn)
		ed_pagecheck();
}

/* the calculator field of a # ruler at col: start, end, decimals */
static int
numfield(const struct ruler *r, int at, int *s, int *e)
{
	int k = at;

	if (at < 0 || at >= r->len || r->s[at] != '#')
		return 0;
	while (k > 0 && (r->s[k - 1] == '#' || r->s[k - 1] == ',' ||
	    (r->s[k - 1] == '.' && k > 1 && r->s[k - 2] == '#')))
		k--;
	*s = k;
	k = at;
	while (k + 1 < r->len && (r->s[k + 1] == '#' || r->s[k + 1] == ',' ||
	    (r->s[k + 1] == '.' && k + 2 < r->len && r->s[k + 2] == '#')))
		k++;
	*e = k;
	return 1;
}

/* Tab (back: ESC Tab): to the next stop; C, R and # regions formatted */
void
ed_tab(int back)
{
	struct ruler r;
	struct line *l = ed_line(ED.cl);
	int s, k, next, b, e;

	if (ED.protect) {
		/* the next protected area, then the field after it */
		int ll = ED.cl, c = ED.cc;

		if (back) {
			while (ed_unprot(ll, c) && (c > 0 || ll > 0)) {
				if (--c < 0) {
					ll--;
					c = cols() - 1;
				}
			}
			if (ed_nextunprot(&ll, &c, -1)) {
				while (c > 0 && ed_unprot(ll, c - 1))
					c--;
				ED.cl = ll;
				ED.cc = c;
			}
			return;
		}
		while (ll < ED.d->n && ed_unprot(ll, c)) {
			if (++c >= cols()) {
				ll++;
				c = 0;
			}
		}
		if (ed_nextunprot(&ll, &c, 1)) {
			ED.cl = ll;
			ED.cc = c;
		}
		return;
	}
	ed_ruler(&r);
	if (back) {
		next = ruler_prev(&r, ED.cc);
		ED.cc = next >= 0 ? next : 0;
		if (next >= 0 && r.s[next] == '#') {
			int fs, fe;

			if (numfield(&r, next, &fs, &fe)) {
				ED.cc = fe;
				calc_run(1);
			}
		}
		return;
	}
	for (s = ED.cc - 1 < r.len ? ED.cc - 1 : r.len - 1; s >= 0; s--)
		if (strchr("CRLTFJ", r.s[s]) && r.s[s])
			break;
	next = ruler_next(&r, ED.cc - 1 >= 0 ? ED.cc - 1 : 0, ED.ins);
	if (next >= 0 && next <= ED.cc)
		next = ruler_next(&r, ED.cc, ED.ins);
	if (s >= 0 && (r.s[s] == 'C' || r.s[s] == 'R')) {
		for (b = s; b < ED.cc && line_ch(l, b) == ' '; b++)
			;
		for (e = ED.cc; e > b && line_ch(l, e - 1) == ' '; e--)
			;
		if (e > b) {
			int len = e - b, end, start;
			unsigned char buf[MAXW + 1], ab[MAXW + 1];

			memcpy(buf, l->s + b, len);
			memcpy(ab, l->a + b, len);
			for (k = b; k < e; k++)
				line_put(l, k, ' ', 0);
			if (r.s[s] == 'C') {
				end = r.right > s ? r.right : (next > s ? next : s + len);
				start = s + (end - s - len) / 2;
			} else {
				end = ruler_next(&r, s, 0);
				if (end < 0)
					end = r.right > s ? r.right : s + len;
				start = end - len;
			}
			if (start < 0)
				start = 0;
			for (k = 0; k < len; k++)
				line_put(l, start + k, buf[k], ab[k]);
			line_trim(l);
			ED.d->changed = 1;
			if (r.s[s] == 'C')
				next = ruler_next(&r, start + len - 1, ED.ins);
		}
	}
	if (next > ED.cc) {
		ED.cc = next;
		if (r.s[next] == '#') {
			int fs, fe;

			if (numfield(&r, next, &fs, &fe)) {
				ED.cc = fe;
				calc_run(1);
			}
		}
	} else if (next < 0) {
		/* no more stops: the start of the next line */
		ed_cr(1);
	}
}

/* Moving by words */

void
ed_wordfwd(void)
{
	struct line *l = ed_line(ED.cl);
	int sl = ED.cl, sc = ED.cc;

	while (ED.cc < l->len && l->s[ED.cc] != ' ')
		ED.cc++;
	for (;;) {
		l = ed_line(ED.cl);
		while (ED.cc < l->len && l->s[ED.cc] == ' ')
			ED.cc++;
		if (ED.cc < l->len)
			return;
		if (ED.cl >= ED.d->n - 1)
			break;
		ED.cl++;
		ED.cc = 0;
	}
	ED.cl = sl;
	ED.cc = sc;
	error("EB");
}

void
ed_wordback(void)
{
	struct line *l = ed_line(ED.cl);
	int sl = ED.cl, sc = ED.cc;

	if (ED.cc > l->len)
		ED.cc = l->len;
	for (;;) {
		l = ed_line(ED.cl);
		while (ED.cc > 0 && line_ch(l, ED.cc - 1) == ' ')
			ED.cc--;
		if (ED.cc > 0)
			break;
		if (ED.cl == 0) {
			ED.cl = sl;
			ED.cc = sc;
			error("TB");
			return;
		}
		ED.cl--;
		ED.cc = ed_line(ED.cl)->len;
	}
	while (ED.cc > 0 && line_ch(l, ED.cc - 1) != ' ')
		ED.cc--;
}

static void
change_case(void)
{
	struct line *l = ed_line(ED.cl);

	while (ED.cc < l->len && l->s[ED.cc] == ' ')
		ED.cc++;
	while (ED.cc < l->len && l->s[ED.cc] != ' ' && l->s[ED.cc] != M.paint) {
		int c = l->s[ED.cc];

		if (upcase(c) != c)
			c = upcase(c);
		else
			c = lowcase(c);
		l->s[ED.cc++] = (unsigned char)c;
	}
	ED.d->changed = 1;
}

/*
 * Justification (ESC J, ESC QJ from the cursor's line): the paragraph is
 * made again between the F/J and M of the ruler; under J the lines but
 * the last are filled out with blanks, first after full stops.  A tab
 * stop between the margins sets the indent of the first line; an H in
 * the ruler hyphenates words that do not fit.
 */
struct word {
	int	start, len;
};

static int
isvowel(int c)
{
	return strchr(M.vowels, upcase(c)) != NULL && c;
}

/* where a word may be broken so that at most room characters (and a hyphen) fit */
int
hyph_point(const unsigned char *w, int len, int room)
{
	int k;

	for (k = room - 1; k >= 2; k--) {
		if (len - k < 2)
			continue;
		if (w[k - 1] == '-')
			return k;
		if (!isvowel(w[k]) && isvowel(w[k - 1]) && isvowel(w[k + 1 < len ? k + 1 : k]))
			return k;
		if (!isvowel(w[k - 1]) && !isvowel(w[k]) && k + 1 < len && isvowel(w[k + 1]))
			return k;
	}
	return 0;
}

int
ed_justify(int from_here)
{
	struct ruler r;
	int s, e, i, k, n, lm, rm, ncur = -1, indent, first_indent;
	unsigned char *t, *ta;
	int tlen, tcap;
	struct word *wd;
	int nw, curline = ED.cl, curcol = ED.cc;

	ED.ins = 0;
	if (ED.cl >= ED.d->n || parabreak(ED.cl)) {
		error("NJ");
		return -1;
	}
	ed_ruler(&r);
	if ((r.kind != 'F' && r.kind != 'J') || r.right <= 0) {
		error("NJ");
		return -1;
	}
	s = from_here ? ED.cl : ed_parastart(ED.cl);
	e = ed_paraend(ED.cl);
	lm = r.left;
	rm = r.right;
	{
		struct line *l = &ED.d->l[s];

		for (indent = 0; indent < l->len && l->s[indent] == ' '; indent++)
			;
	}
	first_indent = indent > lm ? indent : lm;
	if (!from_here) {
		/* a tab stop between the margins: the indent of the first line */
		for (k = lm + 1; k < rm && k < r.len; k++)
			if (strchr("TLR", r.s[k]) && r.s[k]) {
				first_indent = k;
				break;
			}
		if (indent > lm && indent < rm)
			first_indent = indent;
	}
	tcap = 1024;
	tlen = 0;
	t = (unsigned char *)xrealloc(NULL, tcap);
	ta = (unsigned char *)xrealloc(NULL, tcap);
	for (i = s; i <= e; i++) {
		struct line *l = &ED.d->l[i];
		int len = l->len, hy = 0;

		while (len > 0 && l->s[len - 1] == ' ')
			len--;
		/* a soft hyphen at the end of a line is closed up */
		if (i < e && len > 1 && l->s[len - 1] == '-' && l->s[len - 2] != ' ') {
			len--;
			hy = 1;
		}
		for (k = (i == s ? 0 : 0); k <= len; k++) {
			int c = k < len ? l->s[k] : ' ', a = k < len ? l->a[k] : 0;

			if (hy && k == len)
				continue;
			if (tlen + 3 >= tcap) {
				tcap *= 2;
				t = (unsigned char *)xrealloc(t, tcap);
				ta = (unsigned char *)xrealloc(ta, tcap);
			}
			if (i == curline && k == curcol)
				ncur = tlen;
			if (c == ' ') {
				if (tlen > 0 && t[tlen - 1] != ' ') {
					t[tlen] = ' ';
					ta[tlen++] = 0;
				}
			} else {
				t[tlen] = (unsigned char)c;
				ta[tlen++] = (unsigned char)a;
			}
		}
	}
	while (tlen > 0 && t[tlen - 1] == ' ')
		tlen--;
	if (ncur < 0 || ncur > tlen)
		ncur = tlen;
	wd = (struct word *)xrealloc(NULL, (tlen + 2) * sizeof(struct word));
	nw = 0;
	for (k = 0; k < tlen;) {
		while (k < tlen && t[k] == ' ')
			k++;
		if (k >= tlen)
			break;
		wd[nw].start = k;
		while (k < tlen && t[k] != ' ')
			k++;
		wd[nw].len = k - wd[nw].start;
		nw++;
	}
	/* the lines out of the paragraph's place; the new ones in */
	for (i = e; i >= s; i--)
		doc_delline(ED.d, i);
	n = s;
	{
		int w = 0, part = 0;	/* part: characters of word w already set */

		while (w < nw) {
			int start = n == s ? first_indent : lm;
			int avail = rm - start, used, lw = 0, gaps, extra, col, g;
			int ws[256], wl[256], whyph = 0;
			unsigned char buf[MAXW + 2], ab[MAXW + 2];

			if (avail < 1)
				avail = 1;
			/* the first word (or what is left of it) */
			ws[0] = wd[w].start + part;
			wl[0] = wd[w].len - part;
			used = wl[0];
			lw = 1;
			if (used > avail && r.hyph >= 0) {
				int hp = hyph_point(t + ws[0], wl[0], avail - 1);

				if (hp > 0) {
					wl[0] = hp;
					used = hp + 1;
					whyph = 1;
					part += hp;
				}
			}
			if (!whyph) {
				part = 0;
				w++;
				while (w < nw && lw < 255 && used + 1 + wd[w].len <= avail) {
					ws[lw] = wd[w].start;
					wl[lw] = wd[w].len;
					used += 1 + wl[lw];
					lw++;
					w++;
				}
				/* hyphenation: part of the next word */
				if (w < nw && r.hyph >= 0 && avail - used - 1 >= 4 && lw < 255) {
					int hp = hyph_point(t + wd[w].start, wd[w].len, avail - used - 1);

					if (hp > 0) {
						ws[lw] = wd[w].start;
						wl[lw] = hp;
						used += 1 + hp + 1;
						lw++;
						part = hp;
						whyph = 1;
					}
				}
			}
			gaps = lw - 1;
			extra = (r.kind == 'J' && (w < nw || part) && gaps > 0) ? avail - used : 0;
			{
				int gapw[256];

				for (g = 0; g < gaps; g++)
					gapw[g] = 1;
				/* two blanks after a full stop */
				if (!M.onespace)
					for (g = 0; g < gaps; g++) {
						int c = t[ws[g] + wl[g] - 1];

						if (c == '.' || c == '!' || c == '?' || c == ':')
							gapw[g] = 2;
				}
				{
					int sum = 0;

					for (g = 0; g < gaps; g++)
						sum += gapw[g] - 1;
					if (r.kind == 'J')
						extra -= sum;
					if (extra < 0) {
						extra = 0;
						if (r.kind != 'J')
							for (g = 0; g < gaps; g++)
								if (start + used + sum > rm)
									gapw[g] = 1;
					}
				}
				/* the rest spread out: alternately from the two ends,
				   so that no river runs down the paragraph */
				{
					int lo = 0, hi = gaps - 1, side = (n & 1), round = 0;

					while (extra > 0 && gaps > 0 && round < 64) {
						g = side ? hi-- : lo++;
						side = !side;
						gapw[g]++;
						extra--;
						if (lo > hi) {
							lo = 0;
							hi = gaps - 1;
							round++;
						}
					}
				}
				memset(buf, ' ', sizeof buf);
				memset(ab, 0, sizeof ab);
				col = start;
				for (k = 0; k < lw; k++) {
					if (col + wl[k] >= MAXW)
						break;
					if (ncur >= ws[k] && ncur <= ws[k] + wl[k]) {
						ED.cl = n;
						ED.cc = col + (ncur - ws[k]);
					}
					memcpy(buf + col, t + ws[k], wl[k]);
					memcpy(ab + col, ta + ws[k], wl[k]);
					col += wl[k];
					if (k < lw - 1)
						col += gapw[k];
				}
				if (whyph && col < MAXW) {
					buf[col] = '-';
					ab[col++] = 0;
				}
			}
			i = doc_insline(ED.d, n, buf, col);
			memcpy(ED.d->l[i].a, ab, col);
			n++;
		}
	}
	if (nw == 0) {
		doc_insline(ED.d, n, (const unsigned char *)"", 0);
		n++;
	}
	if (ncur >= tlen && n > s) {
		ED.cl = n - 1;
		ED.cc = ED.d->l[n - 1].len;
	}
	xfree(t);
	xfree(ta);
	xfree(wd);
	ED.d->changed = 1;
	/* the cursor is left at the end of the paragraph */
	ED.cl = n - 1;
	ED.cc = ED.d->l[n - 1].len;
	return 0;
}

/* Pages */

static int
spacing4(int upto)
{
	int i, sp = 4;

	for (i = 0; i < upto && i < ED.d->n; i++)
		if (iscontrolline(i) && ED.d->l[i].s[1] == 'S' && ED.d->l[i].len > 2) {
			double v = atof((char *)ED.d->l[i].s + 2);

			sp = v > 0 ? (int)(v * 4 + 0.5) : 4;
		}
	return sp;
}

/* print lines (in quarters) from the last page break above line to it */
static int
pagelines(int line)
{
	int i, q = 0, sp;

	for (i = line - 1; i >= 0; i--)
		if (iscontrolline(i) && ED.d->l[i].s[1] == 'P')
			break;
	sp = spacing4(i + 1);
	for (i = i + 1; i < line && i < ED.d->n; i++) {
		struct line *l = &ED.d->l[i];

		if (iscontrolline(i)) {
			if (l->s[1] == 'S' && l->len > 2)
				sp = (int)(atof((char *)l->s + 2) * 4 + 0.5);
			continue;
		}
		if ((l->flags & (LF_RULER | LF_CONT)) || nolf(i))
			continue;
		q += sp > 0 ? sp : 4;
	}
	return q;
}

int
ed_pagecheck(void)
{
	if (pd_lpp > 0 && pagelines(ED.cl) > pd_lpp * 4) {
		error("EP");
		return 1;
	}
	return 0;
}

static void
ppline(char *b, int page)
{
	char t[16];
	int n;

	if (pd_total > 0)
		sprintf(t, "%d", pd_total);
	else
		strcpy(t, "FF");
	sprintf(b, ".PP=%04d", page);
	n = (int)strlen(b);
	while (n < 26)
		b[n++] = '=';
	sprintf(b + n, "L/P=%d/%s", pd_lpp, t);
	n = (int)strlen(b);
	while (n < 50)
		b[n++] = ' ';
	sprintf(b + n, ".S %d.%02d .C %s", spacing4(ED.cl) / 4, (spacing4(ED.cl) % 4) * 25, I.pcc);
	n = (int)strlen(b);
	while (n < width() - 1 && n < 80)
		b[n++] = '=';
	b[n] = 0;
}

/* ESC P: the next soft page break (.PP) where printing would put it */
void
ed_paginate(void)
{
	int i, q = 0, sp, page = P.pagestart, start, brk = -1, lim;
	char b[MAXW + 1];

	for (i = ED.cl - 1; i >= 0; i--)
		if (iscontrolline(i) && ED.d->l[i].s[1] == 'P')
			break;
	start = i;
	for (i = 0; i <= start; i++)
		if (iscontrolline(i) && ED.d->l[i].s[1] == 'P') {
			const unsigned char *s = ED.d->l[i].s;
			int k = s[2] == 'P' ? 3 : 2;

			while (k < ED.d->l[i].len && (s[k] == '=' || s[k] == ' ' || s[k] == '*'))
				k++;
			if (k < ED.d->l[i].len && s[k] >= '0' && s[k] <= '9')
				page = atoi((char *)s + k);
			else
				page++;
		}
	if (start < 0) {
		/* the document must start with a .PP line */
		ppline(b, page);
		doc_insline(ED.d, 0, (unsigned char *)b, (int)strlen(b));
		start = 0;
		ED.cl++;
	}
	page = page + (start >= 0 ? 1 : 0);
	sp = spacing4(start + 1);
	lim = pd_lpp * 4;
	for (i = start + 1; i < ED.d->n; i++) {
		struct line *l = &ED.d->l[i];

		if (iscontrolline(i)) {
			if (l->s[1] == 'P' && l->len >= 3 && l->s[2] == 'P') {
				doc_delline(ED.d, i);
				i--;
				continue;
			}
			if (l->s[1] == 'P') {
				ED.cl = i;
				ED.cc = 0;
				return;
			}
			if (l->s[1] == 'S' && l->len > 2)
				sp = (int)(atof((char *)l->s + 2) * 4 + 0.5);
			continue;
		}
		if ((l->flags & (LF_RULER | LF_CONT)) || nolf(i))
			continue;
		q += sp > 0 ? sp : 4;
		if (q > lim) {
			brk = i;
			break;
		}
	}
	if (brk < 0) {
		ED.cl = ED.d->n;
		ED.cc = 0;
		error("EB");
		return;
	}
	/* widow and orphan factors: a blank line near the break */
	{
		int k, best = -1;

		for (k = brk - 1; k > brk - 1 - pd_widow && k > start; k--)
			if (blankline(k) && !(ED.d->l[k].flags & LF_RULER)) {
				best = k + 1;
				break;
			}
		if (best < 0)
			for (k = brk; k < brk + pd_orphan && k < ED.d->n; k++)
				if (blankline(k) && !(ED.d->l[k].flags & LF_RULER)) {
					best = k + 1;
					break;
				}
		if (best > 0)
			brk = best;
	}
	ppline(b, page);
	doc_insline(ED.d, brk, (unsigned char *)b, (int)strlen(b));
	ED.cl = brk;
	ED.cc = 0;
	ED.d->changed = 1;
}

/* The document details: rulers, keystores and print details of a type */
void
ed_load_details(const char *ext)
{
	char key[12], *t;
	long len;
	int i, k;

	sprintf(key, "*%.8s", ext[0] == '.' ? ext : ".");
	if (ext[0] != '.')
		sprintf(key, "*.%.7s", ext);
	strncpy(ED.detext, key + 1, 7);
	ED.detext[7] = 0;
	if (rec_text(key, &t, &len, '_') < 0) {
		sprintf(key, "*%.8s", I.defext);
		if (rec_text(key, &t, &len, '_') < 0) {
			for (i = 1; i <= 5; i++)
				ED.rulers[i][0] = 0;
			for (i = 1; i < NKS; i++)
				keystore[i][0] = 0;
			return;
		}
	}
	for (i = 0; i < 5; i++) {
		char *r = ED.rulers[1 + i];
		int n = 80;

		for (k = 0; k < n; k++)
			r[k] = 80 * i + k < len && t[80 * i + k] != '_' && t[80 * i + k] != '\n' && t[80 * i + k] != ' ' ? t[80 * i + k] : '.';
		r[n] = 0;
		/* wider screens: the ruler goes on with dots */
		if (width() > 80) {
			memset(r + 80, '.', width() - 80);
			r[width()] = 0;
		}
		{
			int any = 0;

			for (k = 0; r[k]; k++)
				if (r[k] != '.')
					any = 1;
			if (!any)
				r[0] = 0;
		}
	}
	for (i = 0; i < 9; i++) {
		int off = i < 3 ? 400 + 78 * i : 634 + 38 * (i - 3), n = i < 3 ? 78 : 38;
		char *ks = keystore[1 + i];

		for (k = 0; k < n && off + k < len; k++)
			ks[k] = t[off + k];
		ks[k] = 0;
		while (k > 0 && (ks[k - 1] == '_' || ks[k - 1] == ' '))
			ks[--k] = 0;
	}
	{
		char b[4];

		pd_foot = 862 < len ? t[862] : 'A';
#define NUMF(off, n, dflt, var) do { int j_; for (j_ = 0; j_ < n; j_++) b[j_] = off + j_ < len ? t[off + j_] : '_'; b[n] = 0; \
	var = (b[0] >= '0' && b[0] <= '9') || (b[1] >= '0' && b[1] <= '9') ? atoi(b[0] == '_' ? b + 1 : b) : dflt; } while (0)
		NUMF(863, 2, 0, pd_lm);
		NUMF(865, 2, pd_lm, pd_alt);
		NUMF(867, 1, 3, pd_widow);
		NUMF(868, 1, 3, pd_orphan);
		NUMF(869, 2, 60, pd_lpp);
		b[0] = 871 < len ? t[871] : 'F';
		b[1] = 872 < len ? t[872] : 'F';
		b[2] = 0;
		pd_total = b[0] == 'F' ? 0 : atoi(b);
	}
	xfree(t);
}

void
ed_clearscreen(void)
{
	int i;

	for (i = ED.top; i < ED.top + ED.rows && i < ED.d->n; i++)
		ED.d->l[i].len = 0;
}

/* the start of the screen, or one screen back */
static void
backscreen(void)
{
	if (ED.protect) {
		firstunprot();
		return;
	}
	if (ED.cl != ED.top || ED.cc != 0) {
		ED.cl = ED.top;
		ED.cc = 0;
		return;
	}
	if (ED.top == 0) {
		error("TB");
		return;
	}
	ED.top -= ED.rows - 1;
	if (ED.top < 0)
		ED.top = 0;
	ED.cl = ED.top;
	ED.cc = 0;
}

static int
lastline(void)
{
	int n = ED.d->n;

	while (n > 0 && ED.d->l[n - 1].len == 0)
		n--;
	return n;
}

static void
fwdscreen(void)
{
	int row = ED.cl - ED.top;

	if (ED.protect) {
		int l = ED.top + ED.rows - 1, c = cols() - 1;

		if (l >= ED.d->n)
			l = ED.d->n - 1;

		if (ed_nextunprot(&l, &c, -1)) {
			ED.cl = l;
			ED.cc = c;
		}
		return;
	}
	if (ED.cl >= lastline()) {
		error("EB");
		return;
	}
	ED.top += ED.rows;
	ED.cl = ED.top + row;
}

/* the end of the line (Ctrl-G) */
static void
endline(void)
{
	struct line *l = ed_line(ED.cl);
	int e = l->len;

	while (e > 0 && l->s[e - 1] == ' ')
		e--;
	if (e == 0)
		ED.cc = 0;
	else if (ED.cc == e) {
		ED.cl++;
		l = ed_line(ED.cl);
		for (e = l->len; e > 0 && l->s[e - 1] == ' '; e--)
			;
		ED.cc = e;
	} else
		ED.cc = e;
	if (ED.cc >= cols())
		ED.cc = cols() - 1;
}

/* Delete and insert characters */

static void
delchar(int obey)
{
	struct line *l = ed_line(ED.cl);
	struct ruler r;

	if (ED.cc >= l->len)
		return;
	undel_char(l->s[ED.cc]);
	if (ED.protect) {
		/* the field closes up, the paint character comes in at its end */
		int e = ED.cc;

		while (e + 1 < cols() && ed_unprot(ED.cl, e + 1))
			e++;
		memmove(l->s + ED.cc, l->s + ED.cc + 1, e - ED.cc);
		line_put(l, e, M.paint, l->a[e]);
	} else if (obey) {
		ed_ruler(&r);
		if (r.right > ED.cc && (r.kind == 'F' || r.kind == 'J') && l->len > r.right) {
			/* only the text within the margins moves */
			int e = r.right;

			memmove(l->s + ED.cc, l->s + ED.cc + 1, e - ED.cc);
			memmove(l->a + ED.cc, l->a + ED.cc + 1, e - ED.cc);
			l->s[e] = ' ';
			l->a[e] = 0;
		} else
			line_del(l, ED.cc, 1);
	} else
		line_del(l, ED.cc, 1);
	line_trim(l);
	ED.d->changed = 1;
}

static void
inschar(int obey)
{
	struct line *l = ed_line(ED.cl);

	if (ED.protect) {
		int e = ED.cc;

		while (e + 1 < cols() && ed_unprot(ED.cl, e + 1))
			e++;
		memmove(l->s + ED.cc + 1, l->s + ED.cc, e - ED.cc);
		line_pad(l, e + 1);
		l->s[ED.cc] = ' ';
		ED.d->changed = 1;
		return;
	}
	line_ins(l, ED.cc, (const unsigned char *)" ", 1, 0);
	ED.d->changed = 1;
	if (obey)
		ed_wrap(ED.cl);
	else if (l->len > cols() - 1) {
		/* past the edge: a continuation row */
		int n = l->len - (cols() - 1), nl = ED.cl + 1;

		if (!(l->flags & LF_CONT)) {
			doc_insline(ED.d, nl, (const unsigned char *)"", 0);
			l = ed_line(ED.cl);
			l->flags |= LF_CONT;
		}
		line_ins(ed_line(nl), 0, l->s + cols() - 1, n, 0);
		l->len = cols() - 1;
	}
}

static void
delword(void)
{
	struct line *l = ed_line(ED.cl);
	int b = ED.cc, e;

	if (ED.protect) {
		error("PM");
		return;
	}
	if (b >= l->len || l->s[b] == ' ')
		return;
	while (b > 0 && l->s[b - 1] != ' ')
		b--;
	for (e = b; e < l->len && l->s[e] != ' '; e++)
		;
	while (e < l->len && l->s[e] == ' ')
		e++;
	undel_word(l->s + b, e - b);
	line_del(l, b, e - b);
	ED.cc = b;
	ed_wrap(ED.cl);
	ED.d->changed = 1;
}

static void
delline(void)
{
	if (ED.protect) {
		error("PM");
		return;
	}
	undel_line(ed_line(ED.cl));
	doc_delline(ED.d, ED.cl);
	ed_line(ED.cl);
}

static void
deltoend(void)
{
	struct line *l = ed_line(ED.cl);
	struct line t;

	if (ED.protect) {
		int e = ED.cc;

		while (e < cols() && ed_unprot(ED.cl, e)) {
			line_put(l, e, M.paint, l->a[e]);
			e++;
		}
		return;
	}
	if (ED.cc >= l->len)
		return;
	memset(&t, 0, sizeof t);
	line_pad(&t, 0);
	{
		unsigned char sp[MAXW + 1];

		memset(sp, ' ', ED.cc);
		line_set(&t, sp, ED.cc);
	}
	line_ins(&t, ED.cc, l->s + ED.cc, l->len - ED.cc, 0);
	memcpy(t.a + ED.cc, l->a + ED.cc, l->len - ED.cc);
	undel_line(&t);
	line_free(&t);
	l->len = ED.cc;
	ED.d->changed = 1;
}

/* A control function (Appendix B); returns 1 if the session ends. */
int
ed_ctl(int k)
{
	struct line *l;
	int w = cols();

	switch (k) {
	case CTL('A'):
		if (ED.protect) {
			error("PM");
			break;
		}
		doc_insline(ED.d, ED.cl, (const unsigned char *)"", 0);
		break;
	case CTL('B'):
		ed_wordback();
		break;
	case CTL('C'):
		in_abort();
		break;
	case CTL('D'):
		delchar(0);
		break;
	case CTL('E'):
		inschar(0);
		break;
	case CTL('F'):
		ed_wordfwd();
		break;
	case CTL('G'):
		endline();
		break;
	case CTL('H'):
		if (ED.cc > 0)
			ED.cc--;
		else if (ED.cl > 0) {
			ED.cl--;
			ED.cc = w - 1;
		}
		if (ED.protect && !ed_unprot(ED.cl, ED.cc)) {
			int ll = ED.cl, c = ED.cc;

			if (ed_nextunprot(&ll, &c, -1)) {
				ED.cl = ll;
				ED.cc = c;
			}
		}
		break;
	case TAB:
		ed_tab(0);
		break;
	case CTL('J'):
		ED.cl++;
		if (ED.pagewarn)
			ed_pagecheck();
		break;
	case CTL('K'):
		change_case();
		break;
	case CTL('L'):
		fwdscreen();
		break;
	case CR:
		ed_cr(0);
		break;
	case CTL('N'):
		inschar(1);
		break;
	case CTL('O'):
		ED.exitcode = EX_EXIT;
		return 1;
	case CTL('P'):
		delchar(1);
		break;
	case CTL('Q'):
		ED.cl = ED.cc = 0;
		break;
	case CTL('R'):
		if (ED.cc < w - 1)
			ED.cc++;
		else {
			ED.cl++;
			ED.cc = 0;
		}
		if (ED.protect && !ed_unprot(ED.cl, ED.cc)) {
			int ll = ED.cl, c = ED.cc;

			if (ed_nextunprot(&ll, &c, 1)) {
				ED.cl = ll;
				ED.cc = c;
			}
		}
		break;
	case CTL('S'):
		ED.cl = lastline();
		ED.cc = 0;
		ED.top = ED.cl - ED.rows + 1;
		break;
	case CTL('T'):
		backscreen();
		break;
	case CTL('U'):
		if (ED.cl > 0)
			ED.cl--;
		else if (ED.top == 0 && ED.cl == 0)
			error("TB");
		/* in protect mode too it keeps its column (B.21): what is
		   typed goes on to the next field */
		break;
	case CTL('V'):
		deltoend();
		break;
	case CTL('W'):
		delword();
		break;
	case CTL('X'):
		delline();
		break;
	case CTL('Y'):
		if (ED.cl > 0) {
			l = ed_line(ED.cl - 1);
			ed_typechar(line_ch(l, ED.cc), 1);
		}
		break;
	case CTL('Z'):
		ED.exitcode = EX_ABANDON;
		return 1;
	case DEL:
		if (ED.cc == 0)
			break;
		if (ED.protect && !ed_unprot(ED.cl, ED.cc - 1))
			break;
		ED.cc--;
		l = ed_line(ED.cl);
		if (ED.ins && !ED.protect)
			line_del(l, ED.cc, 1);
		else if (ED.cc < l->len)
			line_put(l, ED.cc, ED.protect ? M.paint : ' ', l->a[ED.cc] & (AT_UNPROT | AT_BG));
		line_trim(l);
		ED.d->changed = 1;
		break;
	default:
		break;
	}
	if (ED.cl < 0)
		ED.cl = 0;
	return 0;
}

/* does s stand on the screen from the cursor on (ESC X W/U) */
int
ed_matches(const char *s)
{
	struct line *l = ed_line(ED.cl);
	int i, n = (int)strlen(s);

	for (i = 0; i < n; i++)
		if (line_ch(l, ED.cc + i) != (unsigned char)s[i])
			return 0;
	return 1;
}

/* the screen of the session (and of the messages over it) */
static void
draw_hook(void)
{
	ed_draw();
}

void
ed_fix(void)
{
	if (ED.cl < 0)
		ED.cl = 0;
	if (ED.cc < 0)
		ED.cc = 0;
	if (ED.cc >= cols())
		ED.cc = cols() - 1;
	ed_line(ED.cl);
}

static int
save(long *lines, long *words, long *chars)
{
	char bak[300], *dot;

	if (ED.mode == ED_PLAY || ED.mode == ED_VIEW)
		return 0;
	/* the old version is kept under the backup extension (not an
	   empty file: a document just created) */
	if (fexists(ED.path) && ED.mode != ED_NEW) {
		strcpy(bak, ED.path);
		dot = strrchr(bak, '.');
		if (dot == NULL || strchr(dot, '/'))
			dot = bak + strlen(bak);
		sprintf(dot, "%s", I.bakext[0] ? I.bakext : ".bak");
		{
			char *p;

			for (p = dot; *p; p++)
				*p = (char)lowcase((unsigned char)*p);
		}
		remove(bak);
		rename(ED.path, bak);
	}
	return doc_save(ED.d, ED.path, lines, words, chars);
}

static int
load(void)
{
	doc_clear(ED.d);
	if (doc_load(ED.d, ED.path, cols()) < 0)
		return -1;
	ED.d->changed = 0;
	return 0;
}

/*
 * An edit session: the playground, or a document.  rec: a record to
 * perform at the start (from a menu), after a line put in at the top.
 * Returns the exit code (EX_...); the counts of a saved document are
 * left in ED_lines etc.
 */
long	ED_lines, ED_words, ED_chars;

int
edit_run(int mode, const char *path, const char *name, const char *rec)
{
	int k, savedrow = 0;
	void (*olddraw)(void) = ui_drawfn;

	memset(&ED.posl, -1, sizeof ED.posl);
	ED.mode = mode;
	ED.ins = ED.vert = ED.protect = ED.bgmode = ED.ignrul = ED.attr = 0;
	ED.useover = 0;
	ED.markl = -1;
	ED.sqch = '_';
	ED.exitcode = EX_NONE;
	ED.statusline = I.statline;
	ED.pagewarn = I.pagewarn;
	ED.calc = 0;
	ED.started = (long)time(NULL);
	ED.cl = ED.cc = ED.top = 0;
	ED.rows = t_rows - (ED.statusline ? 1 : 0);
	ED_lines = ED_words = ED_chars = 0;
	msgon = 0;
	shush = 0;
	strncpy(ED.path, path ? path : "", sizeof ED.path - 1);
	strncpy(ED.name, name ? name : "", MAXNAME);
	if (ED.d == NULL)
		ED.d = doc_new();
	doc_clear(ED.d);
	if (mode == ED_PLAY) {
		char ext[8];

		sprintf(ext, ".%03d", cols() % 1000);
		if (cols() == 80)
			strcpy(ext, ".080");
		ed_load_details(ED.ext[0] ? ED.ext : ext);
	} else {
		ed_load_details(ED.ext);
		if (mode != ED_NEW && load() < 0) {
			ui_drawfn = olddraw;
			return -1;
		}
		ed_markrulers();
		if (mode == ED_NEW && ED.d->n == 0) {
			struct ruler r;

			defruler(&r);
			doc_insline(ED.d, 0, (unsigned char *)r.s, r.len);
			ED.d->l[0].flags = LF_RULER;
			doc_insline(ED.d, 1, (const unsigned char *)"", 0);
			ED.cl = 1;
		}
	}
	ED.d->changed = 0;
	ui_drawfn = draw_hook;
	vdu_seq4(1);
	if (!shutup)
		t_clear();
	if (rec && rec[0]) {
		if (mode != ED_PLAY) {
			doc_insline(ED.d, 0, (const unsigned char *)"", 0);
			ED.cl = 0;
			ED.cc = 0;
		}
		perform(rec, 0);
	}
	for (;;) {
		ed_fix();
		k = in_key();
		if (getenv("LEXTRACE")) {
			FILE *tf = fopen(getenv("LEXTRACE"), "a");

			if (tf) {
				fprintf(tf, "key %03x at %d,%d prot=%d\n", k, ED.cl, ED.cc, ED.protect);
				fclose(tf);
			}
		}
		if (k == K_NONE) {
			ED.exitcode = EX_EXIT;
			break;
		}
		if (msgon) {
			/* a message stays until Return (or ESC E, ESC Z, which
			   only take it away); other keys typed are lost, as
			   in LEX-68.  A program goes on. */
			if (in_prognow()) {
				msgon = 0;
			} else if (k == CR || k == LF) {
				msgon = 0;
				ED.dirty = 1;
				continue;
			} else if (k == ESC) {
				int e = upcase(KC(in_key()));

				if (e == 'E' || e == 'Z') {
					msgon = 0;
					ED.dirty = 1;
				}
				continue;
			} else {
				bell();
				continue;
			}
		}
		if (k & K_LIT) {
			ed_typechar(KC(k), 1);
			continue;
		}
		if (k == ESC) {
			if (ed_esc())
				goto ended;
			continue;
		}
		if (k < 32 || k == DEL) {
			if (k == CTL('C')) {
				in_abort();
				continue;
			}
			if (ed_ctl(k))
				goto ended;
			continue;
		}
		ed_typechar(k, 0);
		continue;
	ended:
		if (ED.exitcode == EX_REEDIT || ED.exitcode == EX_REEDITPOS) {
			if (ED.mode == ED_PLAY) {
				ED.exitcode = EX_NONE;
				error("EV");
				continue;
			}
			savedrow = ED.cl;
			if (ED.mode != ED_VIEW)
				save(&ED_lines, &ED_words, &ED_chars);
			load();
			ed_markrulers();
			ED.cl = ED.cc = ED.top = 0;
			if (ED.exitcode == EX_REEDITPOS)
				ED.cl = savedrow;
			ED.exitcode = EX_NONE;
			continue;
		}
		break;
	}
	if (ED.exitcode == EX_EXIT && mode != ED_PLAY && mode != ED_VIEW) {
		if (save(&ED_lines, &ED_words, &ED_chars) < 0)
			error("NR");
	} else if (mode != ED_PLAY)
		doc_count(ED.d, &ED_lines, &ED_words, &ED_chars);
	esc_closefiles();
	vdu_seq4(3);
	ED.protect = 0;
	shush = 0;
	ED.statusline = 0;
	ui_drawfn = olddraw;
	return ED.exitcode;
}
