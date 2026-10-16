/*
 * term.h - terminal input and output for lex (a VT100/ANSI terminal).
 *
 * Text is kept as LEX keeps it: 7-bit, with the Swedish letters as the
 * ISO 646-SE codes  [ \ ] { | }  = Ä Ö Å ä ö å  (and @ ` = É é in some
 * profiles: not used).  The terminal may want them as 7-bit Swedish,
 * as Latin-1 or as UTF-8.
 */
#ifndef TERM_H
#define TERM_H

#define CS_SWE7		0	/* the terminal shows ISO 646-SE */
#define CS_LATIN1	1
#define CS_UTF8		2

/* attributes */
#define A_NORM		0
#define A_REV		1
#define A_BOLD		2
#define A_UL		4
#define A_DIM		8
#define A_BLINK		16

/* keys beyond the bytes 0-255 */
#define K_UP		0x101
#define K_DOWN		0x102
#define K_LEFT		0x103
#define K_RIGHT		0x104
#define K_HOME		0x105
#define K_END		0x106
#define K_PGUP		0x107
#define K_PGDN		0x108
#define K_DEL		0x109
#define K_INS		0x10a
#define K_F1		0x111	/* .. K_F1 + 11 */
#define K_NONE		(-1)

extern int	t_rows, t_cols, t_charset, t_national;

int	t_open(void);
void	t_close(void);
void	t_suspend(void);	/* back to the normal terminal, for a shell */
void	t_resume(void);
int	t_getkey(void);		/* waits */
int	t_keyready(void);
int	t_getbyte(int tenths);
void	t_unget(int c);
void	t_rawout(const char *s, int n);
void	t_move(int row, int col);
void	t_put(int row, int col, int c, int attr);	/* into the screen image */
void	t_puts(int row, int col, const char *s, int attr);
void	t_clearrow(int row);
void	t_clear(void);
void	t_refresh(void);	/* update the terminal */
void	t_redraw(void);		/* all of it again */
void	t_bell(void);
int	t_fromkbd(int c);	/* a typed character to LEX's 7-bit code */

#endif
