/*
 * input.c - where keys come from: the terminal, and programs (keystores
 * and records, manual chapter 11) whose visible equivalents become the
 * keys they stand for.  Programs nest; a program may pause ("?" or the
 * pause character of **MONT) for the user to type until Return; a
 * repeat (ESC X) feeds its string again and again.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

#define MAXSRC	48
#define HIST	256

#define S_PROG	0
#define S_RAW	1	/* keys as they are (function keys, ESC =) */
#define S_LIT	2	/* text to type (partial record reads) */
#define S_REP	3	/* a repeat */
#define S_KBD	4	/* keys a function key stands for */

struct src {
	int	type;
	char	*text;
	long	len, pos;
	int	sysrec;
	int	lit;		/* in a literal string */
	int	comment;
	int	aust;		/* Australian mode: programming on */
	long	count;		/* repeats left (S_REP); -1 for ever */
	int	cond;		/* 'W' while, 'U' until, 0 */
	char	*test;		/* the string tested */
	long	done;
};

static struct src	src[MAXSRC];
static int		nsrc;
static int		hist[HIST];
static int		nhist;
static int		fromprog;
static int		pausedepth;	/* the program that paused: nsrc then */
static long		progkeys;
int			in_pausing;

extern int	ed_matches(const char *s);

static void
pop(void)
{
	if (nsrc <= 0)
		return;
	nsrc--;
	xfree(src[nsrc].text);
	xfree(src[nsrc].test);
	src[nsrc].text = src[nsrc].test = NULL;
	if (pausedepth > nsrc) {
		pausedepth = 0;
		in_pausing = 0;
	}
}

static struct src *
push(int type, const char *text, long len)
{
	struct src *s;

	if (nsrc >= MAXSRC) {
		error("XX");
		return NULL;
	}
	s = &src[nsrc++];
	memset(s, 0, sizeof *s);
	s->type = type;
	s->text = (char *)xrealloc(NULL, len + 1);
	memcpy(s->text, text, len);
	s->text[len] = 0;
	s->len = len;
	s->aust = 1;
	return s;
}

void
in_prog(const char *text, long len, int sysrec)
{
	struct src *s = push(S_PROG, text, len);

	if (s) {
		s->sysrec = sysrec;
		s->aust = M.aust_on ? 0 : 1;
	}
}

void
in_raw(const char *s, long len)
{
	push(S_RAW, s, len);
}

void
in_kbd(const char *s, long len)
{
	push(S_KBD, s, len);
}

void
in_lits(const char *s, long len)
{
	push(S_LIT, s, len);
}

/* A repeat: body n times (n 0: up to 9999, until the end), or while/until. */
void
in_repeat(const char *body, long len, long n, int cond, const char *test)
{
	struct src *s = push(S_REP, body, len);

	if (s == NULL)
		return;
	s->count = n <= 0 ? 9999 : n;
	s->cond = cond;
	if (test) {
		s->test = (char *)xrealloc(NULL, strlen(test) + 1);
		strcpy(s->test, test);
	}
	s->pos = 0;
	s->done = 0;
}

/* The repeats stop (the end of the document, no more keys). */
void
in_stoprepeat(void)
{
	int i;

	for (i = nsrc - 1; i >= 0; i--)
		if (src[i].type == S_REP)
			break;
	if (i < 0)
		return;
	while (nsrc > i)
		pop();
}

void
in_typeahead(const char *s)
{
	/* typed ahead: below any program, read before the terminal */
	if (nsrc < MAXSRC) {
		memmove(src + 1, src, nsrc * sizeof(struct src));
		nsrc++;
		memset(&src[0], 0, sizeof src[0]);
		src[0].type = S_PROG;
		src[0].len = (long)strlen(s);
		src[0].text = (char *)xrealloc(NULL, src[0].len + 1);
		strcpy(src[0].text, s);
		src[0].aust = 1;
		if (pausedepth)
			pausedepth++;
	}
}

void
in_abort(void)
{
	while (nsrc > 0)
		pop();
	pausedepth = 0;
	in_pausing = 0;
}

int
in_prognow(void)
{
	return fromprog;
}

int
in_depth(void)
{
	return in_pausing ? 0 : nsrc;
}

int
in_paused(void)
{
	return in_pausing;
}

/* the keys typed, for ESC RS and ESC Rn */
void
in_record(int k)
{
	if (nhist == HIST) {
		memmove(hist, hist + 1, (HIST - 1) * sizeof(int));
		nhist--;
	}
	hist[nhist++] = k;
}

void
in_clearlast(void)
{
	nhist = 0;
}

/* A key as text in a program: its visible equivalent. */
void
vis_of(int k, char *out)
{
	int c = KC(k);

	out[0] = out[1] = out[2] = 0;
	if (k & K_LIT) {
		out[0] = (char)c;
		return;
	}
	if (c == ESC)
		out[0] = (char)M.vesc;
	else if (c == CR)
		out[0] = (char)M.ret;
	else if (c == TAB && M.tab)
		out[0] = (char)M.tab;
	else if (c == CTL('H') && M.left)
		out[0] = (char)M.left;
	else if (c == CTL('R') && M.right)
		out[0] = (char)M.right;
	else if (c == M.endkey)
		out[0] = (char)M.vend;
	else if (c < 32) {
		out[0] = (char)M.vctl;
		out[1] = (char)(c + 64);
	} else if (c == M.vesc || c == M.vctl || c == M.ret || c == M.lit || c == M.next ||
	    c == M.vstart || c == M.vend || c == '?' || (M.pause && c == M.pause)) {
		out[0] = (char)M.next;
		out[1] = (char)c;
	} else
		out[0] = (char)c;
}

/* the last keys typed, as a program of at most max characters */
int
in_lastkeys(char *buf, int max)
{
	char tmp[HIST * 3 + 1], v[4];
	int i, n = 0, start;

	for (i = 0; i < nhist; i++) {
		vis_of(hist[i], v);
		strcpy(tmp + n, v);
		n += (int)strlen(v);
	}
	start = n > max ? n - max : 0;
	strcpy(buf, tmp + start);
	return n - start;
}

static int
isvisible(struct src *s, int c)
{
	return c == M.vesc || c == M.vctl || c == M.ret || c == M.vend || c == M.vstart ||
	    (s->sysrec && M.sysret && c == M.sysret) ||
	    (M.left && c == M.left) || (M.tab && c == M.tab) || (M.right && c == M.right) ||
	    (M.up && c == M.up) || (M.down && c == M.down);
}

/*
 * The next key of a program: -2 if it has none left, -3 to look again
 * (it paused, or skipped something).
 */
static int
progkey(struct src *s)
{
	int c, d;

	for (;;) {
		if (s->pos >= s->len)
			return -2;
		c = (unsigned char)s->text[s->pos++];
		d = s->pos < s->len ? (unsigned char)s->text[s->pos] : -1;
		if (s->type == S_LIT)
			return c == '\n' ? CR : (c | K_LIT);
		if (c == '\n') {
			if (s->comment)
				continue;
			return CR;
		}
		if (s->comment) {
			if (c == M.lit && d == M.lit) {
				s->pos++;
				s->comment = 0;
				if (s->pos < s->len && s->text[s->pos] == '\n')
					s->pos++;
			}
			continue;
		}
		if (M.aust_on) {
			if (!s->aust) {
				if (c == M.aust_on) {
					s->aust = 1;
					continue;
				}
				return c | K_LIT;
			}
			if (c == M.aust_off) {
				s->aust = 0;
				continue;
			}
		}
		if (s->lit) {
			if (c == M.lit) {
				s->lit = 0;
				continue;
			}
			return c | K_LIT;
		}
		if (c == M.lit) {
			if (d == M.lit) {	/* "" starts a comment */
				s->pos++;
				s->comment = 1;
				continue;
			}
			s->lit = 1;
			continue;
		}
		if (c == M.next) {
			if (d < 0)
				return c | K_LIT;
			s->pos++;
			return d == '\n' ? CR : (d | K_LIT);
		}
		if (c == M.vesc)
			return ESC;
		if (c == M.vctl && d >= 0 && d != '\n') {
			s->pos++;
			if (d == '?')
				return DEL;
			return CTL(d);
		}
		if (c == M.ret || (s->sysrec && M.sysret && c == M.sysret))
			return CR;
		if (c == M.vstart)
			return M.startkey | K_LIT;
		if (c == M.vend)
			return M.endkey;
		if (M.left && c == M.left)
			return CTL('H');
		if (M.tab && c == M.tab)
			return TAB;
		if (M.right && c == M.right)
			return CTL('R');
		if (M.up && c == M.up)
			return CTL('U');
		if (M.down && c == M.down)
			return CTL('J');
		if (c == '?' || (M.pause && c == M.pause)) {
			/* a pause: the user types until Return */
			pausedepth = nsrc;
			in_pausing = 1;
			return -3;
		}
		(void)isvisible;
		return c;
	}
}

static int
termkey(void)
{
	int k;

	if (getenv("LEXTRACE")) {
		FILE *f = fopen(getenv("LEXTRACE"), "a");

		if (f) {
			fprintf(f, "termkey quiet=%d shutup=%d shush=%d pausing=%d nsrc=%d\n", ui_quiet(), shutup, shush, in_pausing, nsrc);
			fclose(f);
		}
	}
	if (!ui_quiet() || in_pausing)
		ui_refresh();
	k = vdu_key();
	return k;
}

int
in_key(void)
{
	int k;

	vdu_special = 0;
	for (;;) {
		if (nsrc > 0 && src[nsrc - 1].type == S_KBD) {
			struct src *s = &src[nsrc - 1];

			if (s->pos >= s->len) {
				pop();
				continue;
			}
			k = (unsigned char)s->text[s->pos++];
			if (s->pos >= s->len)
				pop();
			fromprog = 0;
			if (in_pausing && k == CR) {
				in_pausing = 0;
				pausedepth = 0;
				continue;
			}
			in_record(k);
			return k;
		}
		if (in_pausing && pausedepth > 0 && pausedepth <= nsrc) {
			/* typed by the user while a program waits */
			k = termkey();
			if (k == -2)
				continue;
			fromprog = 0;
			if (k == K_NONE)
				return k;
			if (k == CTL('C')) {
				in_abort();
				return k;
			}
			if (k == CR) {
				in_pausing = 0;
				pausedepth = 0;
				continue;
			}
			in_record(k);
			return k;
		}
		if (nsrc > 0) {
			struct src *s = &src[nsrc - 1];

			if (s->type == S_RAW) {
				if (s->pos >= s->len) {
					pop();
					continue;
				}
				fromprog = 1;
				return (unsigned char)s->text[s->pos++];
			}
			if (s->type == S_REP && s->pos == 0) {
				/* the start of a round: is there one? */
				if (s->count <= 0 || (s->cond && s->test &&
				    (ed_matches(s->test) ? s->cond == 'U' : s->cond == 'W'))) {
					pop();
					continue;
				}
			}
			k = progkey(s);
			if (k == -3)
				continue;
			if (k == -2) {
				if (s->type == S_REP) {
					s->count--;
					s->pos = 0;
					s->lit = s->comment = 0;
					if (s->count > 0 && ++progkeys % 16 == 0 && t_keyready() && in_peekterm() == CTL('C')) {
						in_abort();
						return CTL('C');
					}
					continue;
				}
				pop();
				continue;
			}
			fromprog = 1;
			if (++progkeys % 256 == 0 && t_keyready() && in_peekterm() == CTL('C')) {
				in_abort();
				return CTL('C');
			}
			return k;
		}
		fromprog = 0;
		k = termkey();
		if (k == -2)
			continue;
		if (k != K_NONE)
			in_record(k);
		return k;
	}
}

/* a key waiting at the terminal (read; it is kept if it is not ^C) */
int
in_peekterm(void)
{
	int c = t_getbyte(0);

	if (c < 0)
		return -1;
	if (c == CTL('C'))
		return c;
	t_unget(c);
	return c;
}
