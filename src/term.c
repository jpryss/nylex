/*
 * term.c - terminal input and output for lex: a VT100/ANSI terminal,
 * driven through a screen image so that only changes are sent (a 9600
 * baud line is slow).  Builds with termios (Linux, BSD, macOS) or with
 * System V termio (D-NIX: cc -DUSE_TERMIO).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#ifdef USE_TERMIO
#include <termio.h>
#else
#include <termios.h>
#include <sys/ioctl.h>
#endif
#include "term.h"

int t_rows = 24, t_cols = 80, t_charset = CS_UTF8;
int t_national = 1;	/* [ \\ ] { | } are Swedish letters (**MONT) */

#define MAXROWS	100
#define MAXCOLS	256

static unsigned short	*vscr, *pscr;	/* virtual, physical: char | attr << 8 */
static int		cur_r, cur_c;	/* cursor wanted */
static int		phys_r = -1, phys_c = -1, phys_a = -1;
static char		obuf[4096];
static int		olen;
static int		opened;
static int		pend = -1;	/* a byte read ahead */

#ifdef USE_TERMIO
static struct termio	saved, raw;
#define GETTERM(t)	ioctl(0, TCGETA, t)
#define SETTERM(t)	ioctl(0, TCSETAW, t)
#else
static struct termios	saved, raw;
#define GETTERM(t)	tcgetattr(0, t)
#define SETTERM(t)	tcsetattr(0, TCSADRAIN, t)
#endif

static void
oflush(void)
{
	int n = 0, w;

	while (n < olen) {
		w = write(1, obuf + n, olen - n);
		if (w <= 0)
			break;
		n += w;
	}
	olen = 0;
}

static void
oputc(int c)
{
	if (olen == sizeof obuf)
		oflush();
	obuf[olen++] = c;
}

static void
oputs(const char *s)
{
	while (*s)
		oputc(*s++);
}

/* LEX's 7-bit Swedish letters and their Latin-1 forms */
static const char swe7[] = "[\\]{|}";
static const unsigned char lat1[] = { 0xc4, 0xd6, 0xc5, 0xe4, 0xf6, 0xe5 };

static void
outchar(int c)
{
	const char *p;

	c &= 0xff;
	if (!t_national && c >= 0xa0) {
		/* English text may hold Latin-1 letters */
		if (t_charset == CS_UTF8) {
			oputc(0xc0 | (c >> 6));
			oputc(0x80 | (c & 0x3f));
		} else
			oputc(t_charset == CS_SWE7 ? '?' : c);
		return;
	}
	if (t_national && t_charset != CS_SWE7 && c < 0x80 && (p = strchr(swe7, c)) != NULL && c) {
		c = lat1[p - swe7];
		if (t_charset == CS_UTF8) {
			oputc(0xc0 | (c >> 6));
			oputc(0x80 | (c & 0x3f));
		} else
			oputc(c);
		return;
	}
	if (c < ' ' || c == 0x7f)
		c = '.';
	oputc(c);
}

/* A typed character (Latin-1 code) to LEX's code. */
int
t_fromkbd(int c)
{
	int i;

	if (t_charset == CS_SWE7 || !t_national)
		return c;
	for (i = 0; i < 6; i++)
		if (c == lat1[i])
			return swe7[i];
	return c;
}

static void
setraw(void)
{
	raw = saved;
	raw.c_iflag &= ~(ICRNL | INLCR | IGNCR | ISTRIP | IXOFF);
	raw.c_iflag &= ~IXON;	/* Ctrl-S and Ctrl-Q are LEX's */
	raw.c_oflag &= ~OPOST;
	raw.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ISIG);
	raw.c_cflag |= CS8;
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	SETTERM(&raw);
}

static void
settime(int vmin, int tenths)
{
	raw.c_cc[VMIN] = vmin;
	raw.c_cc[VTIME] = tenths;
	SETTERM(&raw);
}

static void
getsize(void)
{
	char *s;
#ifdef TIOCGWINSZ
	struct winsize ws;

	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
		t_rows = ws.ws_row;
		t_cols = ws.ws_col;
	}
#endif
	if ((s = getenv("LINES")) != NULL && atoi(s) > 0)
		t_rows = atoi(s);
	if ((s = getenv("COLUMNS")) != NULL && atoi(s) > 0)
		t_cols = atoi(s);
	if (t_rows > MAXROWS)
		t_rows = MAXROWS;
	if (t_cols > MAXCOLS)
		t_cols = MAXCOLS;
	if (t_rows < 12)
		t_rows = 12;
	if (t_cols < 40)
		t_cols = 40;
}

int
t_open(void)
{
	if (GETTERM(&saved) < 0)
		return -1;
	getsize();
	vscr = (unsigned short *)calloc(MAXROWS * MAXCOLS, sizeof *vscr);
	pscr = (unsigned short *)calloc(MAXROWS * MAXCOLS, sizeof *pscr);
	if (vscr == NULL || pscr == NULL)
		return -1;
	setraw();
	opened = 1;
	if (t_charset == CS_SWE7)
		oputs("\033(7");
	t_clear();
	t_redraw();
	return 0;
}

void
t_suspend(void)
{
	oputs("\033[m");
	if (t_charset == CS_SWE7)
		oputs("\033(B");
	t_move(t_rows - 1, 0);
	phys_r = phys_c = phys_a = -1;
	{
		char b[16];

		sprintf(b, "\033[%d;1H\r\n", t_rows);
		oputs(b);
	}
	oflush();
	SETTERM(&saved);
}

void
t_resume(void)
{
	setraw();
	if (t_charset == CS_SWE7)
		oputs("\033(7");
	t_redraw();
}

void
t_close(void)
{
	if (!opened)
		return;
	t_suspend();
	opened = 0;
}

static int
rdbyte(void)
{
	unsigned char c;

	if (pend >= 0) {
		c = pend;
		pend = -1;
		return c;
	}
	if (read(0, &c, 1) != 1)
		return -1;
	return c;
}

/* a byte if one comes within tenths/10 s */
static int
rdbyte_t(int tenths)
{
	unsigned char c;
	int n;

	if (pend >= 0)
		return rdbyte();
	settime(0, tenths);		/* tenths 0: only what is there */
	n = read(0, &c, 1);
	settime(1, 0);
	return n == 1 ? c : -1;
}

/* A byte from the keyboard: wait for it (tenths < 0) or up to tenths/10 s. */
int
t_getbyte(int tenths)
{
	if (tenths < 0)
		return rdbyte();
	return rdbyte_t(tenths);
}

int
t_keyready(void)
{
	if (pend < 0)
		pend = rdbyte_t(0) ;
	return pend >= 0;
}

int
t_getkey(void)
{
	int c, d, e;

	t_refresh();
	c = rdbyte();
	if (c < 0)
		return K_NONE;
	if (c == 0x1b) {
		/* an arrow or function key sends ESC [ x or ESC O x at once */
		d = rdbyte_t(1);
		if (d < 0)
			return 0x1b;
		if (d != '[' && d != 'O') {
			pend = d;
			return 0x1b;
		}
		e = rdbyte_t(1);
		if (e < 0) {
			pend = d;	/* ESC [ typed: LEX's ESC [ */
			return 0x1b;
		}
		switch (e) {
		case 'A': return K_UP;
		case 'B': return K_DOWN;
		case 'C': return K_RIGHT;
		case 'D': return K_LEFT;
		case 'H': return K_HOME;
		case 'F': return K_END;
		case 'P': return K_F1;
		case 'Q': return K_F1 + 1;
		case 'R': return K_F1 + 2;
		case 'S': return K_F1 + 3;
		}
		if (e >= '0' && e <= '9') {	/* ESC [ n ~ */
			int n = e - '0';

			while ((e = rdbyte_t(1)) >= '0' && e <= '9')
				n = n * 10 + e - '0';
			switch (n) {
			case 1: case 7: return K_HOME;
			case 2: return K_INS;
			case 3: return K_DEL;
			case 4: case 8: return K_END;
			case 5: return K_PGUP;
			case 6: return K_PGDN;
			case 11: case 12: case 13: case 14: case 15:
				return K_F1 + n - 11;
			case 17: case 18: case 19: case 20: case 21:
				return K_F1 + n - 12;
			case 23: case 24:
				return K_F1 + n - 13;
			}
			return K_NONE;
		}
		return K_NONE;
	}
	if (c >= 0x80 && t_charset == CS_UTF8) {
		/* UTF-8: two bytes for Latin-1; others are dropped */
		int n = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
		int u = c & (0x3f >> n);

		while (n-- > 0) {
			d = rdbyte();
			if (d < 0)
				return K_NONE;
			u = (u << 6) | (d & 0x3f);
		}
		if (u > 0xff)
			return K_NONE;
		c = u;
	}
	if (c >= 0x80)
		return t_fromkbd(c);
	return c;
}

void
t_move(int row, int col)
{
	cur_r = row;
	cur_c = col;
}

void
t_put(int row, int col, int c, int attr)
{
	if (row < 0 || row >= t_rows || col < 0 || col >= t_cols)
		return;
	vscr[row * MAXCOLS + col] = (c & 0xff) | (attr << 8);
}

void
t_puts(int row, int col, const char *s, int attr)
{
	while (*s)
		t_put(row, col++, (unsigned char)*s++, attr);
}

void
t_clearrow(int row)
{
	int c;

	for (c = 0; c < t_cols; c++)
		t_put(row, c, ' ', A_NORM);
}

void
t_clear(void)
{
	int r;

	for (r = 0; r < t_rows; r++)
		t_clearrow(r);
}

static void
phmove(int r, int c)
{
	char b[40];

	if (r == phys_r && c == phys_c)
		return;
	if (r == phys_r && c > phys_c && c - phys_c <= 4) {
		sprintf(b, "\033[%dC", c - phys_c);
		oputs(b);
	} else if (c == 0 && r == phys_r + 1) {
		oputs("\r\n");
	} else if (c == 0 && r == phys_r) {
		oputc('\r');
	} else {
		sprintf(b, "\033[%d;%dH", r + 1, c + 1);
		oputs(b);
	}
	phys_r = r;
	phys_c = c;
}

static void
phattr(int a)
{
	if (a == phys_a)
		return;
	oputs("\033[0");
	if (a & A_BOLD)
		oputs(";1");
	if (a & A_UL)
		oputs(";4");
	if (a & A_REV)
		oputs(";7");
	if (a & A_DIM)
		oputs(";2");
	if (a & A_BLINK)
		oputs(";5");
	oputc('m');
	phys_a = a;
}

void
t_refresh(void)
{
	int r, c, last, blank;
	unsigned short *v, *p;

	for (r = 0; r < t_rows; r++) {
		v = vscr + r * MAXCOLS;
		p = pscr + r * MAXCOLS;
		/* the last non-blank place of the new row */
		for (last = t_cols - 1; last >= 0 && v[last] == ' '; last--)
			;
		for (c = 0; c < t_cols; c++) {
			if (v[c] == p[c])
				continue;
			if (c > last) {
				/* the rest of the row is blank now */
				for (blank = c; blank < t_cols && p[blank] == ' '; blank++)
					;
				if (blank == t_cols)
					break;
				phmove(r, c);
				phattr(A_NORM);
				oputs("\033[K");
				for (; c < t_cols; c++)
					p[c] = ' ';
				break;
			}
			if (r == t_rows - 1 && c == t_cols - 1)
				continue;	/* do not scroll the screen */
			/* a short gap on the row: the characters are cheaper
			   than a cursor movement (and text stays text) */
			if (r == phys_r && c > phys_c && c - phys_c <= 4) {
				int g;

				for (g = phys_c; g < c && (v[g] >> 8) == phys_a; g++)
					;
				if (g == c)
					for (g = phys_c; g < c; g++) {
						outchar(v[g] & 0xff);
						p[g] = v[g];
						phys_c++;
					}
			}
			phmove(r, c);
			phattr(v[c] >> 8);
			outchar(v[c] & 0xff);
			p[c] = v[c];
			phys_c++;
			if (phys_c >= t_cols) {
				phys_r = phys_c = -1;	/* the wrap is not certain */
			}
		}
	}
	phattr(A_NORM);
	phmove(cur_r, cur_c);
	oflush();
}

void
t_redraw(void)
{
	int i;

	for (i = 0; i < MAXROWS * MAXCOLS; i++)
		pscr[i] = 0xffff;
	oputs("\033[m\033[H\033[2J");
	for (i = 0; i < MAXROWS * MAXCOLS; i++)
		pscr[i] = ' ';
	phys_r = phys_c = 0;
	phys_a = A_NORM;
	t_refresh();
}

void
t_bell(void)
{
	oputc(7);
	oflush();
}

/* Bytes sent to the terminal as they are (ESC SS, ESC ST, ESC SV). */
void
t_rawout(const char *s, int n)
{
	t_refresh();
	while (n-- > 0)
		oputc(*s++);
	oflush();
	phys_r = phys_c = phys_a = -1;
}

void
t_unget(int c)
{
	pend = c;
}
