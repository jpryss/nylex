/*
 * lexspell - the spelling program of nylex, called as LEX called its
 * own (/lib/lexspell in LEX-68's system file): from a menu, by the
 * options /B /D /H, with a record that names the program (characters
 * 1-32), the dictionary (33-64), the user's word list (65-96), the
 * common word list (97-128), an output device (129-160) and what to do
 * (161), or, when that is blank, the first character of keystore 0.
 * nylex gives the record in $LEXREC, keystore 0 in $LEXKS0 and the
 * terminal's code in $LEXCS; the documents are the arguments.
 *
 *	C	check a document; unknown words are shown with suggestions
 *	A	the same, with keystore 0's word list as well
 *	E	the unknown words of a document into another
 *	S	spell words: right, or the suggestions
 *	M	words that fit a pattern (? a letter, * any letters)
 *	L	the dictionary from a word on
 *	W F	add words, a file of words to the common list
 *	X Y	take words, a file of words out (into the common list as
 *		words not to accept)
 *
 * Without $LEXREC:  lexspell [-d dict] [-u userlist] [-c commonlist]
 *	[-k swe7|latin1|utf8] FUNCTION [indoc [outdoc]]
 * A dictionary named without a / is LEXLIB/name.dict: sv, en-GB (or
 * en), en-US; $LEXLIB, when set, is used for LEXLIB.
 *
 * The dictionary is made by tools/mkdict.py of a Hunspell dictionary:
 * every inflected form, with bits for the parts of compounds, looked up
 * by binary search; a word not in it is right if it is made of parts
 * that may begin, go in the middle of and end a compound (Swedish).
 * The word lists are text files, a word a line ("-word": not a word).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LEXLIB
#define LEXLIB	"/usr/local/share/nylex"
#endif

#define MAXW	64
#define MAXSUG	9

/* The dictionary */

static FILE	*df;
static long	nwords, nblocks, indexoff;
static int	perblock;
static long	*boff;
static char	**bfirst;
static char	lang[3] = "sv";
static long	cacheblk = -1;
static char	(*cw)[MAXW];		/* the words of the block read last */
static unsigned char *cf;
static int	ncw;

static long
be4(const unsigned char *p)
{
	return ((long)p[0] << 24) | ((long)p[1] << 16) | ((long)p[2] << 8) | p[3];
}

static void *
xmalloc(unsigned long n)
{
	void *p = malloc(n ? n : 1);

	if (p == NULL) {
		fprintf(stderr, "lexspell: out of memory\n");
		exit(1);
	}
	return p;
}

/* old C libraries do not take realloc(NULL) */
static void *
xrealloc(void *p, unsigned long n)
{
	p = p ? realloc(p, n ? n : 1) : malloc(n ? n : 1);
	if (p == NULL) {
		fprintf(stderr, "lexspell: out of memory\n");
		exit(1);
	}
	return p;
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

static int
dict_open(const char *path)
{
	unsigned char h[28];
	long i, len;
	char *buf, *p;

	if ((df = fopen(path, "rb")) == NULL)
		return -1;
	if (fread(h, 1, 28, df) != 28 || memcmp(h, "LEXDICT1", 8) != 0) {
		fclose(df);
		df = NULL;
		return -2;
	}
	lang[0] = (char)h[8];
	lang[1] = (char)h[9];
	nwords = be4(h + 12);
	perblock = (h[16] << 8) | h[17];
	/* the header: 8 magic, 2 language, 2 spare, 4 words, 2 a block,
	   2 spare, 4 blocks, 4 index */
	nblocks = be4(h + 20);
	indexoff = be4(h + 24);
	fseek(df, 0L, 2);
	len = ftell(df) - indexoff;
	buf = (char *)xmalloc(len + 1);
	fseek(df, indexoff, 0);
	if ((long)fread(buf, 1, len, df) != len)
		return -2;
	boff = (long *)xmalloc(nblocks * sizeof(long));
	bfirst = (char **)xmalloc(nblocks * sizeof(char *));
	p = buf;
	for (i = 0; i < nblocks; i++) {
		boff[i] = be4((unsigned char *)p);
		bfirst[i] = p + 4;
		p += 4 + strlen(p + 4) + 1;
	}
	cw = (char (*)[MAXW])xmalloc(perblock * MAXW);
	cf = (unsigned char *)xmalloc(perblock);
	return 0;
}

static void
readblock(long b)
{
	char prev[MAXW];
	int n = 0, c;

	if (b == cacheblk)
		return;
	cacheblk = b;
	fseek(df, boff[b], 0);
	prev[0] = 0;
	while (n < perblock) {
		int share = getc(df), k;

		if (share == EOF)
			break;
		if (b + 1 < nblocks && ftell(df) > boff[b + 1])
			break;
		k = share;
		memcpy(cw[n], prev, k);
		while ((c = getc(df)) != EOF && c != 0)
			if (k < MAXW - 1)
				cw[n][k++] = (char)c;
		cw[n][k] = 0;
		cf[n] = (unsigned char)getc(df);
		strcpy(prev, cw[n]);
		n++;
		if (ftell(df) >= indexoff)
			break;
	}
	ncw = n;
}

/* the flag bits of a word in the dictionary: 0 not there */
static int
dict_bits(const char *w)
{
	long lo = 0, hi = nblocks - 1, mid;
	int i;
	char t[MAXW];

	if (df == NULL || nblocks == 0)
		return 0;
	if (lang[0] != 's') {
		/* English text is Latin-1: e-acute is the dictionary's ` and
		   @; a word with other letters is taken as it is */
		for (i = 0; w[i] && i < MAXW - 1; i++) {
			int c = (unsigned char)w[i];

			if (c == 0xe9 || c == 0xc9)
				c = c == 0xe9 ? '`' : '@';
			else if (c >= 0x80)
				return 1 | 2 | 4 | 8;
			t[i] = (char)c;
		}
		t[i] = 0;
		w = t;
	}
	if (strcmp(w, bfirst[0]) < 0)
		return 0;
	while (lo < hi) {
		mid = (lo + hi + 1) / 2;
		if (strcmp(bfirst[mid], w) <= 0)
			lo = mid;
		else
			hi = mid - 1;
	}
	readblock(lo);
	for (i = 0; i < ncw; i++) {
		int c = strcmp(cw[i], w);

		if (c == 0)
			return cf[i] & 0x0f;
		if (c > 0)
			break;
	}
	return 0;
}

/* The word lists */

struct list {
	char	**w;
	int	n, max;
	char	path[256];
	int	changed;
};

static struct list	user, common, extra, session;

static void
list_add(struct list *l, const char *w)
{
	if (l->n == l->max) {
		l->max = l->max ? l->max * 2 : 64;
		l->w = (char **)xrealloc(l->w, l->max * sizeof(char *));
		if (l->w == NULL) {
			fprintf(stderr, "lexspell: out of memory\n");
			exit(1);
		}
	}
	l->w[l->n] = (char *)xmalloc(strlen(w) + 1);
	strcpy(l->w[l->n++], w);
	l->changed = 1;
}

/* 1 the word is in the list, -1 it is there as not a word */
static int
list_has(const struct list *l, const char *w)
{
	int i, r = 0;

	for (i = 0; i < l->n; i++) {
		if (l->w[i][0] == '-' && strcmp(l->w[i] + 1, w) == 0)
			r = -1;
		else if (strcmp(l->w[i], w) == 0)
			r = 1;
	}
	return r;
}

static void
list_load(struct list *l, const char *path)
{
	FILE *f;
	char b[256];

	strncpy(l->path, path, 255);
	if ((f = fopen(path, "r")) == NULL)
		return;
	while (fgets(b, sizeof b, f)) {
		int n = (int)strlen(b);

		while (n > 0 && (b[n - 1] == '\n' || b[n - 1] == '\r' || b[n - 1] == ' '))
			b[--n] = 0;
		if (n > 0 && n < MAXW)
			list_add(l, b);
	}
	fclose(f);
	l->changed = 0;
}

static int
list_save(struct list *l)
{
	FILE *f;
	int i;

	if (!l->changed || !l->path[0])
		return 0;
	if ((f = fopen(l->path, "w")) == NULL)
		return -1;
	for (i = 0; i < l->n; i++)
		fprintf(f, "%s\n", l->w[i]);
	l->changed = 0;
	return fclose(f);
}

/* Letters: LEX's 7-bit Swedish ([ \ ] { | } and @ `) in Swedish */

static int
isletter(int c)
{
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		return 1;
	if (lang[0] == 's' && c && strchr("[\\]{|}@`", c))
		return 1;
	/* English: the letters of Latin-1 */
	if (lang[0] != 's' && c >= 0xc0 && c != 0xd7 && c != 0xf7 && c != 0xff)
		return 1;
	return 0;
}

static int
lower(int c)
{
	if (c >= 'A' && c <= 'Z')
		return c + 32;
	if (lang[0] == 's') {
		if (c == '[') return '{';
		if (c == '\\') return '|';
		if (c == ']') return '}';
		if (c == '@') return '`';
	}
	return c;
}

static int
upper(int c)
{
	if (c >= 'a' && c <= 'z')
		return c - 32;
	if (lang[0] == 's') {
		if (c == '{') return '[';
		if (c == '|') return '\\';
		if (c == '}') return ']';
		if (c == '`') return '@';
	}
	return c;
}

/* Checking a word */

static int
plain(const char *w)
{
	int r;

	if ((r = list_has(&common, w)) != 0)
		return r > 0;
	if (list_has(&user, w) > 0 || list_has(&extra, w) > 0 || list_has(&session, w) > 0)
		return 1;
	return dict_bits(w) & 1;
}

/* w made of compound parts from pos on; first: the part is the first */
static int
compound(const char *w, int first)
{
	int n = (int)strlen(w), k;
	char part[MAXW];

	if (lang[0] != 's')
		return 0;
	for (k = n - 2; k >= 2; k--) {
		int b;

		memcpy(part, w, k);
		part[k] = 0;
		b = dict_bits(part);
		if (!(b & (first ? 2 : 4 | 2)))
			continue;
		if (dict_bits(w + k) & 8)
			return 1;
		/* a triple consonant written as two: "tunn" + "nål" */
		if (k > 1 && w[k - 1] == w[k - 2]) {
			char t[MAXW];

			t[0] = w[k - 1];
			strcpy(t + 1, w + k);
			if (dict_bits(t) & 8)
				return 1;
		}
		if (compound(w + k, 0))
			return 1;
	}
	return 0;
}

static int
ok_as(const char *w)
{
	return plain(w) || compound(w, 1);
}

/* right as written, or with the case of the start of a sentence */
static int
correct(const char *w)
{
	char t[MAXW];
	int i, allup = 1;

	if (ok_as(w))
		return 1;
	for (i = 0; w[i]; i++)
		if (lower((unsigned char)w[i]) == (unsigned char)w[i] && isletter((unsigned char)w[i]))
			allup = 0;
	strcpy(t, w);
	t[0] = (char)lower((unsigned char)t[0]);
	if (ok_as(t))
		return 1;
	if (allup) {
		for (i = 0; t[i]; i++)
			t[i] = (char)lower((unsigned char)t[i]);
		if (ok_as(t))
			return 1;
		t[0] = (char)upper((unsigned char)t[0]);
		if (ok_as(t))
			return 1;
	}
	return 0;
}

/* Suggestions: one change (a letter out, in, changed, two swapped) */

static char	sug[MAXSUG][MAXW];
static int	nsug;

static void
addsug(const char *w)
{
	int i;

	if (nsug >= MAXSUG)
		return;
	for (i = 0; i < nsug; i++)
		if (strcmp(sug[i], w) == 0)
			return;
	if (correct(w))
		strcpy(sug[nsug++], w);
}

static void
suggest(const char *w)
{
	const char *alpha = lang[0] == 's' ? "aeirnstlodkmgpbvhfuj}{|yc`xwqz" :
	    "esiarntolcdugpmhbyfvkwzxqj";
	char t[MAXW + 2];
	int n = (int)strlen(w), i, k, up = upper((unsigned char)w[0]) == (unsigned char)w[0];

	nsug = 0;
	if (n < 1 || n >= MAXW - 1)
		return;
	/* a name written small */
	strcpy(t, w);
	t[0] = (char)upper((unsigned char)t[0]);
	if (t[0] != w[0])
		addsug(t);
	/* two letters swapped */
	for (i = 0; i + 1 < n; i++) {
		strcpy(t, w);
		t[i] = w[i + 1];
		t[i + 1] = w[i];
		addsug(t);
	}
	/* a letter changed */
	for (i = 0; i < n; i++)
		for (k = 0; alpha[k]; k++) {
			if (alpha[k] == lower((unsigned char)w[i]))
				continue;
			strcpy(t, w);
			t[i] = (char)(i == 0 && up ? upper(alpha[k]) : alpha[k]);
			addsug(t);
		}
	/* a letter out */
	for (i = 0; i < n; i++) {
		memcpy(t, w, i);
		strcpy(t + i, w + i + 1);
		addsug(t);
	}
	/* a letter in */
	for (i = 0; i <= n; i++)
		for (k = 0; alpha[k]; k++) {
			memcpy(t, w, i);
			t[i] = alpha[k];
			strcpy(t + i + 1, w + i);
			addsug(t);
		}
	/* two words */
	for (i = 3; i <= n - 3 && nsug < MAXSUG; i++) {
		char a[MAXW], b[MAXW];

		memcpy(a, w, i);
		a[i] = 0;
		strcpy(b, w + i);
		if (correct(a) && correct(b)) {
			memcpy(sug[nsug], a, i);
			sug[nsug][i] = ' ';
			strcpy(sug[nsug] + i + 1, b);
			nsug++;
		}
	}
}

/* The terminal: line by line, LEX's 7-bit Swedish shown as it can */

static int	cs = 1;		/* 0 swe7, 1 latin1, 2 utf8 */

static void
putch(int c)
{
	static const char s7[] = "[\\]{|}@`";
	static const unsigned char l1[] = { 0xc4, 0xd6, 0xc5, 0xe4, 0xf6, 0xe5, 0xc9, 0xe9 };
	const char *p;

	c &= 0xff;
	if (lang[0] != 's') {
		/* English: Latin-1, the dictionary's e-acute as ` */
		if (c == '`')
			c = 0xe9;
		if (c >= 0x80 && cs == 0)
			c = '?';
	} else if (cs != 0 && c && (p = strchr(s7, c)) != NULL)
		c = l1[p - s7];
	if (c >= 0x80 && cs == 2) {
		putchar(0xc0 | (c >> 6));
		putchar(0x80 | (c & 0x3f));
		return;
	}
	putchar(c);
}

static void
puts7(const char *s)
{
	while (*s)
		putch((unsigned char)*s++);
}

/* a line from the keyboard, in LEX's code; -1 at the end */
static int
getline7(char *b, int max)
{
	int c, n = 0;

	fflush(stdout);
	while ((c = getchar()) != EOF && c != '\n') {
		if (c == '\r')
			continue;
		if (cs == 2 && (c & 0xe0) == 0xc0) {
			int d = getchar();

			c = ((c & 0x1f) << 6) | (d & 0x3f);
		}
		if (c >= 0x80 && lang[0] != 's') {
			if (c < 0xc0 || c == 0xd7 || c == 0xf7)
				continue;
		} else if (c >= 0x80) {
			static const unsigned char l1[] = { 0xc4, 0xd6, 0xc5, 0xe4, 0xf6, 0xe5, 0xc9, 0xe9 };
			static const char s7[] = "[\\]{|}@`";
			int i;

			for (i = 0; i < 8; i++)
				if (l1[i] == c)
					break;
			if (i == 8)
				continue;
			c = s7[i];
		}
		if (n < max - 1)
			b[n++] = (char)c;
	}
	b[n] = 0;
	return c == EOF && n == 0 ? -1 : n;
}

/* the messages, Swedish or English after the dictionary */
static const char *
msg(int n)
{
	static const char *sv[] = {
		"Stavningskontroll", "Ok{nt ord", "F|rslag", "Inga f|rslag",
		"Siffra: f|rslag, R: r{tta, G: godta, E: egen ordlista, A: avsluta, Z: avbryt",
		"Nytt ord: ", "Dokumentet sparat", "Inga {ndringar", "Ord (Return slutar): ",
		"r{tt stavat", "M|nster (? en bokstav, * flera): ", "Fr}n ord: ",
		"Return: mer, A: avsluta", "tillagt", "borttaget", "Ok{nda ord: ",
		"Ordlistan finns ej: ", "Dokumentet finns ej: ", "Ingen ordlista angiven",
		"ord kontrollerade", "Ok{nd funktion: ", "Tryck Return f|r att forts{tta"
	};
	static const char *en[] = {
		"Spelling check", "Unknown word", "Suggestions", "No suggestions",
		"Number: suggestion, R: correct, A: accept, L: own list, F: finish, Q: abandon",
		"New word: ", "Document saved", "No changes", "Word (Return ends): ",
		"spelt right", "Pattern (? a letter, * letters): ", "From word: ",
		"Return: more, F: finish", "added", "taken out", "Unknown words: ",
		"No dictionary: ", "No document: ", "No dictionary given",
		"words checked", "Unknown function: ", "Press Return to go on"
	};

	return lang[0] == 's' ? sv[n] : en[n];
}

/* Documents */

struct dline {
	char	*s;
	int	len;
	int	attr;		/* an attribute line (after one ending in 255) */
	int	skip;		/* a ruler or a control line */
};

static struct dline	*dl;
static int		ndl;

static int
isruler(const char *s, int n)
{
	int i, dots = 0, marks = 0;

	if (n < 20)
		return 0;
	for (i = 0; i < n; i++)
		if (s[i] == '.')
			dots++;
		else if (s[i] && strchr("CFJMLRTH#,", s[i]))
			marks++;
		else
			return 0;
	return marks > 0 && dots >= n / 3;
}

static int
doc_read(const char *path)
{
	FILE *f = fopen(path, "rb");
	char b[2048];
	int max = 0, afterff = 0;

	if (f == NULL)
		return -1;
	ndl = 0;
	while (fgets(b, sizeof b, f)) {
		int n = (int)strlen(b);
		struct dline *l;

		if (n > 0 && b[n - 1] == '\n')
			b[--n] = 0;
		if (ndl == max) {
			max = max ? max * 2 : 256;
			dl = (struct dline *)xrealloc(dl, max * sizeof *dl);
			if (dl == NULL)
				exit(1);
		}
		l = &dl[ndl++];
		l->s = (char *)xmalloc(n + 1);
		memcpy(l->s, b, n + 1);
		l->len = n;
		l->attr = afterff;
		l->skip = afterff || isruler(b, n) || (n >= 2 && b[0] == '.' && b[1] >= 'A' && b[1] <= 'Z');
		afterff = !afterff && n > 0 && (unsigned char)b[n - 1] == 0xff;
	}
	fclose(f);
	return 0;
}

static int
doc_write(const char *path)
{
	FILE *f = fopen(path, "wb");
	int i;

	if (f == NULL)
		return -1;
	for (i = 0; i < ndl; i++) {
		fwrite(dl[i].s, 1, dl[i].len, f);
		putc('\n', f);
	}
	return fclose(f);
}

/* line i, from at, n characters, becomes w (its attribute line too) */
static void
replace(int i, int at, int n, const char *w)
{
	struct dline *l = &dl[i];
	int m = (int)strlen(w), j;
	char *s = (char *)xmalloc(l->len - n + m + 1);

	memcpy(s, l->s, at);
	memcpy(s + at, w, m);
	memcpy(s + at + m, l->s + at + n, l->len - at - n + 1);
	free(l->s);
	l->s = s;
	l->len += m - n;
	if (i + 1 < ndl && dl[i + 1].attr && at < dl[i + 1].len) {
		/* the attribute line (under the text; beyond its end the word
		   has none): the word's cells get the attribute of its first */
		struct dline *a = &dl[i + 1];
		int end = at + n < a->len ? at + n : a->len;
		int len = a->len - (end - at) + m;
		char *t = (char *)xmalloc(len + 1);
		int c = a->s[at];

		memcpy(t, a->s, at);
		for (j = 0; j < m; j++)
			t[at + j] = (char)c;
		memcpy(t + at + m, a->s + end, a->len - end);
		t[len] = 0;
		free(a->s);
		a->s = t;
		a->len = len;
	}
}

/* the next word from line i, column *c on: its start, its length */
static int
nextword(int i, int *c, int *len)
{
	const char *s = dl[i].s;
	int k = *c, n = dl[i].len, e;

	for (;;) {
		while (k < n && !isletter((unsigned char)s[k]))
			k++;
		if (k >= n)
			return 0;
		e = k;
		while (e < n && (isletter((unsigned char)s[e]) ||
		    (s[e] == '\'' && e + 1 < n && isletter((unsigned char)s[e + 1]) && lang[0] == 'e')))
			e++;
		/* words with digits in them are not checked */
		if ((k > 0 && s[k - 1] >= '0' && s[k - 1] <= '9') || (e < n && s[e] >= '0' && s[e] <= '9')) {
			k = e;
			continue;
		}
		*c = k;
		*len = e - k;
		return 1;
	}
}

static void
showline(int i, int at, int n)
{
	int k;

	printf("\n");
	if (i > 0 && !dl[i - 1].skip) {
		puts7(dl[i - 1].s);
		printf("\n");
	}
	for (k = 0; k < dl[i].len; k++) {
		if (k == at)
			printf("\033[7m");
		putch((unsigned char)dl[i].s[k]);
		if (k == at + n - 1)
			printf("\033[m");
	}
	printf("\n");
	if (i + 1 < ndl && !dl[i + 1].skip) {
		puts7(dl[i + 1].s);
		printf("\n");
	}
}

/* C, A: the document checked; the corrections written to out (or it) */
static int
check(const char *in, const char *out)
{
	int i, changed = 0, words = 0, unknown = 0;

	if (doc_read(in) < 0) {
		puts7(msg(17));
		puts7(in);
		printf("\n");
		return 1;
	}
	printf("\033[H\033[J");
	puts7(msg(0));
	printf(": ");
	puts7(in);
	printf("\n");
	for (i = 0; i < ndl; i++) {
		int c = 0, n;

		if (dl[i].skip)
			continue;
		while (nextword(i, &c, &n)) {
			char w[MAXW], b[MAXW + 8];

			if (n >= MAXW) {
				c += n;
				continue;
			}
			memcpy(w, dl[i].s + c, n);
			w[n] = 0;
			words++;
			if (correct(w)) {
				c += n;
				continue;
			}
			unknown++;
			for (;;) {
				int k;

				showline(i, c, n);
				puts7(msg(1));
				printf(": ");
				puts7(w);
				printf("\n");
				suggest(w);
				if (nsug == 0)
					puts7(msg(3));
				else {
					puts7(msg(2));
					printf(":");
					for (k = 0; k < nsug; k++) {
						printf("  %d ", k + 1);
						puts7(sug[k]);
					}
				}
				printf("\n");
				puts7(msg(4));
				printf("\n> ");
				if (getline7(b, sizeof b) < 0)
					b[0] = 'Z';
				k = b[0] >= 'a' ? b[0] - 32 : b[0];
				if (lang[0] != 's')	/* the English letters: the Swedish ones */
					k = k == 'A' ? 'G' : k == 'L' ? 'E' : k == 'F' ? 'A' : k == 'Q' ? 'Z' : k == 'G' || k == 'E' || k == 'Z' ? '?' : k;
				if (k >= '1' && k <= '9' && k - '1' < nsug) {
					replace(i, c, n, sug[k - '1']);
					n = (int)strlen(sug[k - '1']);
					changed = 1;
					break;
				}
				if (k == 'R') {
					puts7(msg(5));
					if (getline7(b, sizeof b) > 0) {
						replace(i, c, n, b);
						n = (int)strlen(b);
						changed = 1;
					}
					break;
				}
				if (k == 'G' || k == 0) {
					list_add(&session, w);
					session.changed = 0;
					break;
				}
				if (k == 'E') {
					list_add(&user, w);
					break;
				}
				if (k == 'A')
					goto done;
				if (k == 'Z') {
					changed = 0;
					goto done;
				}
			}
			c += n;
		}
	}
done:
	printf("\n%d %s, %d %s\n", words, msg(19), unknown, lang[0] == 's' ? "ok{nda" : "unknown");
	if (changed) {
		const char *to = out && out[0] ? out : in;

		if (!(out && out[0])) {
			char bak[300], *dot;

			strcpy(bak, in);
			dot = strrchr(bak, '.');
			if (dot == NULL || strchr(dot, '/'))
				dot = bak + strlen(bak);
			strcpy(dot, ".bak");
			remove(bak);
			rename(in, bak);
		}
		if (doc_write(to) == 0) {
			puts7(msg(6));
			printf(": ");
			puts7(to);
			printf("\n");
		}
	} else {
		puts7(msg(7));
		printf("\n");
	}
	return 0;
}

static int
cmpstr(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* E: the unknown words of in, one a line, into out */
static int
extract(const char *in, const char *out)
{
	struct list u;
	int i;
	FILE *f;

	memset(&u, 0, sizeof u);
	if (doc_read(in) < 0) {
		puts7(msg(17));
		puts7(in);
		printf("\n");
		return 1;
	}
	for (i = 0; i < ndl; i++) {
		int c = 0, n;

		if (dl[i].skip)
			continue;
		while (nextword(i, &c, &n)) {
			char w[MAXW];

			if (n < MAXW) {
				memcpy(w, dl[i].s + c, n);
				w[n] = 0;
				if (!correct(w) && list_has(&u, w) == 0)
					list_add(&u, w);
			}
			c += n;
		}
	}
	qsort(u.w, u.n, sizeof(char *), cmpstr);
	f = out && out[0] ? fopen(out, "w") : stdout;
	if (f == NULL)
		return 1;
	for (i = 0; i < u.n; i++)
		fprintf(f, "%s\n", u.w[i]);
	if (f != stdout)
		fclose(f);
	puts7(msg(15));
	printf("%d\n", u.n);
	return 0;
}

/* S: words spelt */
static void
spell(void)
{
	char b[MAXW];
	int k;

	for (;;) {
		puts7(msg(8));
		if (getline7(b, sizeof b) <= 0)
			return;
		if (correct(b)) {
			puts7(b);
			printf(": ");
			puts7(msg(9));
			printf("\n");
			continue;
		}
		suggest(b);
		puts7(nsug ? msg(2) : msg(3));
		for (k = 0; k < nsug; k++) {
			printf("  ");
			puts7(sug[k]);
		}
		printf("\n");
	}
}

/* M: words that fit a pattern */
static int
fits(const char *p, const char *w)
{
	if (*p == 0)
		return *w == 0;
	if (*p == '*') {
		for (;; w++) {
			if (fits(p + 1, w))
				return 1;
			if (!*w)
				return 0;
		}
	}
	if (!*w)
		return 0;
	if (*p == '?' || lower((unsigned char)*p) == lower((unsigned char)*w))
		return fits(p + 1, w + 1);
	return 0;
}

static int
more(int *shown)
{
	char b[8];

	if (++*shown % 20)
		return 1;
	puts7(msg(12));
	if (getline7(b, sizeof b) < 0 || b[0] == 'a' || b[0] == 'A' || b[0] == 'f' || b[0] == 'F' || b[0] == 'q' || b[0] == 'Q')
		return 0;
	return 1;
}

static void
pattern(void)
{
	char p[MAXW];
	long b;
	int i, shown;

	for (;;) {
		puts7(msg(10));
		if (getline7(p, sizeof p) <= 0)
			return;
		shown = 0;
		for (b = 0; b < nblocks; b++) {
			readblock(b);
			for (i = 0; i < ncw; i++)
				if ((cf[i] & 1) && fits(p, cw[i])) {
					puts7(cw[i]);
					printf("\n");
					if (!more(&shown))
						goto next;
				}
		}
next:		;
	}
}

/* L: the dictionary from a word on */
static void
listdict(void)
{
	char w[MAXW];
	long lo = 0, b;
	int i, shown = 0;

	puts7(msg(11));
	if (getline7(w, sizeof w) < 0)
		return;
	while (lo + 1 < nblocks && strcmp(bfirst[lo + 1], w) <= 0)
		lo++;
	for (b = lo; b < nblocks; b++) {
		readblock(b);
		for (i = 0; i < ncw; i++) {
			if (strcmp(cw[i], w) < 0 || !(cf[i] & 1))
				continue;
			puts7(cw[i]);
			printf("\n");
			if (!more(&shown))
				return;
		}
	}
}

/* W F X Y: the common list changed */
static void
change(int add, const char *file)
{
	char b[MAXW + 2];
	FILE *f = NULL;

	if (file && file[0] && (f = fopen(file, "r")) == NULL) {
		puts7(msg(17));
		puts7(file);
		printf("\n");
		return;
	}
	for (;;) {
		int n;

		if (f) {
			if (fgets(b + 1, MAXW, f) == NULL)
				break;
			n = (int)strlen(b + 1);
			while (n > 0 && (b[n] == '\n' || b[n] == '\r' || b[n] == ' '))
				b[n--] = 0;
		} else {
			puts7(msg(8));
			if ((n = getline7(b + 1, MAXW)) <= 0)
				break;
		}
		if (!b[1])
			continue;
		b[0] = '-';
		/* the word's other entry goes */
		{
			int i, j = 0;

			for (i = 0; i < common.n; i++)
				if (strcmp(common.w[i], add ? b : b + 1) == 0)
					free(common.w[i]);
				else
					common.w[j++] = common.w[i];
			common.n = j;
		}
		list_add(&common, add ? b + 1 : b);
		if (!f) {
			puts7(b + 1);
			printf(": ");
			puts7(msg(add ? 13 : 14));
			printf("\n");
		}
	}
	if (f)
		fclose(f);
	if (list_save(&common) < 0) {
		printf("lexspell: ");
		puts7(common.path);
		printf("?\n");
	}
}

/* a field of the record: characters from..from+31, without paint */
static void
field(char *out, const char *rec, int from)
{
	int n = 0, l = (int)strlen(rec), i;

	for (i = from; i < from + 32 && i < l; i++)
		out[n++] = rec[i] == '_' ? ' ' : rec[i];
	while (n > 0 && out[n - 1] == ' ')
		n--;
	out[n] = 0;
}

/* a name of a file: bare names of dictionaries are in LEXLIB, of the
   word lists in $HOME */
static void
where(char *out, const char *name, int dict)
{
	const char *home = getenv("HOME"), *lib = getenv("LEXLIB");

	if (lib == NULL || !*lib)
		lib = LEXLIB;
	if (dict && strcmp(name, "en") == 0)
		name = "en-GB";
	if (name[0] == '/' || name[0] == '.' || (!dict && !home))
		strcpy(out, name);
	else if (dict) {
		if (strchr(name, '.'))
			sprintf(out, "%s/%s", lib, name);
		else
			sprintf(out, "%s/%s.dict", lib, name);
	} else
		sprintf(out, "%s/%s", home, name);
}

int
main(int argc, char **argv)
{
	char dict[300], ul[300], cl[300], fn = 0, b[300], ksx[300];
	const char *rec = getenv("LEXREC"), *ks0 = getenv("LEXKS0"), *c = getenv("LEXCS");
	const char *in = "", *out = "";
	int i, r = 0;

	dict[0] = ul[0] = cl[0] = ksx[0] = 0;
	if (c)
		cs = strcmp(c, "swe7") == 0 ? 0 : strcmp(c, "utf8") == 0 ? 2 : 1;
	if (rec && rec[0]) {
		/* called by nylex: the record after its first 32 characters */
		field(b, rec, 0);
		where(dict, b, 1);
		field(b, rec, 32);
		where(ul, b[0] ? b : "user.def", 0);
		field(b, rec, 64);
		where(cl, b[0] ? b : "common.def", 0);
		if ((int)strlen(rec) > 128 && rec[128] != '_' && rec[128] != ' ')
			fn = rec[128];
		if (!fn && ks0)
			fn = ks0[0];
		if (ks0 && ks0[0] && ks0[1])
			strcpy(ksx, ks0 + 1);
		if (argc > 1)
			in = argv[1];
		if (argc > 2)
			out = argv[2];
	} else {
		for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
			const char *v = i + 1 < argc ? argv[i + 1] : "";

			switch (argv[i][1]) {
			case 'd': where(dict, v, 1); i++; break;
			case 'u': strcpy(ul, v); i++; break;
			case 'c': strcpy(cl, v); i++; break;
			case 'k': cs = strcmp(v, "swe7") == 0 ? 0 : strcmp(v, "utf8") == 0 ? 2 : 1; i++; break;
			default:
				fprintf(stderr, "usage: lexspell [-d dict] [-u list] [-c list] [-k swe7|latin1|utf8] C|A|E|S|M|L|W|F|X|Y [in [out]]\n");
				return 2;
			}
		}
		if (i < argc)
			fn = argv[i++][0];
		if (i < argc)
			in = argv[i++];
		if (i < argc)
			out = argv[i++];
		if (!dict[0])
			where(dict, swedish() ? "sv" : "en", 1);
		if (!ul[0])
			where(ul, "user.def", 0);
		if (!cl[0])
			where(cl, "common.def", 0);
	}
	if (fn >= 'a' && fn <= 'z')
		fn -= 32;
	if ((r = dict_open(dict)) < 0 && fn != 'W' && fn != 'F' && fn != 'X' && fn != 'Y') {
		printf("%s%s\n", msg(16), dict);
		return 1;
	}
	list_load(&user, ul);
	list_load(&common, cl);
	if (fn == 'A' && !ksx[0]) {
		printf("%s", lang[0] == 's' ? "Extra ordlista: " : "Extra word list: ");
		getline7(ksx, sizeof ksx);
	}
	if (fn == 'A' && ksx[0]) {
		where(b, ksx, 0);
		list_load(&extra, ksx[0] == '/' ? ksx : b);
	}
	r = 0;
	switch (fn) {
	case 'C':
	case 'A':
		r = check(in, out);
		break;
	case 'E':
		r = extract(in, out);
		break;
	case 'S':
		spell();
		break;
	case 'M':
		pattern();
		break;
	case 'L':
		listdict();
		break;
	case 'W':
		change(1, NULL);
		break;
	case 'F':
		change(1, in[0] ? in : out);
		break;
	case 'X':
		change(0, NULL);
		break;
	case 'Y':
		change(0, in[0] ? in : out);
		break;
	default:
		printf("%s%c\n", msg(20), fn ? fn : '?');
		r = 2;
	}
	if (list_save(&user) < 0) {
		printf("lexspell: ");
		puts7(user.path);
		printf("?\n");
	}
	fflush(stdout);
	return r;
}
