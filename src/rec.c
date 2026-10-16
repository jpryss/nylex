/*
 * rec.c - records of the system file as the editor sees them (manual
 * chapter 12 and C.16): keys on the screen, reading a record onto it,
 * writing what is on it away, forms (protect mode), masks, foreign
 * files, and the squashing of blanks in stored records.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

struct vmf	*sysvmf;
char		vmfname[256];
static char	lastkey[KEYLEN + 1];

/* the foreign file (ESC SF) */
static struct {
	int		type;
	struct vmf	*v;
	int		ro;
	char		spec[80];
} fgn;

/* the mask (ESC SM) */
static struct {
	int	on, type, total, n;
	int	pos[64], len[64], ro[64];
	char	name[12];
} msk;

void
mkkey(char *k, const char *name)
{
	int i;

	for (i = 0; i < KEYLEN; i++) {
		int c = (name && *name) ? (unsigned char)*name++ : ' ';

		if (I.fcase == 'U')
			c = upcase(c);
		else if (I.fcase == 'L')
			c = lowcase(c);
		if (c == '_')
			c = ' ';
		k[i] = (char)c;
	}
}

void
unsquash(const unsigned char *in, long n, char **out, long *outn, int sq)
{
	long i, m = 0;
	char *o;

	for (i = 0; i < n; i++)
		m += in[i] > 0 && in[i] < 32 ? in[i] : 1;
	o = (char *)xrealloc(NULL, m + 1);
	m = 0;
	for (i = 0; i < n; i++) {
		int c = in[i];

		if (c == 0)
			o[m++] = '\n';
		else if (c < 32) {
			memset(o + m, sq, c);
			m += c;
		} else
			o[m++] = (char)c;
	}
	o[m] = 0;
	*out = o;
	*outn = m;
}

void
squash(const char *in, long n, unsigned char **out, long *outn, int sq)
{
	long i, m = 0;
	unsigned char *o = (unsigned char *)xrealloc(NULL, n + 1);

	for (i = 0; i < n;) {
		int c = (unsigned char)in[i];

		if (c == sq && sq != '\n') {
			int run = 0;

			while (i < n && (unsigned char)in[i] == sq && run < 31) {
				run++;
				i++;
			}
			o[m++] = (unsigned char)run;
			continue;
		}
		if (c == '\n')
			c = 0;
		else if (c > 0 && c < 32)
			c = ' ';
		o[m++] = (unsigned char)c;
		i++;
	}
	o[m] = 0;
	*out = o;
	*outn = m;
}

struct vmf *
vmf_for(const char *key)
{
	if (fgn.v && key[0] == fgn.type)
		return fgn.v;
	return sysvmf;
}

unsigned char *
rec_get(const char *key, long *len)
{
	char k[KEYLEN];
	struct vmf *v;

	mkkey(k, key);
	v = vmf_for(k);
	if (v == NULL)
		return NULL;
	return vmf_get(v, k, len);
}

int
rec_text(const char *key, char **out, long *len, int sq)
{
	long n;
	unsigned char *d = rec_get(key, &n);

	if (d == NULL)
		return -1;
	unsquash(d, n, out, len, sq);
	return 0;
}

int
rec_put(const char *key, const unsigned char *data, long len)
{
	char k[KEYLEN];
	struct vmf *v;

	mkkey(k, key);
	v = vmf_for(k);
	if (v == NULL)
		return -1;
	if (v == fgn.v && fgn.ro) {
		error("FR");
		return -1;
	}
	if (v->readonly) {
		error("NR");
		return -1;
	}
	return vmf_put(v, k, data, len);
}

void
rec_lastkey(const char *key)
{
	mkkey(lastkey, key);
	lastkey[KEYLEN] = 0;
}

/* Foreign files */

int
foreign_set(const char *spec)
{
	char path[256];
	struct vmf *v;
	const char *f = spec + 4;

	if (strlen(spec) < 5)
		return -1;
	foreign_unset();
	while (*f == ' ')
		f++;
	strncpy(path, f, 250);
	path[250] = 0;
	if ((v = vmf_open(path)) == NULL) {
		strcat(path, ".vmf");
		if ((v = vmf_open(path)) == NULL)
			return -1;
	}
	fgn.v = v;
	fgn.type = (unsigned char)spec[0];
	fgn.ro = upcase((unsigned char)spec[2]) == 'R' || v->readonly;
	strncpy(fgn.spec, spec, 79);
	fgn.spec[79] = 0;
	return 0;
}

void
foreign_unset(void)
{
	if (fgn.v)
		vmf_close(fgn.v);
	fgn.v = NULL;
	fgn.spec[0] = 0;
}

int
foreign_desc(char *b)
{
	strcpy(b, fgn.v ? fgn.spec : "");
	return fgn.v != NULL;
}

int
foreign_has(const char *key)
{
	char k[KEYLEN];

	if (!fgn.v)
		return 0;
	mkkey(k, key);
	if (k[0] != fgn.type)
		return 0;
	return vmf_find(fgn.v, k, 0) >= 0;
}

/* Masks */

int
mask_set(const char *name)
{
	char key[12], *t, b[8];
	long len;
	int i;

	sprintf(key, "*,%.8s", name);
	if (rec_text(key, &t, &len, '_') < 0) {
		error("NM");
		return -1;
	}
	memset(&msk, 0, sizeof msk);
	msk.type = len > 1 ? (unsigned char)t[1] : 0;
	memcpy(b, t + 2, 4);
	b[4] = 0;
	msk.total = atoi(b);
	for (i = 0; i < 64 && 6 + 8 * i + 8 <= len; i++) {
		const char *e = t + 6 + 8 * i;

		memcpy(b, e, 4);
		b[4] = 0;
		if (atoi(b) <= 0)
			continue;
		msk.pos[msk.n] = atoi(b);
		memcpy(b, e + 4, 3);
		b[3] = 0;
		msk.len[msk.n] = atoi(b);
		msk.ro[msk.n] = e[7] != ' ' && e[7] != '_' && e[7] != 0;
		msk.n++;
	}
	xfree(t);
	strncpy(msk.name, name, 11);
	msk.on = 1;
	ED.mask = 1;
	return 0;
}

void
mask_unset(void)
{
	msk.on = 0;
	ED.mask = 0;
}

static int
masked(const char *key)
{
	return msk.on && (unsigned char)key[0] == msk.type;
}

/* Keys on the screen */

/* the start of a key before the cursor: up the screen */
int
key_find(int *line, int *col)
{
	int i, c;

	for (i = ED.cl; i >= ED.top && i >= 0; i--) {
		struct line *l = ed_line(i);

		c = i == ED.cl ? ED.cc - 1 : l->len - 1;
		if (c >= l->len)
			c = l->len - 1;
		for (; c >= 0; c--)
			if (l->s[c] == M.startkey) {
				*line = i;
				*col = c;
				return 0;
			}
	}
	return -1;
}

/* the key after the start at (line, col): 10 cells, or up to 18 with +nnn/mmm */
static void
screenkey(int line, int col, char *key, int *from, int *cnt)
{
	struct line *l = ed_line(line);
	char b[24];
	int k, n = 0;

	for (k = 1; k <= 18; k++) {
		int c = line_ch(l, col + k);

		if (c == M.paint)
			c = ' ';
		b[n++] = (char)c;
	}
	b[n] = 0;
	*from = *cnt = -1;
	for (k = 0; k < KEYLEN && b[k]; k++)
		if (b[k] == '+' && b[k + 1] >= '0' && b[k + 1] <= '9')
			break;
	if (k < KEYLEN && b[k] == '+') {
		*from = atoi(b + k + 1);
		if (strchr(b + k, '/'))
			*cnt = atoi(strchr(b + k, '/') + 1);
		b[k] = 0;
	} else
		b[KEYLEN] = 0;
	strcpy(key, b);
}

/* the cells of the record after the key: in protect mode the unprotected
   ones in order, otherwise all of them */
struct cellpos {
	int	l, c;
};

static int
cells(int line, int col, int toline, int tocol, struct cellpos **out)
{
	int n = 0, cap = 256, i = line, c = col + KEYLEN + 1;
	struct cellpos *p = (struct cellpos *)xrealloc(NULL, cap * sizeof(struct cellpos));

	while (i < ED.d->n || (i <= toline && toline >= 0)) {
		if (toline >= 0 && (i > toline || (i == toline && c >= tocol)))
			break;
		if (c >= t_cols) {
			i++;
			c = 0;
			continue;
		}
		if (toline < 0 && i >= ED.d->n)
			break;
		if (ed_unprot(i, c)) {
			if (n == cap) {
				cap *= 2;
				p = (struct cellpos *)xrealloc(p, cap * sizeof(struct cellpos));
			}
			p[n].l = i;
			p[n].c = c;
			n++;
		}
		c++;
	}
	*out = p;
	return n;
}

/* ESC ] R: the record on the screen after its key */
int
rec_read(int line, int col, int lock)
{
	char key[24], *t;
	long len;
	int from, cnt;

	(void)lock;
	if (line != ED.cl || ED.cc - col > 19) {
		error("NX");
		return -1;
	}
	screenkey(line, col, key, &from, &cnt);
	if (rec_text(key, &t, &len, ED.sqch) < 0) {
		error("NZ");
		return -1;
	}
	rec_lastkey(key);
	if (from > 0) {
		long n;

		if (from - 1 >= len)
			len = from - 1;
		n = cnt < 0 ? len - (from - 1) : cnt;
		if (from - 1 + n > len)
			n = len - (from - 1);
		memmove(t, t + from - 1, n);
		len = n;
		t[len] = 0;
	}
	if (ED.protect) {
		struct cellpos *p;
		int n = cells(line, col, -1, -1, &p), k = 0, i;

		if (masked(key)) {
			int f, j;

			for (f = 0; f < msk.n; f++)
				for (j = 0; j < msk.len[f]; j++) {
					int pos = msk.pos[f] - 1 + j;
					int ch = pos < len ? (unsigned char)t[pos] : M.paint;

					if (ch == '\n')
						ch = ' ';
					if (k >= n) {
						error("TM");
						goto done;
					}
					line_put(ed_line(p[k].l), p[k].c, ch, line_at(ed_line(p[k].l), p[k].c));
					k++;
				}
		} else {
			for (i = 0; i < len; i++) {
				int ch = (unsigned char)t[i];

				if (ch == '\n') {
					/* on to the first cell of a later line */
					int ln = k < n ? p[k].l : -1;

					while (k < n && p[k].l == ln)
						k++;
					continue;
				}
				if (k >= n) {
					error("TM");
					break;
				}
				line_put(ed_line(p[k].l), p[k].c, ch, line_at(ed_line(p[k].l), p[k].c));
				k++;
			}
		}
	done:
		/* the cursor is left after what was read: on a form made for
		   the record that is the field to write it from */
		if (k < n) {
			ED.cl = p[k].l;
			ED.cc = p[k].c;
		}
		xfree(p);
		ED.d->changed = 1;
		xfree(t);
		return 0;
	}
	/* the text after the key: its first line over the rest of the key's
	   line, the others put in below */
	{
		long i;
		int first = 1;

		ED.cl = line;
		ED.cc = col + KEYLEN + 1;
		for (i = 0; i <= len; i++) {
			long e = i;

			while (e < len && t[e] != '\n')
				e++;
			if (!first) {
				doc_insline(ED.d, ED.cl + 1, (const unsigned char *)"", 0);
				ED.cl++;
				ED.cc = 0;
			}
			{
				struct line *l = ed_line(ED.cl);
				long k;

				if (first)
					l->len = l->len > ED.cc ? ED.cc : l->len;
				for (k = i; k < e; k++)
					line_put(l, ED.cc++, (unsigned char)t[k], 0);
			}
			first = 0;
			i = e;
		}
		ed_markrulers();
		ED.d->changed = 1;
	}
	xfree(t);
	return 0;
}

/* ESC ] x / ESC > x: the screen from the key to the cursor written away */
int
rec_write(int line, int col, int how, int excl)
{
	char key[24], *data = NULL;
	long n = 0, cap;
	int from, cnt, i;
	char k10[KEYLEN];
	struct vmf *v;
	unsigned char *sq;
	long sqn;

	screenkey(line, col, key, &from, &cnt);
	mkkey(k10, key);
	if (k10[0] == ' ') {
		error("NX");
		return -1;
	}
	v = vmf_for(k10);
	if (v == NULL || v->readonly) {
		error("NR");
		return -1;
	}
	if (how == 'K') {
		if (vmf_delete(v, k10) < 0) {
			error("BK");
			return -1;
		}
		rec_lastkey(key);
		return 0;
	}
	if (how == 'S') {
		if (vmf_undelete(v, k10) < 0) {
			error("BS");
			return -1;
		}
		rec_lastkey(key);
		return 0;
	}
	if (how == 'C' && vmf_find(v, k10, 0) >= 0) {
		error("BC");
		return -1;
	}
	if (ED.protect) {
		struct cellpos *p;
		int nc = cells(line, col, ED.cl, ED.cc, &p);

		cap = nc + 1;
		data = (char *)xrealloc(NULL, cap + 1);
		if (masked(key)) {
			char *old;
			long ol;
			int f, j, k = 0;

			if (rec_text(key, &old, &ol, ED.sqch) < 0) {
				old = (char *)xrealloc(NULL, 1);
				ol = 0;
			}
			n = ol > msk.total ? ol : msk.total;
			data = (char *)xrealloc(data, n + 1);
			memset(data, ED.sqch == '\n' ? ' ' : ED.sqch, n);
			memcpy(data, old, ol);
			xfree(old);
			for (f = 0; f < msk.n; f++)
				for (j = 0; j < msk.len[f]; j++, k++) {
					int pos = msk.pos[f] - 1 + j;

					if (k >= nc || pos >= n || msk.ro[f])
						continue;
					data[pos] = (char)line_ch(ed_line(p[k].l), p[k].c);
				}
		} else
			for (i = 0; i < nc; i++)
				data[n++] = (char)line_ch(ed_line(p[i].l), p[i].c);
		if (how == 'U' || how == 'C') {
			for (i = 0; i < nc; i++)
				line_put(ed_line(p[i].l), p[i].c, M.paint, line_at(ed_line(p[i].l), p[i].c));
		}
		xfree(p);
	} else {
		int l = line, c = col + KEYLEN + 1;

		cap = 256;
		data = (char *)xrealloc(NULL, cap);
		while (l < ED.cl || (l == ED.cl && c < ED.cc)) {
			struct line *ln = ed_line(l);
			int e = l == ED.cl ? ED.cc : ln->len;
			int k;

			if (e > ln->len && l != ED.cl)
				e = ln->len;
			if (l != ED.cl)
				while (e > c && ln->s[e - 1] == ' ')
					e--;
			if (n + (e - c) + 2 >= cap) {
				cap = (n + (e - c) + 2) * 2;
				data = (char *)xrealloc(data, cap);
			}
			for (k = c; k < e; k++)
				data[n++] = (char)line_ch(ln, k);
			if (l == ED.cl)
				break;
			if (!(ln->flags & LF_CONT))
				data[n++] = excl ? ' ' : '\n';
			l++;
			c = 0;
		}
		while (n > 0 && data[n - 1] == ' ')
			n--;
	}
	if (from > 0) {
		/* a part of the record: the rest stays */
		char *old;
		long ol, at = from - 1, nl;

		if (rec_text(key, &old, &ol, ED.sqch) < 0) {
			old = (char *)xrealloc(NULL, 1);
			ol = 0;
		}
		if (cnt >= 0 && n > cnt)
			n = cnt;
		nl = at + (cnt >= 0 ? cnt : n) > ol ? at + (cnt >= 0 ? cnt : n) : ol;
		old = (char *)xrealloc(old, nl + 1);
		if (ol < nl)
			memset(old + ol, ' ', nl - ol);
		memcpy(old + at, data, n);
		if (cnt > n)
			memset(old + at + n, ' ', cnt - n);
		xfree(data);
		data = old;
		n = nl;
	}
	if (how == 'A') {
		char *old;
		long ol;

		if (rec_text(key, &old, &ol, ED.sqch) == 0) {
			old = (char *)xrealloc(old, ol + n + 2);
			memcpy(old + ol, data, n);
			xfree(data);
			data = old;
			n += ol;
		}
	}
	squash(data, n, &sq, &sqn, ED.sqch);
	xfree(data);
	i = rec_put(key, sq, sqn);
	xfree(sq);
	if (i < 0) {
		if (errcode[0] != 'F')
			error("NG");
		return -1;
	}
	rec_lastkey(key);
	ED.d->changed = 1;
	return 0;
}

/* ESC N, ESC -N, ESC QY: the next (previous, next deleted) key */
int
rec_nextkey(int dir, int deleted)
{
	struct vmf *v = vmf_for(lastkey[0] ? lastkey : " ");
	int i, line, col;
	char key[KEYLEN + 1];

	if (v == NULL)
		return -1;
	if (!lastkey[0])
		memset(lastkey, ' ', KEYLEN);
	i = vmf_seek(v, lastkey);
	if (dir > 0) {
		while (i < v->nent && (memcmp(v->ent[i].key, lastkey, KEYLEN) <= 0 ||
		    ((v->ent[i].flags & VMF_DELETED) != 0) != (deleted != 0)))
			i++;
	} else {
		i--;
		while (i >= 0 && ((v->ent[i].flags & VMF_DELETED) != 0))
			i--;
	}
	/* the keys of the same record type only (LEX-68's *DTIR stops at
	   the last document) */
	if (i < 0 || i >= v->nent || (lastkey[0] != ' ' && v->ent[i].key[0] != lastkey[0])) {
		error("NZ");
		return -1;
	}
	memcpy(key, v->ent[i].key, KEYLEN);
	key[KEYLEN] = 0;
	memcpy(lastkey, key, KEYLEN);
	/* into the key cells after a start on the line, or at the cursor */
	if (key_find(&line, &col) == 0 && line == ED.cl && ED.cc - col <= KEYLEN + 3) {
		struct line *l = ed_line(line);
		int k, c = col + 1;

		if (ED.protect) {
			/* on a form the key goes into the fields: a protected
			   cell that holds the key's character takes it, any
			   other protected cell is passed over (LEX-68's *DTIR:
			   "<:______.____" shows ":NAME   .EXT") */
			for (k = 0; k < KEYLEN && c < MAXW; c++) {
				if (ed_unprot(line, c))
					line_put(l, c, (unsigned char)key[k++], line_at(l, c));
				else if (line_ch(l, c) == (unsigned char)key[k])
					k++;
			}
			ED.cc = c;
			if (!ed_unprot(ED.cl, ED.cc)) {
				int ll = ED.cl, cc = ED.cc;

				if (ed_nextunprot(&ll, &cc, -1) && ll == ED.cl)
					ED.cc = cc;
			}
		} else {
			for (k = 0; k < KEYLEN; k++)
				line_put(l, col + 1 + k, (unsigned char)key[k], line_at(l, col + 1 + k));
			ED.cc = col + 1 + KEYLEN;
		}
	} else {
		struct line *l = ed_line(ED.cl);
		int k;

		for (k = 0; k < KEYLEN; k++)
			line_put(l, ED.cc + k, (unsigned char)key[k], line_at(l, ED.cc + k));
		ED.cc += KEYLEN;
	}
	ED.d->changed = 1;
	return 0;
}
