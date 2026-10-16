/*
 * lex.h - nylex, a word processor in the style of LEX (Ace Microsystems,
 * 1985; LEX-68 for DIAB's D-NIX machines), written from scratch for the
 * D-NIX Amiga port.  It works the way LEX's technical manual describes:
 * a screen editor driven by keystrokes, programs of "visible
 * equivalents" in keystores and in the records of a system file (.VMF),
 * menus, forms, list processing and printing all made of such records.
 */
#ifndef LEX_H
#define LEX_H

#include <stdio.h>
#include "vmf.h"
#include "term.h"

#define LEX_VERSION	"0.2"

#define MAXW		255	/* the widest row of text */
#define KEYLEN		10
#define MAXNAME		64
#define KSLEN		78	/* a keystore */
#define NKS		10
#define MAXMENU		6

/* keys from the input stream */
#define K_LIT		0x100	/* a character to type as text, never a command */
#define KC(k)		((k) & 0xff)
#define ESC		0x1b
#define CR		0x0d
#define LF		0x0a
#define TAB		0x09
#define BS		0x08
#define DEL		0x7f
#define CTL(c)		((c) & 0x1f)

/* the cells of a line: LEX's attributes 1-5 and two of the program's own */
#define AT_MASK		0x1f
#define AT_UNPROT	0x20	/* unprotected (protect mode) */
#define AT_BG		0x40	/* shown in background mode */

/* line flags */
#define LF_RULER	1
#define LF_CONT		2	/* continues on the next row ("&" shown) */

struct line {
	unsigned char	*s, *a;		/* text, attributes */
	int		len, cap;
	int		flags;
};

struct doc {
	struct line	*l;
	int		n, cap;
	int		changed;
};

struct ruler {
	char	s[MAXW + 1];
	int	len;
	int	left;		/* column of F, J or C, or -1 */
	int	right;		/* column of M, or -1 */
	int	kind;		/* 'F', 'J', 'C' or 0 */
	int	hyph;		/* column of H, or -1 */
};

/* doc.c */
struct doc	*doc_new(void);
void		doc_free(struct doc *d);
void		doc_clear(struct doc *d);
struct line	*doc_line(struct doc *d, int i);
int		doc_insline(struct doc *d, int at, const unsigned char *s, int len);
void		doc_delline(struct doc *d, int at);
void		line_set(struct line *l, const unsigned char *s, int len);
void		line_setattr(struct line *l, const unsigned char *s, const unsigned char *a, int len);
void		line_copy(struct line *to, const struct line *from);
void		line_free(struct line *l);
void		line_ins(struct line *l, int at, const unsigned char *s, int n, int attr);
void		line_del(struct line *l, int at, int n);
void		line_trim(struct line *l);
int		line_ch(const struct line *l, int at);
int		line_at(const struct line *l, int at);
void		line_put(struct line *l, int at, int c, int attr);
void		line_pad(struct line *l, int n);
int		isruler(const unsigned char *s, int len);
void		ruler_parse(struct ruler *r, const char *s, int len);
int		ruler_next(const struct ruler *r, int col, int insmode);
int		ruler_prev(const struct ruler *r, int col);
int		doc_load(struct doc *d, const char *path, int width);
int		doc_save(struct doc *d, const char *path, long *lines, long *words, long *chars);
void		doc_count(struct doc *d, long *lines, long *words, long *chars);

/* sys.c: the system control records */
struct mont {
	int	startkey, ret, endkey, left, tab, right;	/* field 1 */
	int	vstart, vesc, vend, vctl, lit, qmark, next;
	int	op_lt, op_eq, op_gt, op_has, pause, sel_l, sel_r;
	int	cont, menuexit;
	char	month[12][16];
	char	pre[5][3], suf[5][3];
	int	style;		/* 'M' American, 'Y' Swedish, else English */
	int	yes, no, query, from, to, odd, even;
	int	onespace, hyphlang;
	int	aust_on, aust_off, up, down, paint, sysret, g_or, g_and, g_col, attrterm;
	int	kill, timesep;
	char	shortdate[40];	/* delimiter + 36 */
	int	yearadd;
	char	vowels[32];
	int	case_lo[2], case_hi[2];
	char	stat[5];	/* P L A C */
	char	help[16];
};
struct inst {
	char	vdu[10], device[33], prtkey[11];
	int	worksize;
	char	defext[5], bakext[5], tmpext[5], vmfbak[5];
	char	pcc[13];			/* print control characters */
	int	multi, keeptmp, versions, freeform, fcase;
	int	titling, counts, detail, summary, helpcopy, helpsel;
	int	running, sure, showkeys, showpage, freecmp, showsel, nsel;
	int	delay, sleep, blockwarn;
	int	autoerr, statline, pagewarn;
	int	decpt, thou, neg, decs;
};
struct printer {
	char	type[9], petal[9];
	int	spool, pause, vdu, lastff;
	char	device[41];
};
struct pnst {
	char		prompt[160];
	struct printer	p[10];
	char		spooldev[17];
	int		pagestart;
	char		ndxext[5];
};
extern struct mont	M;
extern struct inst	I;
extern struct pnst	P;
extern char		montkey[11], instkey[11], pnstkey[11];
void		sys_load(void);
int		sys_switch(const char *key);
const char	*text(int rec, int n);		/* message n of **TEXT (0), **TEXT2 (1), **TEXTP (2) */
void		text_init(void);
int		is_yes(int c);
int		is_no(int c);
void		date_full(char *b);
void		date_short(char *b);
void		date_yymmdd(char *b);
void		date_ddmmmyy(char *b);
void		time_hms(char *b, int sep);
int		upcase(int c);
int		lowcase(int c);
int		sortval(int c);
int		convchar(int c);
void		tables_load(void);

/* the system file and foreign files */
extern struct vmf	*sysvmf;
extern char		vmfname[256];
struct vmf	*vmf_for(const char *key);		/* the file a key is read from */
unsigned char	*rec_get(const char *key, long *len);	/* raw (squashed) */
int		rec_text(const char *key, char **out, long *len, int sq);	/* unsquashed copy (free it) */
int		rec_put(const char *key, const unsigned char *data, long len);
void		mkkey(char *k, const char *name);
void		unsquash(const unsigned char *in, long n, char **out, long *outn, int sq);
void		squash(const char *in, long n, unsigned char **out, long *outn, int sq);

/* input.c: the keys from the terminal and from programs */
int		in_key(void);
int		in_peekterm(void);
void		in_prog(const char *text, long len, int sysrec);
void		in_raw(const char *s, long len);
void		in_lits(const char *s, long len);
void		in_kbd(const char *s, long len);
void		in_repeat(const char *body, long len, long n, int cond, const char *test);
void		in_stoprepeat(void);
void		in_typeahead(const char *s);
void		in_abort(void);
int		in_prognow(void);
int		in_depth(void);
void		in_record(int k);
int		in_lastkeys(char *buf, int max);
void		in_clearlast(void);
int		in_paused(void);
void		vis_of(int k, char *out);		/* a key as visible equivalents */
extern int	in_pausing;

/* vdu.c: terminal records */
extern char	vdukey[11];
int		vdu_load(const char *key);
int		vdu_key(void);
void		vdu_seq4(int n);
void		vdu_seq2(int n);
void		vdu_send(const char *s, int n);
int		vdu_sgr(const char *seq, int len);
void		vdu_menuattr(int n, int *attr);
const char	*vdu_pport(int on);
int		vdu_attr(int n);
extern int	vdu_bgattr, vdu_fgattr;
extern int	vdu_special;		/* the key was an arrow, a function key ... */
extern char	vdu_firstmenu[11];

/* ui.c: messages, prompts, quiet modes */
extern int	shush, shutup;
extern int	errcode[2];			/* the last error (two letters) */
extern int	showerr;
void		bell(void);
void		error(const char *code);
const char	*errtext(const char *code);
void		ui_refresh(void);
int		ui_quiet(void);
int		ui_line(int row, const char *prompt, char *buf, int max, int width, int *term);
int		ui_yesno(int row, int col, const char *q);
void		ui_center(int row, const char *s, int attr);
void		ui_text(int row, int col, const char *s, int attr);
int		ui_waitkey(void);
void		ui_pause(const char *msg);

/* edit.c: the editor */
#define ED_PLAY		0	/* the playground */
#define ED_EDIT		1
#define ED_VIEW		2
#define ED_NEW		3
#define ED_FIRST	4	/* /I: edit, the first line only */
struct edstate {
	struct doc	*d;
	int		cl, cc, top;
	int		mode;
	int		ins, vert, protect, bgmode, ignrul, attr, sqch;
	char		path[256], name[MAXNAME + 1], ext[8], detext[8];
	char		rulers[10][MAXW + 1];
	int		useover;
	struct ruler	over;
	int		markl, markc;
	int		posl[10], posc[10];
	int		statusline, pagewarn;
	int		rows;		/* rows for text */
	int		dirty;		/* the screen needs drawing */
	int		exitcode;	/* what ended the session */
	int		calc;		/* in the calculator */
	long		started;
	int		mask;		/* a mask is set */
	int		foreign;
	int		insert_on_start;
};
extern struct edstate	ED;
extern char		keystore[NKS][KSLEN + 1];
#define EX_NONE		0
#define EX_EXIT		1	/* ESC E */
#define EX_ABANDON	2	/* ESC Z */
#define EX_REEDIT	3	/* ESC V */
#define EX_REEDITPOS	4	/* ESC QX */
#define EX_MENU		5	/* a program went to a menu */
int		edit_run(int mode, const char *path, const char *name, const char *rec);
void		ed_draw(void);
void		ed_typechar(int c, int lit);
void		ed_typestr(const char *s);
void		ed_typelit(const char *s);
void		ed_cr(int margin);
struct line	*ed_line(int i);
void		ed_ruler(struct ruler *r);
void		ed_rulerat(int line, struct ruler *r);
void		ed_markrulers(void);
void		ed_fix(void);
int		ed_esc(void);
int		ed_ctl(int k);
int		ed_unprot(int l, int c);
int		ed_nextunprot(int *l, int *c, int dir);
void		ed_setprotect(int on);
void		ed_clearscreen(void);
int		ed_justify(int from_here);
void		ed_tab(int back);
void		ed_wrap(int i);
void		ed_wordfwd(void);
void		ed_wordback(void);
void		ed_status(char *b);
int		ed_screenrow(int line);
void		ed_load_details(const char *ext);
void		ed_paginate(void);
int		ed_pagecheck(void);
int		ed_parastart(int i);
int		ed_paraend(int i);
void		ed_message(const char *s);
int		ed_matches(const char *s);
int		hyph_point(const unsigned char *w, int len, int room);
extern long	ED_lines, ED_words, ED_chars;
extern int	pd_lm, pd_alt, pd_widow, pd_orphan, pd_lpp, pd_total, pd_foot;
extern char	curopt[16];

/* esc2.c: the rest of the escape sequences */
int		esc_record(int k, int excl);
int		esc_cond(void);
int		esc_repeat(void);
int		esc_merge(int c);
int		esc_extract(int c);
const char	*esc_mergename(void);
void		esc_closefiles(void);
int		esc_sortk(int a, int b);
int		esc_open(void);
int		esc_sort(void);
int		esc_attr(int k);
int		esc_search(int k, int back);
int		esc_read(int which);
void		perform(const char *key, int partial);
int		read_string(const char *prompt, char *buf, int max, int *term);
void		esc_setks(int n);
void		esc_clearfuncs(void);
extern char	funcstr[4][KSLEN + 1];	/* find, get, repeat, merge strings */

/* cut.c: cut and paste, delete buffers */
void		cut_mark(void);
int		cut_block(int buf, int how);
int		cut_sentence(int buf, int how);
int		paste(int buf, int how);
int		paste_blanks(int how);
void		cut_clearall(void);
int		cut_used(char *list);
void		undel_char(int c);
void		undel_word(const unsigned char *s, int n);
void		undel_line(const struct line *l);
void		undel_putchar(void);
void		undel_putword(void);
void		undel_putline(void);

/* calc.c: the calculator */
extern double	calcmem[36];
void		calc_run(int field);
void		calc_clearall(void);
int		calc_used(char *list);
double		calc_get(int c);

/* rec.c: records, forms, masks, foreign files */
int		key_find(int *line, int *col);
int		rec_read(int line, int col, int lock);
int		rec_write(int line, int col, int how, int excl);
int		rec_nextkey(int dir, int deleted);
int		mask_set(const char *name);
void		mask_unset(void);
int		foreign_set(const char *spec);
void		foreign_unset(void);
int		foreign_desc(char *b);
int		foreign_has(const char *key);
void		rec_lastkey(const char *key);

/* menu.c */
extern char	menustack[MAXMENU][KEYLEN + 1];
extern int	menudepth;
extern char	lastdoc[MAXNAME + 1];
extern int	quitting;
extern char	curopt[16];
void		menu_loop(void);
int		switch_option(int opt, const char *arg, const char *rec);
void		menu_goto(const char *key);
int		fork_prog(const char *cmd, int wait);

/* reg.c: the document index */
struct regent {
	char	name[41], title[41], detail[41];
	char	created[8], accessed[8], edited[8], printed[8];
	int	nacc, nedit, nprint;
	char	last[7];
	long	tottime;
	long	lines, words, chars;
	char	dcreated[7], daccessed[7], dedited[7], dprinted[7];
};
void		docname_norm(char *name, const char *defext);
void		docname_file(char *path, const char *name);
void		docname_ext(const char *name, char *ext);
void		reg_key(char *key, const char *name);
int		reg_get(const char *name, struct regent *r);
int		reg_put(const struct regent *r);
int		reg_delete(const char *name);
void		reg_new(struct regent *r, const char *name);
void		reg_touch(struct regent *r, int what, long secs);

/* listp.c: list processing and mailshot */
int		listproc(const char *fmtkey, int mailshot, const char *docname);

/* print.c */
int		print_doc(const char *path, int copies, int ask);
int		print_open(int *pr, FILE **fp, int *ispipe);
void		print_close(int pr, FILE *f, int ispipe);
int		ask_printer(int *pr);
void		print_begin(void);
void		print_pcc(const char *pcc);
void		print_spacing(int quarters);
void		print_text(const char *s, int lm);
void		print_docpart(struct doc *d);
void		print_ff(int last);
void		print_end(void);
int		print_pausing(void);
void		print_setpause(int on);
int		print_stopped(void);
extern int	print_quiet;

/* pdf.c: the PDF printers */
struct pdfhint {
	int	just;			/* a justified line: spread to the margin */
	int	left, right;		/* the ruler's F/J/C and M (with the margin; M
					   is the column after the text) */
	int	kind;			/* the ruler's kind */
};
void		pdf_begin(FILE *f, int family, int ps);
void		pdf_put(int col, int c, int attr, int sup);
int		pdf_col(void);
void		pdf_newline(int quarters, const struct pdfhint *h);
void		pdf_overprint(const struct pdfhint *h);
void		pdf_endpage(void);
int		pdf_inpage(void);
void		pdf_finish(void);
void		daisy_begin(const char *tab, long len, void (*bytes)(const char *, int), void (*attr)(int), void (*chr)(int));
void		daisy_flush(const struct pdfhint *h);
void		daisy_end(void);

/* work.c: the work file */
extern char	workname[256];
int		work_load(void);
int		work_save(void);
void		work_init(void);

/* main.c */
void		fatal(const char *msg);
char		*xstrdup(const char *s);
int		fexists(const char *path);
int		copyfile(const char *from, const char *to);
extern int	demo;

#endif
