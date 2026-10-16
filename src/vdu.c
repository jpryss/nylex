/*
 * vdu.c - the terminal description records (manual chapter 8): )KEY
 * (*VDUFORM: the keys), ))KEY (*VDUFORM2: more keys, the sequences of
 * ESC SS n and of menu highlights), )=KEY (*VDUFORM3: attributes) and
 * );KEY (*VDUFORM4: sequences sent at set points, ESC ST n).
 *
 * The screen itself is always driven as an ANSI terminal (term.c); from
 * the records come what the keys mean and the attributes, taken from
 * the ANSI sequences in them.  Without a record the usual VT100/xterm
 * keys are understood.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

char	vdukey[11] = "VT100";
int	vdu_bgattr = A_BOLD, vdu_fgattr = A_NORM;
char	vdu_firstmenu[11];

static int	loaded;
static char	lead[4][12];		/* lead-ins of the escape sequence tables */
static char	tabA[4][65], tabB[4][65];
static char	lowasc[33];
static char	func[2][37 * 5 + 1];	/* function mappings: ) and )) */
static char	visq[3][256];		/* local visible equivalents: ), )), );... */
static char	gseq[10][64];		/* )) graphics sequences */
static char	pport[2][40];
static char	seq4[10][256];
static int	attrs[6] = { 0, A_UL, A_BOLD, A_BLINK, A_REV, A_DIM };
static int	pend[64], npend;

/* the local visible equivalents at the start of a record: 20 x "c999" */
static void
locvis(char *map, const char *r, long len)
{
	int i, c;

	for (i = 0; i < 256; i++)
		map[i] = (char)i;
	for (i = 0; i < 20; i++) {
		const char *e = r + 1 + 4 * i;

		if (1 + 4 * i + 4 > len)
			break;
		c = (unsigned char)e[0];
		if (c == '_' || e[1] < '0' || e[1] > '9')
			continue;
		map[c] = (char)atoi(e + 1);
		{
			char n[4];

			memcpy(n, e + 1, 3);
			n[3] = 0;
			map[c] = (char)atoi(n);
		}
	}
}

/* a field of n cells, '_' ending it, through the local equivalents */
static void
seqfield(char *out, const char *r, long len, int off, int n, const char *map, int cont)
{
	int i, k = 0;

	for (i = 0; (i < n || cont) && off + i < len; i++) {
		int c = (unsigned char)r[off + i];

		if (c == '_' || c == '\n')
			break;
		out[k++] = map[c];
	}
	out[k] = 0;
}

static void
cells(char *out, const char *r, long len, int off, int n)
{
	int i;

	for (i = 0; i < n; i++)
		out[i] = off + i < len ? r[off + i] : '_';
	out[n] = 0;
}

int
vdu_sgr(const char *s, int len)
{
	int a = 0, i = 0;

	while (i < len) {
		if ((unsigned char)s[i] == ESC && i + 1 < len && s[i + 1] == '[') {
			int v = 0, any = 0;

			i += 2;
			while (i < len) {
				if (s[i] >= '0' && s[i] <= '9') {
					v = v * 10 + s[i] - '0';
					any = 1;
				} else if (s[i] == ';' || s[i] == 'm') {
					if (v == 1) a |= A_BOLD;
					else if (v == 2) a |= A_DIM;
					else if (v == 4) a |= A_UL;
					else if (v == 5) a |= A_BLINK;
					else if (v == 7) a |= A_REV;
					else if (v == 0 && any) a = 0;
					v = 0;
					any = 0;
					if (s[i] == 'm') {
						i++;
						break;
					}
				} else {
					i++;
					break;
				}
				i++;
			}
		} else
			i++;
	}
	return a;
}

int
vdu_load(const char *key)
{
	char k[12], *r;
	long len;
	char b[80];
	int i;

	loaded = 0;
	memset(lead, 0, sizeof lead);
	memset(func, 0, sizeof func);
	memset(gseq, 0, sizeof gseq);
	memset(seq4, 0, sizeof seq4);
	memset(pport, 0, sizeof pport);
	vdu_firstmenu[0] = 0;
	vdu_bgattr = A_BOLD;
	attrs[1] = A_UL; attrs[2] = A_BOLD; attrs[3] = A_BLINK; attrs[4] = A_REV; attrs[5] = A_DIM;
	if (key == NULL || !*key)
		return -1;
	strncpy(vdukey, key[0] == ')' ? key + 1 : key, 9);
	vdukey[9] = 0;
	sprintf(k, ")%.9s", vdukey);
	if (rec_text(k, &r, &len, '_') < 0)
		return -1;
	locvis(visq[0], r, len);
	seqfield(b, r, len, 231, 6, visq[0], 0);
	if (b[0] && vdu_sgr(b, (int)strlen(b)))
		vdu_bgattr = vdu_sgr(b, (int)strlen(b));
	for (i = 0; i < 2; i++) {
		seqfield(lead[i], r, len, 237 + 132 * i, 4, visq[0], 1);
		cells(tabA[i], r, len, 241 + 132 * i, 64);
		cells(tabB[i], r, len, 305 + 132 * i, 64);
	}
	cells(lowasc, r, len, 501, 32);
	cells(func[0], r, len, 533, 180);
	cells(b, r, len, 713, 10);
	for (i = 9; i >= 0 && (b[i] == '_' || b[i] == ' '); i--)
		b[i] = 0;
	strcpy(vdu_firstmenu, b);
	xfree(r);
	loaded = 1;
	sprintf(k, "))%.8s", vdukey);
	if (rec_text(k, &r, &len, '_') == 0) {
		locvis(visq[1], r, len);
		for (i = 0; i < 2; i++) {
			seqfield(lead[2 + i], r, len, 81 + 132 * i, 4, visq[1], 1);
			cells(tabA[2 + i], r, len, 85 + 132 * i, 64);
			cells(tabB[2 + i], r, len, 149 + 132 * i, 64);
		}
		cells(func[1], r, len, 345, 180);
		for (i = 0; i < 10; i++) {
			int o = 525 + 10 * i, j, n = 0;

			/* up to the first underline, on into the next fields */
			for (j = o; j < len && r[j] != '_' && n < 63; j++)
				gseq[i][n++] = (char)(unsigned char)r[j] == (char)255 ? (char)255 : visq[1][(unsigned char)r[j]];
			gseq[i][n] = 0;
		}
		seqfield(pport[0], r, len, 625, 16, visq[1], 0);
		seqfield(pport[1], r, len, 641, 16, visq[1], 0);
		xfree(r);
	}
	sprintf(k, ")=%.8s", vdukey);
	if (rec_text(k, &r, &len, '_') == 0) {
		char map[256];

		locvis(map, r, len);
		for (i = 1; i <= 5; i++) {
			seqfield(b, r, len, 433 + 32 * (i - 1), 16, map, 0);
			if (b[0] && vdu_sgr(b, (int)strlen(b)))
				attrs[i] = vdu_sgr(b, (int)strlen(b));
		}
		xfree(r);
	}
	sprintf(k, ");%.8s", vdukey);
	if (rec_text(k, &r, &len, '_') == 0) {
		char map[256], sep;
		int n = 0, j, o = 82;

		locvis(map, r, len);
		sep = len > 81 ? r[81] : '_';
		for (i = 0; i < 10 && o < len; i++) {
			n = 0;
			while (o < len && r[o] == '_')
				o++;
			for (j = o; j < len && r[j] != sep && n < 255; j++)
				seq4[i][n++] = map[(unsigned char)r[j]];
			seq4[i][n] = 0;
			o = j + 1;
		}
		xfree(r);
	}
	return 0;
}

int
vdu_attr(int a)
{
	int i, r = 0;

	for (i = 1; i <= 5; i++)
		if (a & (1 << (i - 1)))
			r |= attrs[i];
	return r;
}

void
vdu_seq4(int n)
{
	if (n >= 0 && n < 10 && seq4[n][0])
		t_rawout(seq4[n], (int)strlen(seq4[n]));
}

void
vdu_seq2(int n)
{
	if (n >= 0 && n < 10 && gseq[n][0])
		t_rawout(gseq[n], (int)strlen(gseq[n]));
}

void
vdu_send(const char *s, int n)
{
	t_rawout(s, n);
}

/* the attribute of a menu highlight (/Smno): sequence n of ))KEY */
void
vdu_menuattr(int n, int *attr)
{
	if (n < 0 || n > 9)
		return;
	if (gseq[n][0])
		*attr = vdu_sgr(gseq[n], (int)strlen(gseq[n]));
	else if (n == 0)
		*attr = A_NORM;
}

const char *
vdu_pport(int on)
{
	return pport[on ? 0 : 1];
}

/* a function mapping: its characters until '_', on into the next fields */
static void
dofunc(int set, int c)
{
	int i, n = 0, idx;
	char s[64];
	const char *map = visq[set];

	if (c >= 'a' && c <= 'z')
		idx = c - 'a';
	else if (c >= '0' && c <= '9')
		idx = 26 + c - '0';
	else
		return;
	for (i = idx * 5; i < 180 && func[set][i] && func[set][i] != '_' && n < 63; i++)
		s[n++] = map[(unsigned char)func[set][i]];
	if (n)
		in_kbd(s, n);
}

/* what a table entry says: a control function, a mapping, or nothing */
static int
entry(int set, int e)
{
	if (e >= 'A' && e <= 'Z')
		return CTL(e);
	if (e == '@')
		return 0;
	if ((e >= 'a' && e <= 'z') || (e >= '0' && e <= '9')) {
		dofunc(set, e);
		return -2;
	}
	return -2;		/* not used: ignored */
}

static int
byte(int tenths)
{
	if (npend > 0) {
		int c = pend[0];

		memmove(pend, pend + 1, --npend * sizeof(int));
		return c;
	}
	return t_getbyte(tenths);
}

static void
unread(const int *b, int n)
{
	if (n <= 0)
		return;
	memmove(pend + n, pend, npend * sizeof(int));
	memcpy(pend, b, n * sizeof(int));
	npend += n;
}

static int
utf8(int c)
{
	int n, u, d;

	if (t_charset != CS_UTF8 || c < 0xc0)
		return t_fromkbd(c);
	n = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
	u = c & (0x3f >> n);
	while (n-- > 0) {
		if ((d = byte(-1)) < 0)
			return K_NONE;
		u = (u << 6) | (d & 0x3f);
	}
	if (u > 0xff)
		return -2;
	return t_fromkbd(u);
}

/* the last key came from a key of its own (an arrow, a function key ...)
   rather than being typed as a control character */
int	vdu_special;

/* without a terminal record: what term.c makes of the usual keys */
static int
builtin(void)
{
	int k = t_getkey();

	vdu_special = k >= 0x100 && k != K_NONE;
	switch (k) {
	case K_UP: return CTL('U');
	case K_DOWN: return CTL('J');
	case K_LEFT: return CTL('H');
	case K_RIGHT: return CTL('R');
	case K_HOME: return CTL('T');
	case K_END: return CTL('G');
	case K_PGUP: return CTL('T');
	case K_PGDN: return CTL('L');
	case K_DEL: return CTL('P');
	case K_INS:
		in_kbd("\033QD", 3);
		return -2;
	}
	if (k >= K_F1 && k < K_F1 + 12) {
		char s[2];

		s[0] = ESC;
		s[1] = (char)('0' + (k - K_F1 + 1) % 10);
		in_kbd(s, 2);
		return -2;
	}
	return k;
}

int
vdu_key(void)
{
	int c, i, k;

	for (;;) {
		vdu_special = 0;
		if (!loaded) {
			t_refresh();
			k = builtin();
			return k;
		}
		t_refresh();
		c = byte(-1);
		if (c < 0)
			return K_NONE;
		/* the lead-ins of escape sequences */
		for (i = 0; i < 4; i++) {
			int got[16], n = 1, j, ok = 1, x, set = i < 2 ? 0 : 1;

			if (!lead[i][0] || (unsigned char)lead[i][0] != c)
				continue;
			got[0] = c;
			for (j = 1; lead[i][j]; j++) {
				x = byte(1);
				if (x < 0) {
					ok = 0;
					break;
				}
				got[n++] = x;
				if (x != (unsigned char)lead[i][j]) {
					ok = 0;
					break;
				}
			}
			if (!ok) {
				unread(got + 1, n - 1);
				continue;
			}
			x = byte(1);
			if (x < 0) {
				unread(got + 1, n - 1);
				continue;
			}
			vdu_special = 1;
			if (x >= '0' && x <= '9') {
				int v = x - '0';

				while ((x = byte(1)) >= '0' && x <= '9')
					v = v * 10 + x - '0';
				k = v < 32 ? entry(set, (unsigned char)tabA[i][v]) : -2;
			} else if (x >= 0x20 && x < 0x40)
				k = entry(set, (unsigned char)tabA[i][32 + x - 0x20]);
			else if (x >= 0x40 && x < 0x80)
				k = entry(set, (unsigned char)tabB[i][x - 0x40]);
			else
				k = -2;
			return k;
		}
		if (c == ESC || c == DEL)
			return c;
		if (c < 32) {
			int e = (unsigned char)lowasc[c];

			if (e >= 'A' && e <= 'Z')
				return CTL(e);
			if ((e >= 'a' && e <= 'z') || (e >= '0' && e <= '9')) {
				dofunc(0, e);
				return -2;
			}
			if (c == CR || c == TAB || c == LF || c == BS)
				return c;
			continue;	/* not used */
		}
		if (c >= 0x80) {
			k = utf8(c);
			if (k == -2)
				continue;
			return k;
		}
		return c;
	}
}
