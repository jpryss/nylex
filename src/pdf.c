/*
 * pdf.c - printing to a PDF file: the printer types PDF (Times),
 * PDFSANS (Helvetica) and PDFMONO (Courier, LEX's own 10 characters to
 * the inch) of **PNST.  print.c gives the characters of a line with
 * their columns and attributes; here they are set in a proportional
 * font, as a daisy-wheel printer with a proportional wheel set them:
 *
 *   - the text of a justified paragraph (a J ruler, the line reaching
 *     its M and the paragraph going on) is spread to the margin;
 *   - otherwise each piece of a line (words with single spaces between
 *     them) starts at its column: tables and tab stops stay where they
 *     were.  The columns are 12 to the inch (Times 13 point, Helvetica
 *     12), which the text in those fonts about fills; Courier is 12
 *     point at 10 to the inch, LEX's own spacing.  80 columns are put
 *     in the middle of the page;
 *   - a piece that is a number ends at its column (the decimal points
 *     stay in line); a line centred or put against M stays so.
 *
 * Attributes: 1 and 4 underline, 2 and 5 bold, 3 italic; footnote
 * numbers are raised.  The page is A4 ($LEXPAPER=letter: US Letter),
 * 6 lines to the inch; $LEXPDF="grid,size" sets the column and the
 * font's size in points.  The fonts are the standard ones every PDF
 * reader has; the text is ISO 8859-1 (WinAnsiEncoding), the Swedish
 * letters of 7-bit documents translated.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"
#include "term.h"
#include "pdffont.h"

#define MAXC	512
#define LINEQ	3.0		/* a quarter line: 1/24 inch */

static FILE	*pdff;
static long	pos;			/* bytes written */
static long	*objoff;		/* the offsets of the objects */
static int	nobj, capobj;
static int	family;			/* 'T', 'H', 'C'; 'D' a daisy wheel */
static int	psout;			/* PostScript rather than PDF */
static double	pw, ph;			/* the page */
static double	y;			/* the base line of the next line */
static double	colw, x0, fsz;		/* a column, column 0, the font's size */
static int	inpage, npages;
static int	*pageobj;
static char	*cont;			/* the page's content stream */
static long	ncont, capcont;

static void	setline(const struct pdfhint *h);
static void	sethmi(int n);

/* the line being collected */
static unsigned char	cc[MAXC], ca[MAXC], csup[MAXC];
static int		ncells;

static void
w(const char *s, long n)
{
	if (pdff)
		fwrite(s, 1, n, pdff);
	pos += n;
}

static void
ws(const char *s)
{
	w(s, (long)strlen(s));
}

static void
cput(const char *s)
{
	long n = (long)strlen(s);

	if (psout) {
		w(s, n);
		return;
	}
	if (ncont + n + 1 > capcont) {
		capcont = (ncont + n + 1) * 2 + 4096;
		cont = (char *)xrealloc(cont, capcont);
	}
	memcpy(cont + ncont, s, n);
	ncont += n;
}

/* object n starts here */
static void
objstart(int n)
{
	char b[32];

	while (n >= capobj) {
		capobj = capobj * 2 + 64;
		objoff = (long *)xrealloc(objoff, capobj * sizeof(long));
	}
	if (n >= nobj)
		nobj = n + 1;
	objoff[n] = pos;
	sprintf(b, "%d 0 obj\n", n);
	ws(b);
}

/* objects: 1 the catalogue, 2 the pages, 3-6 the fonts, then for each
   page its content and itself */
#define OBJ_FIRSTPAGE	7

static const char *fontnames[3][4] = {
	{ "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic" },
	{ "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique" },
	{ "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique" }
};

/* ps: PostScript (a printer of the type PS, PSSANS, PSMONO) */
void
pdf_begin(FILE *f, int fam, int ps)
{
	const char *paper = getenv("LEXPAPER");

	pdff = f;
	pos = 0;
	nobj = 0;
	family = fam;
	npages = 0;
	inpage = 0;
	ncells = 0;
	if (paper && (paper[0] == 'l' || paper[0] == 'L')) {
		pw = 612;
		ph = 792;
	} else {
		pw = 595.28;
		ph = 841.89;
	}
	/* Courier: LEX's 10 characters to the inch; the proportional
	   fonts on a grid of 12 to the inch, which their text about fills
	   ($LEXPDF="grid,size" in points changes it: "7.2,15") */
	colw = fam == 'C' ? 7.2 : 6.0;
	fsz = fam == 'H' ? 12 : fam == 'C' ? 12 : 13;
	if (getenv("LEXPDF") && sscanf(getenv("LEXPDF"), "%lf,%lf", &colw, &fsz) >= 1) {
		if (colw < 3 || colw > 20)
			colw = 6;
		if (fsz < 4 || fsz > 40)
			fsz = 12;
	}
	x0 = (pw - 80 * colw) / 2;	/* 80 columns in the middle */
	psout = ps;
	if (ps) {
		char b[200];
		int i, fm = fam == 'H' ? 1 : fam == 'C' ? 2 : 0;

		ws("%!PS-Adobe-3.0\n%%Creator: nylex\n%%Pages: (atend)\n");
		sprintf(b, "%%%%BoundingBox: 0 0 %d %d\n%%%%DocumentData: Clean7Bit\n%%%%EndComments\n%%%%BeginProlog\n",
		    (int)pw, (int)ph);
		ws(b);
		/* ISO 8859-1, with ' ` - as they are in ASCII */
		ws("/nyenc ISOLatin1Encoding 256 array copy def\n"
		    "nyenc 39 /quotesingle put nyenc 96 /grave put nyenc 45 /hyphen put\n"
		    "/nyfont { findfont dup length dict begin { 1 index /FID ne { def } { pop pop } ifelse } forall\n"
		    "  /Encoding nyenc def currentdict end definefont pop } bind def\n"
		    "/R { newpath 4 2 roll moveto 1 index 0 rlineto 0 exch rlineto neg 0 rlineto closepath fill } bind def\n");
		for (i = 0; i < 4; i++) {
			sprintf(b, "/nyF%d /%s nyfont\n/F%d { /nyF%d findfont exch scalefont setfont } bind def\n",
			    i + 1, fontnames[fm][i], i + 1, i + 1);
			ws(b);
		}
		ws("%%EndProlog\n");
		sprintf(b, "%%%%BeginSetup\n<< /PageSize [%d %d] >> setpagedevice\n%%%%EndSetup\n", (int)(pw + 0.5), (int)(ph + 0.5));
		ws(b);
		return;
	}
	ws("%PDF-1.4\n%\342\343\317\323\n");
}

/* a proportional daisy wheel (a ";" record): the increments (1/120
   inch) before and after each character 32-126, and how to send */
static unsigned char	dbefore[95], dafter[95];
static void		(*dbytes)(const char *, int);
static void		(*dattr)(int);
static void		(*dchar)(int);
static int		dhmi;		/* the HMI set, in increments */

static double
cwidth(int c, int fi)
{
	static const short *tab[2][4] = {
		{ w_TR, w_TB, w_TI, w_TBI },
		{ w_HR, w_HB, w_HR, w_HB }
	};
	int i;

	if (family == 'C')
		return 600;
	if (family == 'D') {
		if (c < 32 || c > 126)
			c = 'n';
		return (dbefore[c - 32] + dafter[c - 32]) * 1000.0;
	}
	if (c >= 32 && c <= 126)
		i = c - 32;
	else if (c >= 160 && c <= 255)
		i = 95 + c - 160;
	else
		return 500;
	return tab[family == 'H'][fi][i];
}

/* printing on a daisy wheel with the table of a ";" record (95 pairs
   of digits from its second character); bytes, attributes and
   characters go to the functions given */
void
daisy_begin(const char *tab, long len, void (*bytes)(const char *, int), void (*attr)(int), void (*chr)(int))
{
	int i;

	family = 'D';
	colw = 12;
	x0 = 0;
	fsz = 1;
	inpage = 1;
	ncells = 0;
	dbytes = bytes;
	dattr = attr;
	dchar = chr;
	dhmi = -1;
	for (i = 0; i < 95; i++) {
		int o = 1 + 2 * i;

		dbefore[i] = (unsigned char)(o < len && tab[o] >= '0' && tab[o] <= '9' ? tab[o] - '0' : 0);
		dafter[i] = (unsigned char)(o + 1 < len && tab[o + 1] >= '0' && tab[o + 1] <= '9' ? tab[o + 1] - '0' : 6);
	}
}

/* the line collected goes to the daisy wheel */
void
daisy_flush(const struct pdfhint *h)
{
	setline(h);
	ncells = 0;
}

/* back to 10 characters to the inch */
void
daisy_end(void)
{
	sethmi(12);
	family = 0;
	inpage = 0;
}

static double
fsize(void)
{
	return fsz;
}

/* a character of the document as WinAnsi */
static int
ansi(int c)
{
	static const char s7[] = "[\\]{|}";
	static const unsigned char l1[] = { 0xc4, 0xd6, 0xc5, 0xe4, 0xf6, 0xe5 };
	const char *p;

	if (family == 'D')
		return c;
	if (t_national && c && (p = strchr(s7, c)) != NULL)
		return l1[p - s7];
	if (c < 32 || (c >= 127 && c < 160))
		return ' ';
	return c;
}

static int
fontof(int a)
{
	return ((a & (2 | 16)) ? 1 : 0) | ((a & 4) ? 2 : 0);
}

static void
page_begin(void)
{
	inpage = 1;
	ncont = 0;
	y = ph - 45;
	if (psout) {
		char b[60];

		sprintf(b, "%%%%Page: %d %d\nsave\n", npages + 1, npages + 1);
		ws(b);
	}
}

void
pdf_put(int col, int c, int attr, int sup)
{
	if (col < 0 || col >= MAXC)
		return;
	if (!inpage)
		page_begin();
	while (ncells < col) {
		cc[ncells] = ' ';
		ca[ncells] = 0;
		csup[ncells] = 0;
		ncells++;
	}
	cc[col] = (unsigned char)ansi(c);
	ca[col] = (unsigned char)(attr & 0x1f);
	csup[col] = (unsigned char)sup;
	if (col >= ncells)
		ncells = col + 1;
}

int
pdf_col(void)
{
	return ncells;
}

static double
colx(int col)
{
	return x0 + col * colw;
}

static int
isnumchar(int c)
{
	return (c >= '0' && c <= '9') || c == '.' || c == ',' || c == '-' || c == '+' || c == '%';
}

/* the width of a cell as set (points) */
static double
cellw(int i)
{
	double s = fsize() * (csup[i] ? 0.65 : 1.0);

	return cwidth(cc[i], fontof(ca[i])) * s / 1000.0;
}

static void
esc_str(char *b, int c)
{
	if (c == '(' || c == ')' || c == '\\')
		sprintf(b, "\\%c", c);
	else if (c >= 128)
		sprintf(b, "\\%03o", c);
	else {
		b[0] = (char)c;
		b[1] = 0;
	}
}

/* Diablo 630 (and the daisy-wheel printers that follow it): ESC US n
   sets the motion index to (n-1)/120 inch; a space moves that far */
static void
sethmi(int n)
{
	char b[3];

	if (n == dhmi)
		return;
	b[0] = 27;
	b[1] = 31;
	b[2] = (char)(n + 1);
	dbytes(b, 3);
	dhmi = n;
}

static void
move(int d)
{
	while (d > 0) {
		int k = d > 126 ? 126 : d;

		sethmi(k);
		dbytes(" ", 1);
		d -= k;
	}
}

static void
daisy_line(const double *x, int b, int e)
{
	int i, cur = 0;

	for (i = b; i <= e; i++) {
		int c = cc[i], to;

		if (c == ' ')
			continue;
		if (c < 32 || c > 126)
			c = 'n';
		to = (int)(x[i] + 0.5) + dbefore[c - 32];
		/* the space before: underlined when the text round it is */
		dattr(i > b && (ca[i - 1] & (1 | 8)) && (ca[i] & (1 | 8)) ? ca[i] : ca[i] & ~(1 | 8));
		if (to > cur)
			move(to - cur);
		dattr(ca[i]);
		sethmi(dafter[c - 32] > 0 ? dafter[c - 32] : 1);
		dchar(cc[i]);
		cur = to + (dafter[c - 32] > 0 ? dafter[c - 32] : 1);
	}
	dattr(0);
}

/* the line collected, at the base line y */
static void
setline(const struct pdfhint *h)
{
	double x[MAXC], wd[MAXC], spw = cwidth(' ', 0) * fsize() / 1000.0;
	int b, e, i, j;
	char buf[160];

	for (b = 0; b < ncells && cc[b] == ' '; b++)
		;
	for (e = ncells - 1; e >= b && cc[e] == ' '; e--)
		;
	if (b > e)
		return;
	for (i = 0; i < ncells; i++) {
		x[i] = colx(i);
		wd[i] = family == 'C' ? colw : cellw(i);
	}
	if (family == 'C')
		;
	else if (h && h->just) {
		/* the words spread from the first column to the margin */
		double tot = 0, gap, at;
		int nw = 0;

		for (i = b; i <= e; i++)
			if (cc[i] != ' ') {
				tot += wd[i];
				if (i == b || cc[i - 1] == ' ')
					nw++;
			}
		gap = nw > 1 ? (colx(e + 1) - colx(b) - tot) / (nw - 1) : spw;
		if (gap < spw * 0.5)
			gap = spw * 0.5;
		at = colx(b);
		for (i = b; i <= e; i++) {
			if (cc[i] == ' ') {
				x[i] = at;
				wd[i] = 0;
				if (cc[i + 1] != ' ') {
					wd[i] = gap;
					at += gap;
				}
				continue;
			}
			x[i] = at;
			at += wd[i];
		}
	} else {
		/* pieces: words with single spaces between them */
		double prevend = -1;
		int s = b, npieces = 0;

		for (i = b; i <= e; i++)
			if (cc[i] != ' ' && (i == b || (cc[i - 1] == ' ' && i >= 2 && cc[i - 2] == ' ')))
				npieces++;
		while (s <= e) {
			int t = s, num = 1, digit = 0;
			double wsum = 0, at;

			while (t + 1 <= e && !(cc[t + 1] == ' ' && (t + 2 > e || cc[t + 2] == ' ')))
				t++;
			for (i = s; i <= t; i++) {
				if (cc[i] == ' ')
					wd[i] = spw * (csup[i] ? 0.65 : 1.0);
				wsum += wd[i];
				if (cc[i] >= '0' && cc[i] <= '9')
					digit = 1;
				else if (!isnumchar(cc[i]) && cc[i] != ' ')
					num = 0;
			}
			if (num && digit)
				at = colx(t + 1) - wsum;
			else if (npieces == 1 && h && h->right > h->left && h->left >= 0 &&
			    (h->kind == 'C' || (s > h->left + 1 && abs((s + t) - (h->left + h->right - 1)) <= 2)))
				at = (colx(h->left) + colx(h->right) - wsum) / 2;
			else if (npieces == 1 && h && h->right > h->left && h->left >= 0 &&
			    t == h->right - 1 && s > h->left + (h->right - h->left) / 3)
				at = colx(t + 1) - wsum;
			else
				at = colx(s);
			if (prevend >= 0 && at < prevend + spw)
				at = prevend + spw;
			for (i = s; i <= t; i++) {
				x[i] = at;
				at += wd[i];
			}
			prevend = at;
			for (s = t + 1; s <= e && cc[s] == ' '; s++)
				;
		}
	}
	if (family == 'D') {
		daisy_line(x, b, e);
		return;
	}
	/* the text: runs of one font set at their places */
	for (i = b; i <= e; i = j) {
		int fi = fontof(ca[i]), sup = csup[i];
		double sz = fsize() * (sup ? 0.65 : 1.0), at;

		if (cc[i] == ' ') {
			j = i + 1;
			continue;
		}
		if (psout)
			sprintf(buf, "%.2f F%d %.2f %.2f moveto (", sz, fi + 1, x[i], y + (sup ? 4.2 : 0));
		else
			sprintf(buf, "BT /F%d %.2f Tf 1 0 0 1 %.2f %.2f Tm (", fi + 1, sz, x[i], y + (sup ? 4.2 : 0));
		cput(buf);
		at = x[i];
		for (j = i; j <= e; j++) {
			char eb[8];

			if (fontof(ca[j]) != fi || csup[j] != sup)
				break;
			if (j > i && (x[j] - at > 0.01 || x[j] - at < -0.01))
				break;
			if (cc[j] == ' ' && (family == 'C' ? 0 : wd[j] - cwidth(' ', fi) * sz / 1000.0 > 0.01))
				break;
			esc_str(eb, cc[j]);
			cput(eb);
			at = x[j] + (cc[j] == ' ' && family != 'C' ? cwidth(' ', fi) * sz / 1000.0 : wd[j]);
		}
		cput(psout ? ") show\n" : ") Tj ET\n");
		if (j == i)
			j = i + 1;
	}
	/* the underlines */
	for (i = b; i <= e; i++) {
		double x1, x2;

		if (!(ca[i] & (1 | 8)))
			continue;
		x1 = x[i];
		for (j = i; j <= e && (ca[j] & (1 | 8)); j++)
			;
		x2 = x[j - 1] + wd[j - 1];
		sprintf(buf, psout ? "%.2f %.2f %.2f 0.6 R\n" : "%.2f %.2f %.2f 0.6 re f\n", x1, y - 2.2, x2 - x1);
		cput(buf);
		i = j - 1;
	}
}

/* the line collected is set; then down so many quarter lines */
void
pdf_newline(int quarters, const struct pdfhint *h)
{
	if (!inpage)
		page_begin();
	setline(h);
	ncells = 0;
	y -= quarters * LINEQ;
}

/* the line collected is set; the next one is printed over it */
void
pdf_overprint(const struct pdfhint *h)
{
	if (!inpage)
		page_begin();
	setline(h);
	ncells = 0;
}

void
pdf_endpage(void)
{
	char b[200];
	int co, po;

	if (!inpage)
		page_begin();
	if (ncells)
		pdf_newline(4, NULL);
	if (psout) {
		ws("restore showpage\n");
		npages++;
		inpage = 0;
		return;
	}
	co = OBJ_FIRSTPAGE + 2 * npages;
	po = co + 1;
	objstart(co);
	sprintf(b, "<< /Length %ld >>\nstream\n", ncont);
	ws(b);
	w(cont ? cont : "", ncont);
	ws("\nendstream\nendobj\n");
	objstart(po);
	sprintf(b, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R\n"
	    "   /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> >>\nendobj\n", pw, ph, co);
	ws(b);
	pageobj = (int *)xrealloc(pageobj, (npages + 1) * sizeof(int));
	pageobj[npages++] = po;
	inpage = 0;
	ncont = 0;
}

int
pdf_inpage(void)
{
	return inpage;
}

void
pdf_finish(void)
{
	char b[200];
	int i, fam = family == 'H' ? 1 : family == 'C' ? 2 : 0;
	long xref;

	if (inpage || npages == 0)
		pdf_endpage();
	if (psout) {
		sprintf(b, "%%%%Trailer\n%%%%Pages: %d\n%%%%EOF\n", npages);
		ws(b);
		psout = 0;
		pdff = NULL;
		return;
	}
	for (i = 0; i < 4; i++) {
		objstart(3 + i);
		sprintf(b, "<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>\nendobj\n", fontnames[fam][i]);
		ws(b);
	}
	objstart(2);
	ws("<< /Type /Pages /Kids [");
	for (i = 0; i < npages; i++) {
		sprintf(b, "%s%d 0 R", i ? " " : "", pageobj[i]);
		ws(b);
	}
	sprintf(b, "] /Count %d >>\nendobj\n", npages);
	ws(b);
	objstart(1);
	ws("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
	xref = pos;
	sprintf(b, "xref\n0 %d\n0000000000 65535 f \n", nobj);
	ws(b);
	for (i = 1; i < nobj; i++) {
		sprintf(b, "%010ld 00000 n \n", objoff[i]);
		ws(b);
	}
	sprintf(b, "trailer\n<< /Size %d /Root 1 0 R /Info << /Producer (nylex %s) >> >>\nstartxref\n%ld\n%%%%EOF\n",
	    nobj, LEX_VERSION, xref);
	ws(b);
	xfree(cont);
	cont = NULL;
	capcont = ncont = 0;
	xfree(pageobj);
	pageobj = NULL;
	xfree(objoff);
	objoff = NULL;
	capobj = nobj = 0;
	pdff = NULL;
}
