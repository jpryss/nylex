/*
 * esc2.c - the larger escape sequences: records (ESC ] x), conditionals
 * (ESC CT x), repeats (ESC X), search (ESC F, G, -G), merging (ESC M x)
 * and writing out (ESC W x), opening a document from the playground
 * (ESC O x) and sorting (ESC B x).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

/* the parameters a program gives a command: keys until stop */
static int
collect(char *buf, int max, int stopspace)
{
	int k, n = 0;
	char v[4];

	for (;;) {
		k = in_key();
		if (k == K_NONE)
			break;
		if (k == CR)
			return n;
		if (stopspace && (KC(k) == ' ' || KC(k) == M.paint))
			return n;
		if (in_prognow())
			vis_of(k, v);
		else {
			v[0] = (char)KC(k);
			v[1] = 0;
		}
		if (n + (int)strlen(v) < max) {
			strcpy(buf + n, v);
			n += (int)strlen(v);
		}
		buf[n] = 0;
	}
	buf[n] = 0;
	return n;
}

/* a string for a command: from a program, or typed after a prompt */
static int
getstring(const char *prompt, char *buf, int max, int *term, int vis)
{
	if (in_depth() > 0) {
		int k, n = 0;
		char v[4];

		*term = CR;
		for (;;) {
			k = in_key();
			if (k == K_NONE)
				return -1;
			if (k == CR || k == LF) {
				*term = k;
				break;
			}
			if (k == ESC && !in_prognow()) {
				in_key();
				return -1;
			}
			if (vis && in_prognow())
				vis_of(k, v);
			else {
				v[0] = (char)KC(k);
				v[1] = 0;
			}
			if (n + (int)strlen(v) < max) {
				strcpy(buf + n, v);
				n += (int)strlen(v);
			}
		}
		buf[n] = 0;
		return n;
	}
	return read_string(prompt, buf, max, term);
}

/* Performing a record: [KEY] or ESC RK */
void
perform(const char *key, int typed)
{
	char k[40], *t, *plus;
	long len;
	int from = 0, cnt = -1;

	strncpy(k, key, 39);
	k[39] = 0;
	plus = strchr(k, '+');
	if (plus) {
		*plus = 0;
		from = atoi(plus + 1);
		if (strchr(plus + 1, '/'))
			cnt = atoi(strchr(plus + 1, '/') + 1);
	}
	/* the key is "referenced" even when there is no such record: ESC N
	   goes on from it (LEX-68's *DTIR starts so from ":") */
	rec_lastkey(k);
	if (plus) {
		/* a part of a record: text only */
		if (rec_text(k, &t, &len, ED.sqch) < 0) {
			error("NZ");
			return;
		}
		if (from < 1)
			from = 1;
		if (from - 1 < len) {
			long n = cnt < 0 ? len - (from - 1) : cnt;

			if (from - 1 + n > len)
				n = len - (from - 1);
			{
				/* trailing blanks of the field are not typed */
				while (n > 0 && (t[from - 1 + n - 1] == ' '))
					n--;
			}
			in_lits(t + from - 1, n);
		}
		xfree(t);
		rec_lastkey(k);
		return;
	}
	if (rec_text(k, &t, &len, '_') < 0) {
		if (ED.calc)
			error("CB");
		else
			error("NZ");
		return;
	}
	rec_lastkey(k);
	in_prog(t, len, 1);
	xfree(t);
	(void)typed;
}

/* ESC ] x and ESC > x */
int
esc_record(int k, int excl)
{
	int line, col;

	if (!strchr("ACKLPRSU", k) || !k) {
		error("BB");
		return -1;
	}
	if (key_find(&line, &col) < 0) {
		error("NX");
		return -1;
	}
	switch (k) {
	case 'R':
		return rec_read(line, col, 0);
	case 'L':
		return rec_read(line, col, 1);
	default:
		return rec_write(line, col, k, excl);
	}
}

/* ESC CT x string1 prog1 Return prog2 Return */
int
esc_cond(void)
{
	int x = upcase(KC(in_key())), yes = 0;
	char s1[KSLEN + 1], p1[KSLEN * 2 + 1], p2[KSLEN * 2 + 1];
	int oldshush = shush;

	s1[0] = 0;
	shush = 1;
	if (x == 'S' || x == 'K' || x == 'D' || x == 'F')
		collect(s1, KSLEN, 1);
	else if (x == '+' || x == '-' || x == '0' || x == 'O') {
		int k = in_key();

		s1[0] = (char)upcase(KC(k));
		s1[1] = 0;
	}
	collect(p1, (int)sizeof p1, 0);
	collect(p2, (int)sizeof p2, 0);
	shush = oldshush;
	switch (x) {
	case 'S':
		yes = ed_matches(s1);
		break;
	case 'K':
		yes = rec_get(s1, NULL) != NULL;
		break;
	case 'F':
		yes = foreign_has(s1);
		break;
	case 'D':
		{
			char path[300], nm[MAXNAME + 1];

			strncpy(nm, s1, MAXNAME);
			nm[MAXNAME] = 0;
			docname_file(path, nm);
			yes = fexists(path) || fexists(s1);
			if (!yes) {
				docname_norm(nm, I.defext);
				docname_file(path, nm);
				yes = fexists(path);
			}
		}
		break;
	case 'N':
		{
			struct line *l = ed_line(ED.cl);
			int c = ED.cc, any = 0;

			yes = 1;
			while (c < l->len && l->s[c] != ' ' && l->s[c] != M.paint) {
				if ((l->s[c] < '0' || l->s[c] > '9') && l->s[c] != I.decpt && l->s[c] != '-')
					yes = 0;
				any = 1;
				c++;
			}
			if (!any)
				yes = 0;
		}
		break;
	case 'Q':
		{
			int row = ED.cl - ED.top, r, os = shush;

			shush = 0;
			ed_draw();
			t_clearrow(row);
			r = ui_yesno(row, 0, text(0, 30));
			shush = os;
			ED.dirty = 1;
			if (r < 0) {
				in_abort();
				return 0;
			}
			yes = r;
		}
		break;
	case '+':
	case '-':
	case '0':
	case 'O':
		{
			double v = calc_get(s1[0]);

			yes = x == '+' ? v > 0 : x == '-' ? v < 0 : v == 0;
		}
		break;
	default:
		error("AC");
		return -1;
	}
	if (yes)
		in_prog(p1, (long)strlen(p1), 0);
	else
		in_prog(p2, (long)strlen(p2), 0);
	return 0;
}

/* ESC X: nn string, 0 string, W string1\string2, U string1\string2 */
int
esc_repeat(void)
{
	char b[KSLEN * 3 + 1];
	int term, n;
	long count = 0;
	char *p;

	strcpy(b, funcstr[1]);
	n = getstring(text(0, 38), b, (int)sizeof b, &term, 1);
	if (n < 0)
		return -1;
	if (n == 0 && in_depth() == 0)
		strcpy(b, funcstr[1]);
	scopy(funcstr[1], b, KSLEN);
	funcstr[1][KSLEN] = 0;
	p = b;
	if (upcase((unsigned char)*p) == 'W' || upcase((unsigned char)*p) == 'U') {
		int cond = upcase((unsigned char)*p);
		char *sep = strchr(p + 1, M.ret);

		if (sep == NULL || sep[1] == 0) {
			error("XS");
			return -1;
		}
		*sep = 0;
		in_repeat(sep + 1, (long)strlen(sep + 1), 0, cond, p + 1);
		return 0;
	}
	while (*p >= '0' && *p <= '9')
		count = count * 10 + *p++ - '0';
	if (*p == 0 || p == b) {
		error("XR");
		return -1;
	}
	in_repeat(p, (long)strlen(p), count, 0, NULL);
	return 0;
}

/* Search */

static int
eqc(int a, int b, int exact)
{
	if (exact)
		return a == b;
	return upcase(a) == upcase(b);
}

/* does pat stand at col of line l; the end column, or -1 */
static int
match_at(const struct line *l, int col, const char *pat, int exact)
{
	int i = col, k;

	if (!exact && pat[0] && line_ch(l, col) == ' ')
		return -1;
	for (k = 0; pat[k]; k++) {
		if (!exact) {
			if (pat[k] == ' ')
				continue;
			while (i < l->len && l->s[i] == ' ')
				i++;
		}
		if (i >= l->len || !eqc(l->s[i], (unsigned char)pat[k], exact))
			return -1;
		i++;
	}
	return i;
}

/* the first place at or after (*line, *col) (before it: back) where the
   search string is; 0 if none */
static int
find(int *line, int *col, const char *str, int exact, int back)
{
	char s1[KSLEN + 1], s2[KSLEN + 1];
	int how = 0, i, c, colwant = -1;
	const char *p;

	strcpy(s1, str);
	s2[0] = 0;
	if (M.g_or && (p = strchr(str, M.g_or)) != NULL && p != str) {
		how = '/';
		s1[p - str] = 0;
		strcpy(s2, p + 1);
	} else if (M.g_and && (p = strchr(str, M.g_and)) != NULL && p != str) {
		how = '&';
		s1[p - str] = 0;
		strcpy(s2, p + 1);
	} else if (M.g_col && (p = strchr(str, M.g_col)) != NULL && p != str) {
		how = '\\';
		s1[p - str] = 0;
		colwant = atoi(p + 1) > 0 ? atoi(p + 1) - 1 : 0;
	}
	i = *line;
	c = *col;
	while (i >= 0 && i < ED.d->n) {
		struct line *l = &ED.d->l[i];

		if (!back) {
			for (; c < l->len; c++) {
				int hit = match_at(l, c, s1, exact) >= 0;

				if (!hit && how == '/')
					hit = match_at(l, c, s2, exact) >= 0;
				if (hit && how == '\\' && c != colwant)
					hit = 0;
				if (hit && how == '&') {
					int j, k, ok = 0;

					for (j = i; j < ED.d->n && j < i + ED.rows && !ok; j++)
						for (k = 0; k < ED.d->l[j].len; k++)
							if (match_at(&ED.d->l[j], k, s2, exact) >= 0) {
								ok = 1;
								break;
							}
					hit = ok;
				}
				if (hit) {
					*line = i;
					*col = c;
					return 1;
				}
			}
			i++;
			c = 0;
		} else {
			if (c > l->len)
				c = l->len;
			for (c = c - 1; c >= 0; c--) {
				int hit = match_at(l, c, s1, exact) >= 0;

				if (!hit && how == '/')
					hit = match_at(l, c, s2, exact) >= 0;
				if (hit && how == '\\' && c != colwant)
					hit = 0;
				if (hit) {
					*line = i;
					*col = c;
					return 1;
				}
			}
			i--;
			c = i >= 0 ? ED.d->l[i].len : 0;
		}
	}
	return 0;
}

int
esc_search(int kind, int back)
{
	char b[KSLEN + 1];
	int term = CR, n, line, col, k;
	const char *pr = back ? text(0, 51) : kind == 'F' ? text(0, 49) : text(0, 50);

	strcpy(b, funcstr[0]);
	n = getstring(pr, b, KSLEN, &term, 0);
	if (n < 0)
		return -1;
	strcpy(funcstr[0], b);
	if (b[0] == 0) {
		if (term == CR) {
			strcpy(b, " ");
		} else
			strcpy(b, "_");
	}
	/* a number and Line feed: so many lines on */
	if (term == LF && !back) {
		for (k = 0; b[k] >= '0' && b[k] <= '9'; k++)
			;
		if (k > 0 && b[k] == 0) {
			ED.cl += atoi(b);
			ED.cc = 0;
			return 0;
		}
	}
	line = ED.cl;
	col = back ? ED.cc : ED.cc + 1;
	if (find(&line, &col, b, term == LF, back)) {
		ED.cl = line;
		ED.cc = col;
		if (back) {
			if (line < ED.top) {
				ED.top = line - ED.rows / 3;
				if (ED.top < 0)
					ED.top = 0;
			}
		} else if (kind == 'F')
			ED.top = line;
		else if (line >= ED.top + ED.rows)
			ED.top = line - ED.rows + 1;
		return 0;
	}
	if (back) {
		ED.cl = ED.cc = ED.top = 0;
		error("TB");
	} else {
		ED.cl = ED.d->n;
		while (ED.cl > 0 && ED.d->l[ED.cl - 1].len == 0)
			ED.cl--;
		ED.cc = 0;
		error("EB");
	}
	return -1;
}

/* Merging (ESC M x) */

static FILE	*mf;
static char	mname[MAXNAME + 1];
static unsigned char mbuf[MAXW * 4], mattr[MAXW * 4];
static int	mlen = -1;	/* the merge buffer: -1 empty */
static FILE	*wf;
static char	wname[MAXNAME + 1];

const char *
esc_mergename(void)
{
	return mf ? mname : "";
}

static int
mread(void)
{
	int c, n = 0, term = M.attrterm ? M.attrterm : 255, k, m;

	if (mlen >= 0)
		return mlen;
	if (mf == NULL)
		return -1;
	while ((c = getc(mf)) != EOF && c != '\n')
		if (c != '\r' && n < (int)sizeof mbuf - 1)
			mbuf[n++] = (unsigned char)c;
	if (c == EOF && n == 0)
		return -1;
	memset(mattr, 0, sizeof mattr);
	if (n > 0 && mbuf[n - 1] == term) {
		n--;
		m = 0;
		while ((c = getc(mf)) != EOF && c != '\n')
			if (c != '\r' && m < (int)sizeof mattr - 1) {
				int a = c == '_' ? 1 : (c >= 'A' && c <= 'O') ? (c - 'A' + 1) << 1 :
				    (c >= 'a' && c <= 'o') ? ((c - 'a' + 1) << 1) | 1 : 0;

				mattr[m++] = (unsigned char)a;
			}
	}
	for (k = 0; k < n; k++)
		if (mbuf[k] == '\t')
			mbuf[k] = ' ';
	mlen = n;
	return n;
}

/* a line of text put in above the cursor's line, as rows of the screen */
static void
insert_text(const unsigned char *s, const unsigned char *a, int len)
{
	int w = t_cols, at = 0;

	while (len - at > w - 1 && len >= w) {
		int i = doc_insline(ED.d, ED.cl, s + at, w - 1);

		if (a)
			memcpy(ED.d->l[i].a, a + at, w - 1);
		ED.d->l[i].flags = LF_CONT;
		ED.cl++;
		at += w - 1;
	}
	{
		int i = doc_insline(ED.d, ED.cl, s + at, len - at);

		if (a)
			memcpy(ED.d->l[i].a, a + at, len - at);
		if (isruler(ED.d->l[i].s, ED.d->l[i].len))
			ED.d->l[i].flags |= LF_RULER;
		ED.cl++;
	}
}

static void
mmerge1(void)
{
	insert_text(mbuf, mattr, mlen);
	mlen = -1;
}

static int
contains(const unsigned char *s, int n, const char *pat, int exact)
{
	struct line l;
	int c;

	l.s = (unsigned char *)s;
	l.a = NULL;
	l.len = n;
	l.cap = n;
	l.flags = 0;
	for (c = 0; c < n; c++)
		if (match_at(&l, c, pat, exact) >= 0)
			return 1;
	return 0;
}

static void
mclose(void)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	mlen = -1;
}

static int
mopen(const char *name)
{
	char path[300], nm[MAXNAME + 1];

	mclose();
	strncpy(nm, name, MAXNAME);
	nm[MAXNAME] = 0;
	docname_file(path, nm);
	if ((mf = fopen(path, "rb")) == NULL && (mf = fopen(nm, "rb")) == NULL) {
		error("MF");
		in_abort();
		return -1;
	}
	strcpy(mname, nm);
	return 0;
}

int
esc_merge(int c)
{
	char b[KSLEN + 1];
	int term, n, k;

	switch (c) {
	case 'O':
	case 'M':
		b[0] = 0;
		if (getstring(text(0, 1), b, MAXNAME, &term, 0) <= 0)
			return -1;
		if (mopen(b) < 0)
			return -1;
		if (c == 'M') {
			while (mread() >= 0)
				mmerge1();
			mclose();
		}
		ED.d->changed = 1;
		return 0;
	case 'C':
		mclose();
		return 0;
	}
	if (!strchr("AEIJSUV", c) || !c) {
		error("MC");
		return -1;
	}
	if (mf == NULL) {
		error("MC");
		return -1;
	}
	switch (c) {
	case 'I':
		if (mread() < 0) {
			error("ME");
			return -1;
		}
		mmerge1();
		break;
	case 'E':
		while (mread() >= 0)
			mmerge1();
		error("ME");
		break;
	case 'S':
		if (mread() >= 0)
			mlen = -1;
		break;
	case 'V':
		if (mread() < 0) {
			error("ME");
			return -1;
		}
		{
			char s[MAXW + 1];
			int row = ED.cl - ED.top, kk;

			n = mlen < t_cols ? mlen : t_cols;
			memcpy(s, mbuf, n);
			s[n] = 0;
			for (;;) {
				ed_draw();
				t_clearrow(row);
				t_puts(row, 0, s, vdu_bgattr);
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
		break;
	case 'A':
	case 'J':
	case 'U':
		strcpy(b, funcstr[2]);
		{
			char pr[80];

			sprintf(pr, "%s%s", text(0, 43), text(0, c == 'A' ? 45 : c == 'J' ? 47 : 46));
			n = getstring(pr, b, KSLEN, &term, 0);
		}
		if (n < 0)
			return -1;
		strcpy(funcstr[2], b);
		for (k = 0; b[k] >= '0' && b[k] <= '9'; k++)
			;
		if (term == LF && k > 0 && b[k] == 0 && c != 'J') {
			long cnt = atol(b);

			while (cnt-- > 0) {
				if (mread() < 0) {
					error("ME");
					return -1;
				}
				if (c == 'A')
					mlen = -1;
				else
					mmerge1();
			}
			break;
		}
		for (;;) {
			if (mread() < 0) {
				error("ME");
				if (c == 'A')
					mclose();
				return -1;
			}
			if (contains(mbuf, mlen, b, term == LF)) {
				if (c == 'J') {
					mmerge1();
					continue;
				}
				break;	/* stays in the buffer */
			}
			if (c == 'A' || c == 'J')
				mlen = -1;
			else
				mmerge1();
		}
		break;
	}
	ED.d->changed = 1;
	return 0;
}

/* Writing out (ESC W x) */

static void
wline(int i)
{
	int term = M.attrterm ? M.attrterm : 255, k, any = 0, m;
	unsigned char s[MAXW * 4], a[MAXW * 4];
	int n = 0;

	for (;;) {
		struct line *l = ed_line(i);

		for (k = 0; k < l->len && n < (int)sizeof s - 1; k++) {
			s[n] = l->s[k];
			a[n++] = l->a[k] & AT_MASK;
		}
		if (!(l->flags & LF_CONT))
			break;
		i++;
	}
	while (n > 0 && s[n - 1] == ' ' && !a[n - 1])
		n--;
	fwrite(s, 1, n, wf);
	for (k = 0; k < n; k++)
		if (a[k])
			any = 1;
	if (any) {
		putc(term, wf);
		putc('\n', wf);
		for (k = 0, m = 0; k < n; k++) {
			int v = (a[k] >> 1) & 15;

			a[k] = (unsigned char)((a[k] & 1) ? (v ? 'a' + v - 1 : '_') : (v ? 'A' + v - 1 : ' '));
			if (a[k] != ' ')
				m = k + 1;
		}
		fwrite(a, 1, m, wf);
	}
	putc('\n', wf);
}

/* the next logical line after row i */
static int
nextline(int i)
{
	while (i < ED.d->n && (ED.d->l[i].flags & LF_CONT))
		i++;
	return i + 1;
}

static int
lastrow(void)
{
	int n = ED.d->n;

	while (n > 0 && ED.d->l[n - 1].len == 0)
		n--;
	return n;
}

int
esc_extract(int c)
{
	char b[KSLEN + 1];
	int term, n, k, i;

	if (c == 'O') {
		char path[300], nm[MAXNAME + 1];

		b[0] = 0;
		if (getstring(text(0, 1), b, MAXNAME, &term, 0) <= 0)
			return -1;
		if (wf)
			fclose(wf);
		wf = NULL;
		strncpy(nm, b, MAXNAME);
		nm[MAXNAME] = 0;
		docname_file(path, nm);
		if (fexists(path)) {
			error("OF");
			in_abort();
			return -1;
		}
		if ((wf = fopen(path, "wb")) == NULL) {
			error("OW");
			in_abort();
			return -1;
		}
		strcpy(wname, nm);
		return 0;
	}
	if (c == 'C') {
		if (wf)
			fclose(wf);
		wf = NULL;
		return 0;
	}
	if (!strchr("EJUX", c) || !c) {
		error("AC");
		return -1;
	}
	if (wf == NULL) {
		error("WF");
		return -1;
	}
	switch (c) {
	case 'E':
	case 'X':
		for (i = ED.cl; i < lastrow(); i = nextline(i))
			wline(i);
		break;
	case 'J':
	case 'U':
		strcpy(b, funcstr[3]);
		{
			char pr[80];

			sprintf(pr, "%s%s", text(0, 44), text(0, c == 'J' ? 47 : 46));
			n = getstring(pr, b, KSLEN, &term, 0);
		}
		if (n < 0)
			return -1;
		strcpy(funcstr[3], b);
		for (k = 0; b[k] >= '0' && b[k] <= '9'; k++)
			;
		if (c == 'U' && term == LF && k > 0 && b[k] == 0) {
			long cnt = atol(b);

			for (i = ED.cl; cnt-- > 0 && i < lastrow(); i = nextline(i))
				wline(i);
			ED.cl = i;
			break;
		}
		for (i = ED.cl; i < lastrow(); i = nextline(i)) {
			struct line *l = ed_line(i);
			int has = contains(l->s, l->len, b, term == LF);

			if (c == 'U') {
				if (has)
					break;
				wline(i);
			} else if (has)
				wline(i);
		}
		if (c == 'U')
			ED.cl = i;
		break;
	}
	fflush(wf);
	return 0;
}

void
esc_closefiles(void)
{
	mclose();
	if (wf)
		fclose(wf);
	wf = NULL;
}

/* ESC O D, F, X: the playground saved as a document */
int
esc_open(void)
{
	int c = upcase(KC(in_key())), term;
	char b[MAXNAME + 1], path[300];

	if (c != 'D' && c != 'F' && c != 'X') {
		error("AC");
		return -1;
	}
	if (ED.mode != ED_PLAY) {
		error("OD");
		in_abort();
		return -1;
	}
	b[0] = 0;
	if (getstring(text(0, c == 'X' ? 53 : 52), b, MAXNAME, &term, 0) <= 0)
		return -1;
	docname_file(path, b);
	if (c == 'X' && fexists(path)) {
		error("MA");
		in_abort();
		return -1;
	}
	{
		FILE *f = fopen(path, "ab");

		if (f == NULL) {
			error("MW");
			in_abort();
			return -1;
		}
		fclose(f);
	}
	ED.mode = ED_EDIT;
	scopy(ED.path, path, sizeof ED.path - 1);
	scopy(ED.name, b, MAXNAME);
	ED.d->changed = 1;
	if (I.titling) {
		struct regent r;

		if (reg_get(b, &r) < 0)
			reg_new(&r, b);
		reg_put(&r);
	}
	return 0;
}

/* Sorting (ESC BA, BD, BHx, BVx) */

static int	sortcol, sortdesc;

static int
cmpkey(const unsigned char *a, int na, const unsigned char *b, int nb)
{
	int i;

	for (i = 0; i < na || i < nb; i++) {
		int x = i < na ? sortval(a[i]) : sortval(' ');
		int y = i < nb ? sortval(b[i]) : sortval(' ');

		if (x != y)
			return x - y;
	}
	return 0;
}

static int
cmplines(const void *p, const void *q)
{
	const struct line *a = (const struct line *)p, *b = (const struct line *)q;
	int r = cmpkey(a->len > sortcol ? a->s + sortcol : (unsigned char *)"", a->len > sortcol ? a->len - sortcol : 0,
	    b->len > sortcol ? b->s + sortcol : (unsigned char *)"", b->len > sortcol ? b->len - sortcol : 0);

	return sortdesc ? -r : r;
}

static int
sortend(int i)
{
	while (i < ED.d->n) {
		struct line *l = &ED.d->l[i];
		int k, blank = 1;

		for (k = 0; k < l->len; k++)
			if (l->s[k] != ' ')
				blank = 0;
		if (blank || (l->flags & LF_RULER) || (l->len >= 2 && l->s[0] == '.' && l->s[1] >= 'A' && l->s[1] <= 'Z'))
			break;
		i++;
	}
	return i;
}

struct item {
	unsigned char	s[MAXW + 1];
	int		n;
};

static int
cmpitems(const void *p, const void *q)
{
	const struct item *a = (const struct item *)p, *b = (const struct item *)q;
	int r = cmpkey(a->s, a->n, b->s, b->n);

	return sortdesc ? -r : r;
}

int
esc_sortk(int a, int b)
{
	int s = ED.cl, e = sortend(ED.cl);

	if (a == 'A' || a == 'D') {
		sortcol = ED.cc;
		sortdesc = a == 'D';
		if (e > s)
			qsort(ED.d->l + s, e - s, sizeof(struct line), cmplines);
		ED.d->changed = 1;
		return 0;
	}
	{
		struct ruler r;
		int pos[64], np = 0, w = 0, k, i, j, rows = e - s, n;
		struct item *it;

		ed_ruler(&r);
		for (k = 0; k < ED.cc && k < r.len; k++)
			if (r.s[k] != 'L' && r.s[k] != '.') {
				error("SR");
				return -1;
			}
		for (k = ED.cc; k < r.len && np < 64; k++)
			if (r.s[k] == 'L')
				pos[np++] = k;
		if (np == 0 || rows <= 0) {
			error("SR");
			return -1;
		}
		w = np > 1 ? pos[1] - pos[0] : r.len - pos[0];
		for (k = 1; k + 1 < np; k++)
			if (pos[k + 1] - pos[k] != w) {
				error("SL");
				return -1;
			}
		n = rows * np;
		it = (struct item *)xrealloc(NULL, n * sizeof(struct item));
		for (i = 0; i < rows; i++)
			for (j = 0; j < np; j++) {
				struct line *l = ed_line(s + i);
				struct item *t = &it[i * np + j];

				for (k = 0; k < w && k < MAXW; k++)
					t->s[k] = (unsigned char)line_ch(l, pos[j] + k);
				t->n = k;
				while (t->n > 0 && t->s[t->n - 1] == ' ')
					t->n--;
			}
		sortdesc = b == 'D';
		qsort(it, n, sizeof(struct item), cmpitems);
		for (k = 0; k < n; k++) {
			int row, col;

			if (a == 'H') {
				row = k / np;
				col = k % np;
			} else {
				row = k % rows;
				col = k / rows;
			}
			{
				struct line *l = ed_line(s + row);
				int m;

				for (m = 0; m < w; m++)
					line_put(l, pos[col] + m, m < it[k].n ? it[k].s[m] : ' ', 0);
				line_trim(l);
			}
		}
		xfree(it);
		ED.d->changed = 1;
	}
	return 0;
}

int
esc_sort(void)
{
	return 0;
}
